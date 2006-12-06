/* simpleinit.c - poe@daimi.aau.dk */
/* Version 2.0.2 */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 2001-01-25 Richard Gooch <rgooch@atnf.csiro.au>
 * - fixed bug with failed services so they may be later "reclaimed"
 * 2001-02-02 Richard Gooch <rgooch@atnf.csiro.au>
 * - fixed race when reading from pipe and reaping children
 * 2001-02-18 sam@quux.dropbear.id.au
 * - fixed bug in <get_path>: multiple INIT_PATH components did not work
 * 2001-02-21 Richard Gooch <rgooch@atnf.csiro.au>
 * - block signals in handlers, so that longjmp() doesn't kill context
 * 2001-02-25 Richard Gooch <rgooch@atnf.csiro.au>
 * - make default INIT_PATH the boot_prog (if it is a directory) - YECCH
 * 2002-11-20 patch from SuSE
 * - refuse initctl_fd if setting FD_CLOEXEC fails
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
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
#include "xstrncpy.h"
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
static char boot_prog[PATH_SIZE] = _PATH_RC;
static char script_prefix[PATH_SIZE] = "\0";
static char final_prog[PATH_SIZE] = "\0";
static char init_path[PATH_SIZE] = "\0";
static int caught_sigint = 0;
static int no_reboot = 0;
static pid_t rc_child = -1;
static const char *initctl_name = "/dev/initctl";
static int initctl_fd = -1;
static volatile int do_longjmp = 0;
static sigjmp_buf jmp_env;


static void do_single (void);
static int do_rc_tty (const char *path);
static int process_path (const char *path, int (*func) (const char *path),
			 int ignore_dangling_symlink);
static int preload_file (const char *path);
static int run_file (const char *path);
static void spawn (int i), read_inittab (void);
static void sighup_handler (int sig);
static void sigtstp_handler (int sig);
static void sigint_handler (int sig);
static void sigchild_handler (int sig);
static void sigquit_handler (int sig);
static void sigterm_handler (int sig);
#ifdef SET_TZ
static void set_tz (void);
#endif
static void write_wtmp (void);
static pid_t mywait (int *status);
static int run_command (const char *file, const char *name, pid_t pid);


static void err (char *s)
{
	int fd;
	
	if((fd = open("/dev/console", O_WRONLY)) < 0) return;

	write(fd, "init: ", 6);	
	write(fd, s, strlen(s));
	close(fd);
}

static void enter_single (void)
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
	int			vec, i;
	int			want_single = 0;
	pid_t			pid;
	struct sigaction	sa;


#ifdef SET_TZ
	set_tz();
#endif
	sigfillset (&sa.sa_mask);  /*  longjmp and nested signals don't mix  */
	sa.sa_flags = SA_ONESHOT;
	sa.sa_handler = sigint_handler;
	sigaction (SIGINT, &sa, NULL);
	sa.sa_flags = 0;
	sa.sa_handler = sigtstp_handler;
	sigaction (SIGTSTP, &sa, NULL);
	sa.sa_handler = sigterm_handler;
	sigaction (SIGTERM, &sa, NULL);
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
		if (strcmp (argv[i], "single") == 0)
			want_single = 1;
		else if (strcmp (argv[i], "-noreboot") == 0)
			no_reboot = 1;
		else if (strlen(script_prefix) + strlen(argv[i]) < PATH_SIZE) {
			char path[PATH_SIZE];

			strcpy (path, script_prefix);
			strcat (path, argv[i]);
			if (access (path, R_OK | X_OK) == 0)
				strcpy (boot_prog, path);
		}
	}
	if (init_path[0] == '\0')
	{
	    struct stat statbuf;

	    if ( (stat (boot_prog, &statbuf) == 0) && S_ISDIR (statbuf.st_mode) )
	    {
		strcpy (init_path, boot_prog);
		i = strlen (init_path);
		if (init_path[i - 1] == '/') init_path[i - 1] = '\0';
	    }
	}

	if ( ( initctl_fd = open (initctl_name, O_RDWR, 0) ) < 0 ) {
		mkfifo (initctl_name, S_IRUSR | S_IWUSR);
		if ( ( initctl_fd = open (initctl_name, O_RDWR, 0) ) < 0 )
			err ( _("error opening fifo\n") );
	}

	if (initctl_fd >= 0 && fcntl(initctl_fd, F_SETFD, FD_CLOEXEC) != 0) {
		err ( _("error setting close-on-exec on /dev/initctl") );

		/* Can the fcntl ever fail?  If it does, and we leave
		   the descriptor open in child processes, then any
		   process on the system will be able to write to
		   /dev/initctl and have us execute arbitrary commands
		   as root. So let's refuse to use the fifo in this case. */

		close(initctl_fd);
		initctl_fd = -1;
	}

	if ( want_single || (access (_PATH_SINGLE, R_OK) == 0) ) do_single ();

	/*If we get a SIGTSTP before multi-user mode, do nothing*/
	while (stopped)
		pause();

	if ( do_rc_tty (boot_prog) ) do_single ();

	while (stopped)  /*  Also if /etc/rc fails & we get SIGTSTP  */
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
	signal (SIGHUP, sighup_handler);  /* Better semantics with signal(2) */

	for (i = 0; i < getdtablesize (); i++)
		if (i != initctl_fd) close (i);

	for(i = 0; i < numcmd; i++)
		spawn(i);

	if (final_prog[0] != '\0') {
		switch ( fork () )
		{
		  case 0:   /*  Child   */
		    execl (final_prog, final_prog, "start", NULL);
		    err ( _("error running finalprog\n") );
		    _exit (1);
		    break;
		  case -1:  /*  Error   */
		    err ( _("error forking finalprog\n") );
		    break;
		  default:  /*  Parent  */
		    break;
		}
	}

	for ever {
		pid = mywait (&vec);
		if (pid < 1) continue;

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
				if (stopped)
					inittab[i].pid = -1;
				else
					spawn(i);
				if (pid == inittab[i].pid)
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
static int check_single_ok (void)
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

static void do_single (void)
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
    int status;
    pid_t pid;
    sigset_t ss;

    if (caught_sigint) return 0;
    process_path (path, preload_file, 0);
    /*  Launch off a subprocess to start a new session (required for frobbing
	the TTY) and capture control-C  */
    switch ( rc_child = fork () )
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
	break;             /*  No-one else is controlled by this TTY now   */
      case -1:  /*  Error  */
	return (1);
	/*break;*/
      default:  /*  Parent  */
	break;
    }
    /*  Parent  */
    process_path (path, run_file, 0);
    while (1)
    {
	if ( ( pid = mywait (&status) ) == rc_child )
	    return (WTERMSIG (status) == SIGINT) ? 0 : 1;
	if (pid < 0) break;
    }
    kill (rc_child, SIGKILL);
    while (waitpid (rc_child, NULL, 0) != rc_child)  /*  Nothing  */;
    return 0;
}   /*  End Function do_rc_tty  */

static int process_path (const char *path, int (*func) (const char *path),
			 int ignore_dangling_symlink)
{
    struct stat statbuf;
    DIR *dp;
    struct dirent *de;

    if (lstat (path, &statbuf) != 0)
    {
	err (_ ("lstat of path failed\n") );
	return 1;
    }
    if ( S_ISLNK (statbuf.st_mode) )
    {
	if (stat (path, &statbuf) != 0)
	{
	    if ( (errno == ENOENT) && ignore_dangling_symlink ) return 0;
	    err (_ ("stat of path failed\n") );
	    return 1;
	}
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
	if ( ( retval = process_path (newpath, func, 1) ) ) return retval;
    }
    closedir (dp);
    return 0;
}   /*  End Function process_path  */

static int preload_file (const char *path)
{
    int fd;
    char ch;

    if ( ( fd = open (path, O_RDONLY, 0) ) < 0) return 0;
    while (read (fd, &ch, 1) == 1) lseek (fd, 1024, SEEK_CUR);
    close (fd);
    return 0;
}   /*  End Function preload_file  */

static int run_file (const char *path)
{
    const char *ptr;

    if ( ( ptr = strrchr ( (char *) path, '/' ) ) == NULL ) ptr = path;
    else ++ptr;
    return (run_command (path, ptr, 0) == SIG_FAILED) ? 1 : 0;
}   /*  End Function run_file  */

static void spawn (int i)
{
	pid_t pid;
	int j;
	signed long ds_taken;
	struct timeval ct;

	if (inittab[i].toks[0] == NULL) return;

	/*  Check if respawning too fast  */
	gettimeofday (&ct, NULL);
	ds_taken = ct.tv_sec - inittab[i].last_start.tv_sec;

	/* On the first iteration last_start==0 and ds_taken
	   may be very large. Avoid overflow. -- Denis Vlasenko */
	if (ds_taken > 10000)
		ds_taken = 10000;

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
		sched_yield ();
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

static void read_inittab (void)
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
	char *termenv;
	
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
			if ( !strncmp (buf, "INIT_PATH", 9) ) {
				while ( isspace (*ptr) ) ++ptr;
				strcpy (init_path, ptr);
				continue;
			}
			if ( !strncmp (buf, "finalprog", 8) ) {
				while ( isspace (*ptr) ) ++ptr;
				strcpy (final_prog, ptr);
				continue;
			}
		}
			

		(void) strcpy(inittab[i].line, buf);

		(void) strtok(inittab[i].line, ":");
		xstrncpy(inittab[i].tty, inittab[i].line, 10);
		xstrncpy(inittab[i].termcap, strtok((char *)0, ":"), 30);

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
			if(major(stb.st_rdev) == 4 && minor(stb.st_rdev) < 64)
				xstrncpy(inittab[i].termcap, termenv, 30);
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
			strcpy (boot_prog, path);
	}
}   /*  End Function read_inittab  */

static void sighup_handler (int sig)
{
	int i,j;
	int oldnum;
	struct initline	savetab[NUMCMD];
	int had_already;

	signal (SIGHUP, SIG_IGN);
	memcpy(savetab, inittab, NUMCMD * sizeof(struct initline));
	oldnum = numcmd;		
	read_inittab ();
	
	for(i = 0; i < numcmd; i++) {
		had_already = 0;
		for(j = 0; j < oldnum; j++) {
			if(!strcmp(savetab[j].tty, inittab[i].tty)) {
				had_already = 1;
				if((inittab[i].pid = savetab[j].pid) < 0)
					spawn(i);
			}
		}
		if (!had_already) spawn (i);
	}
	signal (SIGHUP, sighup_handler);
}   /*  End Function sighup_handler  */

static void sigtstp_handler (int sig)
{
    stopped = ~stopped;
    if (!stopped) sighup_handler (sig);
}   /*  End Function sigtstp_handler  */

static void sigterm_handler (int sig)
{
    int i;

    for (i = 0; i < numcmd; i++)
	if (inittab[i].pid > 0) kill (inittab[i].pid, SIGTERM);
}   /*  End Function sigterm_handler  */

static void sigint_handler (int sig)
{
    pid_t pid;

    caught_sigint = 1;
    kill (rc_child, SIGKILL);
    if (no_reboot) _exit (1) /*kill (0, SIGKILL)*/;
    sync ();
    sync ();
    pid = fork ();
    if (pid > 0) return;  /*  Parent                     */
    if (pid == 0) 	  /*  Child: reboot properly...  */
	execl (_PATH_REBOOT, _PATH_REBOOT, (char *) 0);

    /* fork or exec failed, try the hard way... */
    my_reboot (LINUX_REBOOT_CMD_RESTART);
}   /*  End Function sigint_handler  */

static void sigchild_handler (int sig)
{
    if (!do_longjmp) return;
    siglongjmp (jmp_env, 1);
}

static void sigquit_handler (int sig)
{
    execl (_PATH_REBOOT, _PATH_REBOOT, NULL); /*  It knows pid=1 must sleep  */
}

#ifdef SET_TZ
static void set_tz (void)
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
#endif

static void write_wtmp (void)
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
}   /*  End Function write_wtmp  */


struct needer_struct
{
    struct needer_struct *next;
    pid_t pid;
};

struct service_struct
{
    struct service_struct *prev, *next;    /*  Script services chain         */
    struct needer_struct *needers;         /*  Needers waiting for service   */
    struct script_struct *attempting_providers;
    int failed;                /*  TRUE if attempting provider failed badly  */
    char name[1];
};

struct script_struct
{
    pid_t pid;
    struct script_struct *prev, *next;              /*  For the list         */
    struct service_struct *first_service, *last_service; /*First is true name*/
    struct script_struct *next_attempting_provider; /*  Provider chain       */
};

struct list_head
{
    struct script_struct *first, *last;
    unsigned int num_entries;
};


static struct list_head available_list = {NULL, NULL, 0};
static struct list_head starting_list = {NULL, NULL, 0};
static struct service_struct *unavailable_services = NULL;  /*  For needers  */
static int num_needers = 0;


static int process_pidstat (pid_t pid, int status);
static void process_command (const struct command_struct *command);
static struct service_struct *find_service_in_list (const char *name,
						    struct service_struct *sv);
static struct script_struct *find_script_byname
    (const char *name,struct list_head *head, struct service_struct **service);
static struct script_struct *find_script_bypid (pid_t pid,
						struct list_head *head);
static void insert_entry (struct list_head *head, struct script_struct *entry);
static void remove_entry (struct list_head *head, struct script_struct *entry);
static void signal_needers (struct service_struct *service, int sig);
static void handle_nonworking (struct script_struct *script);
static int force_progress (void);
static void show_scripts (FILE *fp, const struct script_struct *script,
			  const char *type);
static const char *get_path (const char *file);


static pid_t mywait (int *status)
/*  [RETURNS] The pid for a process to be reaped, 0 if no process is to be
    reaped, and less than 0 if the boot scripts appear to have finished.
*/
{
    pid_t pid;
    sigset_t ss;
    long buffer[COMMAND_SIZE / sizeof (long)];
    struct command_struct *command = (struct command_struct *) buffer;

    if (initctl_fd < 0) return wait (status);
    /*  Some magic to avoid races which can result in lost signals   */
    command->command = -1;
    if ( sigsetjmp (jmp_env, 1) )
    {   /*  Jump from signal handler  */
	do_longjmp = 0;
	process_command (command);
	return 0;
    }
    sigemptyset (&ss);  /*  Block SIGCHLD so wait status cannot be lost  */
    sigaddset (&ss, SIGCHLD);
    sigprocmask (SIG_BLOCK, &ss, NULL);
    if ( ( pid = waitpid (-1, status, WNOHANG) ) > 0 )
    {
	sigprocmask (SIG_UNBLOCK, &ss, NULL);
	return process_pidstat (pid, *status);
    }
    do_longjmp = 1;  /*  After this, SIGCHLD will cause a jump backwards  */
    sigprocmask (SIG_UNBLOCK, &ss, NULL);
    read (initctl_fd, buffer, COMMAND_SIZE);
    do_longjmp = 0;
    process_command (command);
    return 0;
}   /*  End Function mywait  */

static pid_t process_pidstat (pid_t pid, int status)
/*  [RETURNS] The pid for a process to be reaped, 0 if no process is to be
    reaped, and less than 0 if the boot scripts appear to have finished.
*/
{
    int failed;
    struct script_struct *script;
    struct service_struct *service;

    if ( ( script = find_script_bypid (pid, &starting_list) ) == NULL )
	return pid;
    remove_entry (&starting_list, script);
    if ( WIFEXITED (status) && (WEXITSTATUS (status) == 0) )
    {
	struct script_struct *provider;

	/*  Notify needers and other providers  */
	for (service = script->first_service; service != NULL;
	     service = service->next)
	{
	    signal_needers (service, SIG_PRESENT);
	    for (provider = service->attempting_providers; provider != NULL;
		 provider = provider->next_attempting_provider)
		kill (provider->pid, SIG_PRESENT);
	    service->attempting_providers = NULL;
	}
	insert_entry (&available_list, script);
	return force_progress ();
    }
    failed = ( WIFEXITED (status) && (WEXITSTATUS (status) == 2) ) ? 0 : 1;
    for (service = script->first_service; service != NULL;
	 service = service->next)
	service->failed = failed;
    handle_nonworking (script);
    return force_progress ();
}   /*  End Function process_pidstat  */

static void process_command (const struct command_struct *command)
{
    int ival;
    struct script_struct *script;
    struct service_struct *service;

    switch (command->command)
    {
      case COMMAND_TEST:
	kill (command->pid,
	      (find_script_byname (command->name, &available_list,
				   NULL) == NULL) ?
	      SIG_NOT_PRESENT : SIG_PRESENT);
	break;
      case COMMAND_NEED:
	ival = run_command (command->name, command->name, command->pid);
	if (ival == 0)
	{
	    ++num_needers;
	    force_progress ();
	}
	else kill (command->pid, ival);
	break;
      case COMMAND_ROLLBACK:
	if (command->name[0] == '\0') script = NULL;
	else
	{
	    if ( ( script = find_script_byname (command->name, &available_list,
						NULL) ) == NULL )
	    {
		kill (command->pid, SIG_NOT_PRESENT);
		break;
	    }
	}
	while (script != available_list.first)
	{
	    pid_t pid;
	    struct script_struct *victim = available_list.first;
	    char txt[256];

	    if ( ( pid = fork () ) == 0 )   /*  Child   */
	    {
		for (ival = 1; ival < NSIG; ival++) signal (ival, SIG_DFL);
		open ("/dev/console", O_RDONLY, 0);
		open ("/dev/console", O_RDWR, 0);
		dup2 (1, 2);
		execlp (get_path (victim->first_service->name),
			victim->first_service->name, "stop", NULL);
		sprintf (txt, _("error stopping service: \"%s\""),
			 victim->first_service->name);
		err (txt);
		_exit (SIG_NOT_STOPPED);
	    }
	    else if (pid == -1) break;      /*  Error   */
	    else                            /*  Parent  */
	    {
		while (waitpid (pid, &ival, 0) != pid) /*  Nothing  */;
		if ( WIFEXITED (ival) && (WEXITSTATUS (ival) == 0) )
		{
		    sprintf (txt, "Stopped service: %s\n",
			     victim->first_service->name);
		    remove_entry (&available_list, victim);
		    free (victim);
		    err (txt);
		}
		else break;
	    }
	}
	kill (command->pid,
	      (script ==available_list.first) ? SIG_STOPPED : SIG_NOT_STOPPED);
	break;
      case COMMAND_DUMP_LIST:
	if (fork () == 0) /* Do it in a child process so pid=1 doesn't block */
	{
	    FILE *fp;

	    if ( ( fp = fopen (command->name, "w") ) == NULL ) _exit (1);
	    show_scripts (fp, available_list.first, "AVAILABLE");
	    show_scripts (fp, starting_list.first, "STARTING");
	    fputs ("UNAVAILABLE SERVICES:\n", fp);
	    for (service = unavailable_services; service != NULL;
		 service = service->next)
		fprintf (fp, "%s (%s)\n", service->name,
			 service->failed ? "FAILED" : "not configured");
	    fclose (fp);
	    _exit (0);
	}
	break;
      case COMMAND_PROVIDE:
	/*  Sanity check  */
	if ( ( script = find_script_bypid (command->ppid, &starting_list) )
	     == NULL )
	{
	    kill (command->pid, SIG_NOT_CHILD);
	    break;
	}
	if (find_script_byname (command->name, &available_list, NULL) != NULL)
	{
	    kill (command->pid, SIG_PRESENT);
	    break;
	}
	if (find_script_byname (command->name, &starting_list, &service)
	    != NULL)
	{   /*  Someone else is trying to provide  */
	    script->next_attempting_provider = service->attempting_providers;
	    service->attempting_providers = script;
	    break;
	}
	if ( ( service = find_service_in_list (command->name,
					       unavailable_services) )
	     == NULL )
	{   /*  We're the first to try and provide: create it  */
	    if ( ( service =
		   calloc (1, strlen (command->name) + sizeof *service) )
		 == NULL )
	    {
		kill (command->pid, SIG_NOT_CHILD);
		break;
	    }
	    strcpy (service->name, command->name);
	}
	else
	{   /*  Orphaned service: unhook and grab it  */
	    if (service->prev == NULL) unavailable_services = service->next;
	    else service->prev->next = service->next;
	    if (service->next != NULL) service->next->prev = service->prev;
	    service->next = NULL;
	}
	service->prev = script->last_service;
	script->last_service->next = service;
	script->last_service = service;
	kill (command->pid, SIG_NOT_PRESENT);
	break;
      case -1:
      default:
	break;
    }
}   /*  End Function process_command  */

static int run_command (const char *file, const char *name, pid_t pid)
{
    struct script_struct *script;
    struct needer_struct *needer = NULL;
    struct service_struct *service;

    if (find_script_byname (name, &available_list, NULL) != NULL)
	return SIG_PRESENT;
    if (pid != 0)
    {
	needer = calloc (1, sizeof *needer);
	if (needer == NULL) return SIG_FAILED;
	needer->pid = pid;
    }
    script = find_script_byname (name, &starting_list, &service);
    if (script == NULL)
	service = find_service_in_list (name, unavailable_services);
    if (service == NULL)
    {
	int i;
	char txt[1024];

	if ( ( script = calloc (1, sizeof *script) ) == NULL )
	{
	    if (needer != NULL) free (needer);
	    return SIG_FAILED;
	}
	service = calloc (1, strlen (name) + sizeof *service);
	if (service == NULL)
	{
	    free (script);
	    return SIG_FAILED;
	}
	strcpy (service->name, name);
	switch ( script->pid = fork () )
	{
	  case 0:   /*  Child   */
	    for (i = 1; i < NSIG; i++) signal (i, SIG_DFL);
	    execlp (get_path (file), service->name, "start", NULL);
	    sprintf (txt, "error running programme: \"%s\"\n", service->name);
	    err ( _(txt) );
	    _exit (SIG_FAILED);
	    break;
	  case -1:  /*  Error   */
	    service->next = unavailable_services;
	    if (unavailable_services != NULL)
		unavailable_services->prev = service;
	    unavailable_services = service;
	    free (script);
	    if (needer != NULL) free (needer);
	    return SIG_FAILED;
	    /*break;*/
	  default:  /*  Parent  */
	    script->first_service = service;
	    script->last_service = service;
	    insert_entry (&starting_list, script);
	    sched_yield ();
	    break;
	}
    }
    if (needer == NULL) return 0;
    needer->next = service->needers;
    service->needers = needer;
    return 0;
}   /*  End Function run_command  */

static struct service_struct *find_service_in_list (const char *name,
						    struct service_struct *sv)
{
    for (; sv != NULL; sv = sv->next)
	if (strcmp (sv->name, name) == 0) return (sv);
    return NULL;
}   /*  End Function find_service_in_list  */

static struct script_struct *find_script_byname (const char *name,
						 struct list_head *head,
						 struct service_struct **service)
{
    struct script_struct *script;

    for (script = head->first; script != NULL; script = script->next)
    {
	struct service_struct *sv;

	if ( ( sv = find_service_in_list (name, script->first_service) )
	     != NULL )
	{
	    if (service != NULL) *service = sv;
	    return (script);
	}
    }
    if (service != NULL) *service = NULL;
    return NULL;
}   /*  End Function find_script_byname  */

static struct script_struct *find_script_bypid (pid_t pid,
						struct list_head *head)
{
    struct script_struct *script;

    for (script = head->first; script != NULL; script = script->next)
	if (script->pid == pid) return (script);
    return NULL;
}   /*  End Function find_script_bypid  */

static void insert_entry (struct list_head *head, struct script_struct *entry)
{
    if (entry == NULL) return;
    entry->prev = NULL;
    entry->next = head->first;
    if (head->first != NULL) head->first->prev = entry;
    head->first = entry;
    if (head->last == NULL) head->last = entry;
    ++head->num_entries;
}   /*  End Function insert_entry  */

static void remove_entry (struct list_head *head, struct script_struct *entry)
{
    if (entry->prev == NULL) head->first = entry->next;
    else entry->prev->next = entry->next;
    if (entry->next == NULL) head->last = entry->prev;
    else entry->next->prev = entry->prev;
    --head->num_entries;
}   /*  End Function remove_entry  */

static void signal_needers (struct service_struct *service, int sig)
{
    struct needer_struct *needer, *next_needer;

    for (needer = service->needers; needer != NULL; needer = next_needer)
    {
	kill (needer->pid, sig);
	next_needer = needer->next;
	free (needer);
	--num_needers;
    }
    service->needers = NULL;
}   /*  End Function signal_needers  */

static void handle_nonworking (struct script_struct *script)
{
    struct service_struct *service, *next;

    for (service = script->first_service; service != NULL; service = next)
    {
	struct script_struct *provider = service->attempting_providers;

	next = service->next;
	if (provider == NULL)
	{
	    service->prev = NULL;
	    service->next = unavailable_services;
	    if (unavailable_services != NULL)
		unavailable_services->prev = service;
	    unavailable_services = service;
	    continue;
	}
	service->attempting_providers = provider->next_attempting_provider;
	provider->last_service->next = service;
	service->prev = provider->last_service;
	provider->last_service = service;
	service->next = NULL;
	kill (provider->pid, SIG_NOT_PRESENT);
    }
    free (script);
}   /*  End Function handle_nonworking  */

static int force_progress (void)
/*  [RETURNS] 0 if boot scripts are still running, else -1.
*/
{
    struct service_struct *service;

    if (starting_list.num_entries > num_needers) return 0;
    /*  No progress can be made: signal needers  */
    for (service = unavailable_services; service != NULL;
	 service = service->next)
	signal_needers (service,
			service->failed ? SIG_FAILED : SIG_NOT_PRESENT);
    return (starting_list.num_entries < 1) ? -1 : 0;
}   /*  End Function force_progress  */

static void show_scripts (FILE *fp, const struct script_struct *script,
			  const char *type)
{
    fprintf (fp, "%s SERVICES:\n", type);
    for (; script != NULL; script = script->next)
    {
	struct service_struct *service = script->first_service;

	fputs (service->name, fp);
	for (service = service->next; service != NULL; service = service->next)
	    fprintf (fp, "  (%s)", service->name);
	putc ('\n', fp);
    }
}   /*  End Function show_scripts  */

static const char *get_path (const char *file)
{
    char *p1, *p2;
    static char path[PATH_SIZE];

    if (file[0] == '/') return file;
    if (init_path[0] == '\0') return file;
    for (p1 = init_path; *p1 != '\0'; p1 = p2)
    {
	if ( ( p2 = strchr (p1, ':') ) == NULL )
	    p2 = p1 + strlen (p1);
	strncpy (path, p1, p2 - p1);
	path[p2 - p1] = '/';
	strcpy (path + (p2 - p1) + 1, file);
	if (*p2 == ':') ++p2;
	if (access (path, X_OK) == 0) return path;
    }
    return file;
}   /*  End Function get_path  */
