/* simpleinit.c - poe@daimi.aau.dk */
/* Version 1.21 */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <termios.h>
#include <utmp.h>
#include <setjmp.h>
#include <sched.h>
#ifdef SHADOW_PWD
#  include <shadow.h>
#endif
#include "my_crypt.h"
#include "pathnames.h"
#include "linux_reboot.h"
#include "nls.h"
#include "simpleinit.h"

#define CMDSIZ     150	/* max size of a line in inittab */
#define NUMCMD     30	/* max number of lines in inittab */
#define NUMTOK     20	/* max number of tokens in inittab command */
#define PATH_SIZE  (CMDSIZ+CMDSIZ+1)

#define MAX_RESPAWN_RATE  5  /*  number of respawns per 100 seconds  */

#define TZFILE "/etc/TZ"
char tzone[CMDSIZ];
/* #define DEBUGGING */

/* Define this if you want init to ignore the termcap field in inittab for
   console ttys. */
/* #define SPECIAL_CONSOLE_TERM */

#define ever (;;)

struct initline {
	pid_t		pid;
	char		tty[10];
	char		termcap[30];
	char		*toks[NUMTOK];
	char		line[CMDSIZ];
	struct timeval	last_start;
	signed long	rate;
};

struct initline inittab[NUMCMD];
int numcmd;
int stopped = 0;	/* are we stopped */
static char boot_script[PATH_SIZE] = _PATH_RC;
static char script_prefix[PATH_SIZE] = "\0";
static int caught_sigint = 0;
static const char *initctl_name = "/dev/initctl";
static int initctl_fd = -1;
static volatile int do_longjmp = 0;
static sigjmp_buf jmp_env;


static void do_single ();
static int do_rc_tty (const char *path);
static int process_path ( const char *path, int (*func) (const char *path) );
static int preload_file (const char *path);
static int run_file (const char *path);
void spawn(), hup_handler(), read_inittab();
void tstp_handler ();
void int_handler ();
static void sigchild_handler (int sig);
static void sigquit_handler (int sig);
void set_tz(), write_wtmp();
static pid_t mywaitpid (pid_t pid, int *status, int *rc_status);
static int run_command (const char *path, const char *name, pid_t pid);
static void forget_those_not_present ();


void err(char *s)
{
	int fd;
	
	if((fd = open("/dev/console", O_WRONLY)) < 0) return;

	write(fd, "init: ", 6);	
	write(fd, s, strlen(s));
	close(fd);
}

void
enter_single()
{
    pid_t pid;
    int i;

    err(_("Booting to single user mode.\n"));
    if((pid = fork()) == 0) {
	/* the child */
	execl(_PATH_BSHELL, _PATH_BSHELL, NULL);
	err(_("exec of single user shell failed\n"));
    } else if(pid > 0) {
	while (waitpid (pid, &i, 0) != pid)  /*  Nothing  */;
    } else if(pid < 0) {
	err(_("fork of single user shell failed\n"));
    }
    unlink(_PATH_SINGLE);
}

int main(int argc, char *argv[])
{
	int			vec,i;
	int			want_single = 0;
	pid_t			pid;
	struct sigaction	sa;


#ifdef SET_TZ
	set_tz();
#endif
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = 0;
	signal (SIGTSTP, tstp_handler);
	signal (SIGINT, int_handler);
	sa.sa_handler = sigchild_handler;
	sigaction (SIGCHLD, &sa, NULL);
	sa.sa_handler = sigquit_handler;
	sigaction (SIGQUIT, &sa, NULL);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	my_reboot (LINUX_REBOOT_CMD_CAD_OFF);
	/*  Find script to run. Command-line overrides config file overrides
	    built-in default  */
	for (i = 0; i < NUMCMD; i++) inittab[i].pid = -1;
	read_inittab ();
	for (i = 1; i < argc; i++) {
		if (strcmp (argv[i], "single") == 0) want_single = 1;
		else {
			char path[PATH_SIZE];

			strcpy (path, script_prefix);
			strcat (path, argv[i]);
			if (access (path, R_OK | X_OK) == 0)
				strcpy (boot_script, path);
		}
	}

	if ( ( initctl_fd = open (initctl_name, O_RDWR, 0) ) < 0 ) {
		mkfifo (initctl_name, S_IRUSR | S_IWUSR);
		if ( ( initctl_fd = open (initctl_name, O_RDWR, 0) ) < 0 )
			err ( _("error opening fifo\n") );
	}

	if ( want_single || (access (_PATH_SINGLE, R_OK) == 0) ) do_single ();

	/*If we get a SIGTSTP before multi-user mode, do nothing*/
	while(stopped)	
		pause();

	if ( do_rc_tty (boot_script) ) do_single ();

	while(stopped)	/*Also if /etc/rc fails & we get SIGTSTP*/
		pause();

	write_wtmp();	/* write boottime record */
#ifdef DEBUGGING
	for(i = 0; i < numcmd; i++) {
		char **p;
		p = inittab[i].toks;
		printf("toks= %s %s %s %s\n",p[0], p[1], p[2], p[3]);
		printf("tty= %s\n", inittab[i].tty);
		printf("termcap= %s\n", inittab[i].termcap);
	}
	exit(0);
#endif
	signal(SIGHUP, hup_handler);

	for (i = 0; i < getdtablesize (); i++)
		if (i != initctl_fd) close (i);

	for(i = 0; i < numcmd; i++)
		spawn(i);
	
	for ever {
		pid = mywaitpid (-1, &vec, NULL);
		if (pid == 0) continue;

		/* clear utmp entry, and append to wtmp if possible */
		{
		    struct utmp *ut;
		    int ut_fd, lf;

		    utmpname(_PATH_UTMP);
		    setutent();
		    while((ut = getutent())) {
			if(ut->ut_pid == pid) {
			    time(&ut->ut_time);
			    memset(&ut->ut_user, 0, UT_NAMESIZE);
			    memset(&ut->ut_host, 0, sizeof(ut->ut_host));
			    ut->ut_type = DEAD_PROCESS;
			    ut->ut_pid = 0;
			    ut->ut_addr = 0;
			    /*endutent();*/
			    pututline(ut);

			    if ((lf = open(_PATH_WTMPLOCK, O_CREAT|O_WRONLY, 0660)) >= 0) {
				flock(lf, LOCK_EX|LOCK_NB);
				if((ut_fd = open(_PATH_WTMP, O_APPEND|O_WRONLY)) >= 0) {
				    write(ut_fd, ut, sizeof(struct utmp));
				    close(ut_fd);
				}
				flock(lf, LOCK_UN|LOCK_NB);
				close(lf);
			    }
			    break;
			}
		    }
		    endutent();
		}

		for(i = 0; i < numcmd; i++) {
			if(pid == inittab[i].pid || inittab[i].pid < 0) {
				if(stopped) inittab[i].pid = -1;
				else spawn(i);
				break;
			}
		}
	}
}	

#define MAXTRIES 3 /* number of tries allowed when giving the password */

/*
 * return true if singleuser mode is allowed.
 * If /etc/securesingle exists ask for root password, otherwise always OK.
 */
static int check_single_ok ()
{
    char *pass, *rootpass = NULL;
    struct passwd *pwd;
    int i;

    if (access (_PATH_SECURE, R_OK) != 0) return 1;
    if ( ( pwd = getpwnam ("root") ) || ( pwd = getpwuid (0) ) )
	rootpass = pwd->pw_passwd;
    else
	return 1; /* a bad /etc/passwd should not lock out */

    for (i = 0; i < MAXTRIES; i++)
    {
	pass = getpass (_ ("Password: ") );
	if (pass == NULL) continue;
		
	if ( !strcmp (crypt (pass, rootpass), rootpass) ) return 1;

	puts (_ ("\nWrong password.\n") );
    }
    return 0;
}

static void do_single ()
{
    char path[PATH_SIZE];

    if (caught_sigint) return;
    strcpy (path, script_prefix);
    strcat (path, "single");
    if (access (path, R_OK | X_OK) == 0)
	if (do_rc_tty (path) == 0) return;
    if ( check_single_ok () ) enter_single ();
}   /*  End Function do_single  */

/*
 * run boot script(s). The environment is passed to the script(s), so the RC
 * environment variable can be used to decide what to do.
 * RC may be set from LILO.
 * [RETURNS] 0 on success (exit status convention), otherwise error.
 */
static int do_rc_tty (const char *path)
{
    int status, rc_status = 0;
    pid_t pid;
    sigset_t ss;

    if (caught_sigint) return 0;
    process_path (path, preload_file);
    /*  Launch off a subprocess to start a new session (required for frobbing
	the TTY) and capture control-C  */
    switch ( pid = fork () )
    {
      case 0:   /*  Child  */
	for (status = 1; status < NSIG; status++) signal (status, SIG_DFL);
	sigfillset (&ss);
	sigprocmask (SIG_UNBLOCK, &ss, NULL);
	sigdelset (&ss, SIGINT);
	sigdelset (&ss, SIGQUIT);
	setsid ();
	ioctl (0, TIOCSCTTY, 0);  /*  I want my control-C  */
	sigsuspend (&ss);  /*  Should never return, should just be killed  */
	break;             /*  No-one is controlled by this TTY now       */
      case -1:  /*  Error  */
	return (1);
	/*break;*/
      default:  /*  Parent  */
	break;
    }
    /*  Parent  */
    process_path (path, run_file);
    while (rc_status == 0)
	if (mywaitpid (-1, &status, &rc_status) == pid)
	    return (WTERMSIG (status) == SIGINT) ? 0 : 1;
    forget_those_not_present ();
    kill (pid, SIGKILL);
    while (waitpid (pid, NULL, 0) != pid)  /*  Nothing  */;
    return (rc_status < 0) ? 1 : 0;
}   /*  End Function do_rc_tty  */

static int process_path ( const char *path, int (*func) (const char *path) )
{
    struct stat statbuf;
    DIR *dp;
    struct dirent *de;

    if (stat (path, &statbuf) != 0)
    {
	err (_ ("stat of path failed\n") );
	return 1;
    }
    if ( !( statbuf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH) ) ) return 0;
    if ( !S_ISDIR (statbuf.st_mode) ) return (*func) (path);
    if ( ( dp = opendir (path) ) == NULL )
    {
	err (_ ("open of directory failed\n") );
	return 1;
    }
    while ( ( de = readdir (dp) ) != NULL )
    {
	int retval;
	char newpath[PATH_SIZE];

	if (de->d_name[0] == '.') continue;
	retval = sprintf (newpath, "%s/%s", path, de->d_name);
	if (newpath[retval - 1] == '~') continue;  /*  Common mistake  */
	if ( ( retval = process_path (newpath, func) ) ) return retval;
    }
    closedir (dp);
    return 0;
}   /*  End Function process_path  */

static int preload_file (const char *path)
{
    int fd;
    char ch;

    if ( ( fd = open (path, O_RDONLY, 0) ) < 0) return 0;
    while (read (fd, &ch, 1) == 1) lseek (fd, 1023, SEEK_CUR);
    close (fd);
    return 0;
}   /*  End Function preload_file  */

static int run_file (const char *path)
{
    const char *ptr;

    if ( ( ptr = strrchr ( (char *) path, '/' ) ) == NULL ) ptr = path;
    else ++ptr;
    return (run_command (path, ptr, 0) == SIG_FAILED) ? 1 : 0;
}   /*  End Function preload_file  */

void spawn(int i)
{
	pid_t pid;
	int j;
	signed long ds_taken;
	struct timeval ct;

	if (inittab[i].toks[0] == NULL) return;
	/*  Check if respawning too fast  */
	gettimeofday (&ct, NULL);
	ds_taken = ct.tv_sec - inittab[i].last_start.tv_sec;
	ds_taken *= 10;
	ds_taken += (ct.tv_usec - inittab[i].last_start.tv_usec) / 100000;
	if (ds_taken < 1)
		ds_taken = 1;
	inittab[i].rate = (9 * inittab[i].rate + 1000 / ds_taken) / 10;
	if (inittab[i].rate > MAX_RESPAWN_RATE) {
		char txt[256];

		inittab[i].toks[0] = NULL;
		inittab[i].pid = -1;
		inittab[i].rate = 0;
		sprintf (txt,"respawning: \"%s\" too fast: quenching entry\n",
			 inittab[i].tty);
		err (_(txt));
		return;
	}

	if((pid = fork()) < 0) {
		inittab[i].pid = -1;
		err(_("fork failed\n"));
		return;
	}
	if(pid) {
		/* this is the parent */
		inittab[i].pid = pid;
		inittab[i].last_start = ct;
		return;
	} else {
		/* this is the child */
		char term[40];
#ifdef SET_TZ
		char tz[CMDSIZ];
#endif
		char *env[3];
		
		setsid();
		for(j = 0; j < getdtablesize(); j++)
			(void) close(j);

		(void) sprintf(term, "TERM=%s", inittab[i].termcap);
		env[0] = term;
		env[1] = (char *)0;
#ifdef SET_TZ
		(void) sprintf(tz, "TZ=%s", tzone);
		env[1] = tz;
#endif
		env[2] = (char *)0;

		execve(inittab[i].toks[0], inittab[i].toks, env);
		err(_("exec failed\n"));
		sleep(5);
		_exit(1);
	}
}

void read_inittab()
{
	FILE *f;
	char buf[CMDSIZ];
	int i,j,k;
	int has_prog = 0;
	char *ptr, *getty;
	char prog[PATH_SIZE];
#ifdef SPECIAL_CONSOLE_TERM
	char tty[50];
	struct stat stb;
#endif
	char *termenv, *getenv();
	
	termenv = getenv("TERM");	/* set by kernel */
	/* termenv = "vt100"; */
			
	if(!(f = fopen(_PATH_INITTAB, "r"))) {
		err(_("cannot open inittab\n"));
		return;
	}

	prog[0] = '\0';
	i = 0;
	while(!feof(f) && i < NUMCMD - 2) {
		if(fgets(buf, CMDSIZ - 1, f) == 0) break;
		buf[CMDSIZ-1] = 0;

		for(k = 0; k < CMDSIZ && buf[k]; k++) {
			if ((buf[k] == '#') || (buf[k] == '\n')) { 
				buf[k] = 0; break; 
			}
		}

		if(buf[0] == 0 || buf[0] == '\n') continue;
		ptr = strchr (buf, '=');
		if (ptr) {
			ptr++;
			if ( !strncmp (buf, "bootprog", 8) ) {
				while ( isspace (*ptr) ) ++ptr;
				strcpy (prog, ptr);
				has_prog = 1;
				continue;
			}
			if ( !strncmp (buf, "fileprefix", 10) ) {
				while ( isspace (*ptr) ) ++ptr;
				strcpy (script_prefix, ptr);
				continue;
			}
			if ( !strncmp (buf, "PATH", 4) ) {
				while ( isspace (*ptr) ) ++ptr;
				setenv ("PATH", ptr, 1);
				continue;
			}
		}
			

		(void) strcpy(inittab[i].line, buf);

		(void) strtok(inittab[i].line, ":");
		(void) strncpy(inittab[i].tty, inittab[i].line, 10);
		inittab[i].tty[9] = 0;
		(void) strncpy(inittab[i].termcap,
				strtok((char *)0, ":"), 30);
		inittab[i].termcap[29] = 0;

		getty = strtok((char *)0, ":");
		(void) strtok(getty, " \t\n");
		inittab[i].toks[0] = getty;
		j = 1;
		while((ptr = strtok((char *)0, " \t\n")))
			inittab[i].toks[j++] = ptr;
		inittab[i].toks[j] = (char *)0;

#ifdef SPECIAL_CONSOLE_TERM
		/* special-case termcap for the console ttys */
		(void) sprintf(tty, "/dev/%s", inittab[i].tty);
		if(!termenv || stat(tty, &stb) < 0) {
			err(_("no TERM or cannot stat tty\n"));
		} else {
			/* is it a console tty? */
			if(major(stb.st_rdev) == 4 && minor(stb.st_rdev) < 64) {
				strncpy(inittab[i].termcap, termenv, 30);
				inittab[i].termcap[29] = 0;
			}
		}
#endif

		i++;
	}
	fclose(f);
	numcmd = i;
	if (has_prog) {
		int len;
		char path[PATH_SIZE];

		strcpy (path, script_prefix);
		strcat (path, prog);
		len = strlen (path);
		if (path[len - 1] == '/') path[len - 1] = '\0';
		if (access (path, R_OK | X_OK) == 0)
			strcpy (boot_script, path);
	}
}

void hup_handler()
{
	int i,j;
	int oldnum;
	struct initline	savetab[NUMCMD];
	int had_already;

	(void) signal(SIGHUP, SIG_IGN);

	memcpy(savetab, inittab, NUMCMD * sizeof(struct initline));
	oldnum = numcmd;		
	read_inittab();
	
	for(i = 0; i < numcmd; i++) {
		had_already = 0;
		for(j = 0; j < oldnum; j++) {
			if(!strcmp(savetab[j].tty, inittab[i].tty)) {
				had_already = 1;
				if((inittab[i].pid = savetab[j].pid) < 0)
					spawn(i);
			}
		}
		if(!had_already) spawn(i);
	}
	
	(void) signal(SIGHUP, hup_handler);
}

void tstp_handler()
{
	stopped = ~stopped;
	if(!stopped) hup_handler();

	signal(SIGTSTP, tstp_handler);
}

void int_handler()
{
	pid_t pid;
	
	caught_sigint = 1;
	sync();
	sync();
	pid = fork();
	if (pid > 0)
		return;
	if (pid == 0) 	/* reboot properly... */
		execl(_PATH_REBOOT, _PATH_REBOOT, (char *)0);

	/* fork or exec failed, try the hard way... */
	my_reboot(LINUX_REBOOT_CMD_RESTART);
}

static void sigchild_handler (int sig)
{
    if (!do_longjmp) return;
    siglongjmp (jmp_env, 1);
}

static void sigquit_handler (int sig)
{
    execl (_PATH_REBOOT, _PATH_REBOOT, NULL); /*  It knows pid=1 must sleep  */
}

void set_tz()
{
	FILE *f;
	int len;

	if((f=fopen(TZFILE, "r")) == (FILE *)NULL) return;
	fgets(tzone, CMDSIZ-2, f);
	fclose(f);
	if((len=strlen(tzone)) < 2) return;
	tzone[len-1] = 0; /* get rid of the '\n' */
	setenv("TZ", tzone, 0);
}

void write_wtmp()
{
    int fd, lf;
    struct utmp ut;
    
    memset((char *)&ut, 0, sizeof(ut));
    strcpy(ut.ut_line, "~");
    memset(ut.ut_name, 0, sizeof(ut.ut_name));
    time(&ut.ut_time);
    ut.ut_type = BOOT_TIME;

    if ((lf = open(_PATH_WTMPLOCK, O_CREAT|O_WRONLY, 0660)) >= 0) {
	flock(lf, LOCK_EX|LOCK_NB); /* make sure init won't hang */
	if((fd = open(_PATH_WTMP, O_WRONLY|O_APPEND)) >= 0) {
	    write(fd, (char *)&ut, sizeof(ut));
	    close(fd);
	}
	flock(lf, LOCK_UN|LOCK_NB);
	close(lf);
    }
}     


struct waiter_struct
{
    struct waiter_struct *next;
    pid_t pid;
};

struct script_struct
{
    pid_t pid;
    struct script_struct *prev, *next;
    struct waiter_struct *first_waiter;
    char name[1];
};

struct list_head
{
    struct script_struct *first, *last;
};


static struct list_head available_list = {NULL, NULL};
static struct list_head starting_list = {NULL, NULL};
static struct list_head failed_list = {NULL, NULL};
static struct list_head unavailable_list = {NULL, NULL};


static void process_pidstat (pid_t pid, int status, int *rc_status);
static struct script_struct *find_script (const char *name,
					  struct list_head *head);
static void insert_entry (struct list_head *head, struct script_struct *entry);
static void remove_entry (struct list_head *head, struct script_struct *entry);
static void signal_waiters (struct script_struct *script, int sig);


static pid_t mywaitpid (pid_t pid, int *status, int *rc_status)
{
    int ival;
    struct script_struct *script;
    sigset_t ss_new, ss_old;
    long buffer[COMMAND_SIZE / sizeof (long)];
    struct command_struct *command = (struct command_struct *) buffer;

    if (initctl_fd < 0) return waitpid (pid, status, 0);
    if (status == NULL) status = &ival;
    if ( ( pid = waitpid (pid, status, WNOHANG) ) > 0 )
    {
	process_pidstat (pid, *status, rc_status);
	return pid;
    }
    /*  Some magic to avoid races  */
    command->command = -1;
    sigemptyset (&ss_new);
    sigaddset (&ss_new, SIGCHLD);
    sigprocmask (SIG_BLOCK, &ss_new, &ss_old);
    ival = sigsetjmp (jmp_env, 0);
    sigprocmask (SIG_SETMASK, &ss_old, NULL);
    if (ival == 0) do_longjmp = 1;
    else
    {
	do_longjmp = 0;
	if (command->command < 0) return 0;
    }
    if (command->command < 0) read (initctl_fd, buffer, COMMAND_SIZE);
    do_longjmp = 0;
    switch (command->command)
    {
      case COMMAND_TEST:
	kill (command->pid,
	      (find_script (command->name, &available_list) == NULL) ?
	      SIG_NOT_PRESENT : SIG_PRESENT);
	break;
      case COMMAND_NEED:
	ival = run_command (command->name, command->name, command->pid);
	if (ival != 0) kill (command->pid, ival);
	break;
      case COMMAND_ROLLBACK:
	if (command->name[0] == '\0') script = NULL;
	else
	{
	    if ( ( script = find_script (command->name, &available_list) )
		 == NULL )
	    {
		kill (command->pid, SIG_NOT_PRESENT);
		break;
	    }
	}
	while (script != available_list.first)
	{
	    struct script_struct *victim = available_list.first;

	    if ( ( pid = fork () ) == 0 )   /*  Child   */
	    {
		for (ival = 1; ival < NSIG; ival++) signal (ival, SIG_DFL);
		open ("/dev/console", O_RDONLY, 0);
		open ("/dev/console", O_RDWR, 0);
		dup2 (1, 2);
		execlp (victim->name, victim->name, "stop", NULL);
		err ( _("error running programme\n") );
		_exit (SIG_NOT_STOPPED);
	    }
	    else if (pid == -1) break;      /*  Error   */
	    else                            /*  Parent  */
	    {
		while (waitpid (pid, &ival, 0) != pid) /*  Nothing  */;
		if ( WIFEXITED (ival) && (WEXITSTATUS (ival) == 0) )
		{
		    remove_entry (&available_list, victim);
		    free (victim);
		}
		else break;
	    }
	}
	kill (command->pid,
	      (script ==available_list.first) ? SIG_STOPPED : SIG_NOT_STOPPED);
	break;
      case COMMAND_DUMP_LIST:
	if (fork () == 0)
	{
	    FILE *fp;

	    if ( ( fp = fopen (command->name, "w") ) == NULL ) _exit (1);
	    fputs ("AVAILABLE SERVICES:\n", fp);
	    for (script = available_list.first; script != NULL;
		 script = script->next) fprintf (fp, "%s\n", script->name);
	    fputs ("FAILED SERVICES:\n", fp);
	    for (script = failed_list.first; script != NULL;
		 script = script->next) fprintf (fp, "%s\n", script->name);
	    fclose (fp);
	    _exit (0);
	}
	break;
      case -1:
      default:
	break;
    }
    return 0;
}   /*  End Function mywaitpid  */

static void process_pidstat (pid_t pid, int status, int *rc_status)
{
    struct script_struct *script;

    if (initctl_fd < 0) return;
    if (pid < 1) return;
    for (script = starting_list.first; script != NULL; script = script->next)
	if (script->pid == pid) break;
    if (script == NULL) return;
    remove_entry (&starting_list, script);
    if ( WIFEXITED (status) && (WEXITSTATUS (status) == 0) )
    {
	signal_waiters (script, SIG_PRESENT);
	insert_entry (&available_list, script);
    }
    else if ( WIFEXITED (status) && (WEXITSTATUS (status) == 2) )
    {
	signal_waiters (script, SIG_NOT_PRESENT);
	insert_entry (&unavailable_list, script);
    }
    else
    {
	signal_waiters (script, SIG_FAILED);
	insert_entry (&failed_list, script);
    }
    if ( (rc_status == NULL) || (starting_list.first != NULL) ) return;
    *rc_status = (failed_list.first == NULL) ? 1 : -1;
}   /*  End Function process_pidstat  */

static int run_command (const char *path, const char *name, pid_t pid)
{
    struct script_struct *script;
    struct waiter_struct *waiter = NULL;

    if (find_script (name, &available_list) != NULL) return SIG_PRESENT;
    if (find_script (name, &failed_list) != NULL) return SIG_FAILED;
    if (find_script (name, &unavailable_list) != NULL) return SIG_NOT_PRESENT;
    if (pid != 0)
    {
	waiter = calloc (1, sizeof *waiter);
	if (waiter == NULL) return SIG_FAILED;
	waiter->pid = pid;
    }
    script = find_script (name, &starting_list);
    if (script == NULL)
    {
	int i;

	script = calloc (1, strlen (name) + sizeof *script);
	if (script == NULL)
	{
	    if (waiter != NULL) free (waiter);
	    return SIG_FAILED;
	}
	strcpy (script->name, name);
	switch ( script->pid = fork () )
	{
	  case 0:   /*  Child   */
	    for (i = 1; i < NSIG; i++) signal (i, SIG_DFL);
	    execlp (path, script->name, "start", NULL);
	    err ( _("error running programme\n") );
	    _exit (SIG_FAILED);
	    break;
	  case -1:  /*  Error   */
	    free (script);
	    if (waiter != NULL) free (waiter);
	    return SIG_FAILED;
	    /*break;*/
	  default:  /*  Parent  */
	    insert_entry (&starting_list, script);
	    sched_yield ();
	    break;
	}
    }
    if (waiter == NULL) return 0;
    waiter->next = script->first_waiter;
    script->first_waiter = waiter;
    return 0;
}   /*  End Function run_command  */

static struct script_struct *find_script (const char *name,
					  struct list_head *head)
{
    struct script_struct *entry;

    for (entry = head->first; entry != NULL; entry = entry->next)
    {
	if (strcmp (entry->name, name) == 0) return (entry);
    }
    return NULL;
}   /*  End Function find_script  */

static void insert_entry (struct list_head *head, struct script_struct *entry)
{
    if (entry == NULL) return;
    entry->prev = NULL;
    entry->next = head->first;
    if (head->first != NULL) head->first->prev = entry;
    head->first = entry;
    if (head->last == NULL) head->last = entry;
}   /*  End Function insert_entry  */

static void remove_entry (struct list_head *head, struct script_struct *entry)
{
    if (entry->prev == NULL) head->first = entry->next;
    else entry->prev->next = entry->next;
    if (entry->next == NULL) head->last = entry->prev;
    else entry->next->prev = entry->prev;
}   /*  End Function remove_entry  */

static void signal_waiters (struct script_struct *script, int sig)
{
    struct waiter_struct *waiter, *next_waiter;

    for (waiter = script->first_waiter; waiter != NULL; waiter = next_waiter)
    {
	kill (waiter->pid, sig);
	next_waiter = waiter->next;
	free (waiter);
    }
    script->first_waiter = NULL;
}   /*  End Function signal_waiters  */

static void forget_those_not_present ()
{
    struct script_struct *curr, *next;

    for (curr = failed_list.first; curr != NULL; curr = next)
    {
	next = curr->next;
	free (curr);
    }
    failed_list.first = NULL;
    failed_list.last = NULL;
    for (curr = unavailable_list.first; curr != NULL; curr = next)
    {
	next = curr->next;
	free (curr);
    }
    unavailable_list.first = NULL;
    unavailable_list.last = NULL;
}   /*  End Function forget_those_not_present  */
