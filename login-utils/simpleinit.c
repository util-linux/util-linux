/* simpleinit.c - poe@daimi.aau.dk */
/* Version 1.21 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <utmp.h>
#ifdef SHADOW_PWD
#  include <shadow.h>
#endif
#include "my_crypt.h"
#include "pathnames.h"
#include "linux_reboot.h"

#define CMDSIZ 150	/* max size of a line in inittab */
#define NUMCMD 30	/* max number of lines in inittab */
#define NUMTOK 20	/* max number of tokens in inittab command */

#define RUN_RC
#define TZFILE "/etc/TZ"
char tzone[CMDSIZ];
/* #define DEBUGGING */

/* Define this if you want init to ignore the termcap field in inittab for
   console ttys. */
/* #define SPECIAL_CONSOLE_TERM */

#define ever (;;)

struct initline {
	pid_t	pid;
	char	tty[10];
	char	termcap[30];
	char	*toks[NUMTOK];
	char	line[CMDSIZ];
};

struct initline inittab[NUMCMD];
int numcmd;
int stopped = 0;	/* are we stopped */

int do_rc();
void spawn(), hup_handler(), read_inittab();
void tstp_handler(), int_handler(), set_tz(), write_wtmp();
int boot_single();

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

    err("Booting to single user mode.\n");
    if((pid = fork()) == 0) {
	/* the child */
	execl(_PATH_BSHELL, _PATH_BSHELL, NULL);
	err("exec of single user shell failed\n");
    } else if(pid > 0) {
	while(wait(&i) != pid) /* nothing */;
    } else if(pid < 0) {
	err("fork of single user shell failed\n");
    }
    unlink(_PATH_SINGLE);
}

int main(int argc, char *argv[])
{
	int 	vec,i;
	pid_t	pid;

#ifdef SET_TZ
	set_tz();
#endif
	signal(SIGTSTP, tstp_handler);
	signal(SIGINT, int_handler);

	/* 
	 * start up in single user mode if /etc/singleboot exists or if
	 * argv[1] is "single".
	 */
	if(boot_single(0, argc, argv)) enter_single();

#ifdef RUN_RC
	/*If we get a SIGTSTP before multi-user mode, do nothing*/
	while(stopped)	
		pause();
	if(do_rc() != 0 && boot_single(1, argc, argv) && !stopped)
		enter_single();
	while(stopped)	/*Also if /etc/rc fails & we get SIGTSTP*/
		pause();
#endif

	write_wtmp();	/* write boottime record */

	for(i = 0; i < NUMCMD; i++)
		inittab[i].pid = -1;

	read_inittab();

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

	for(i = 0; i < getdtablesize(); i++) close(i);

	for(i = 0; i < numcmd; i++)
		spawn(i);
	
	for ever {
		pid = wait(&vec);

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
 * return true if we should boot up in singleuser mode. If argv[i] is 
 * "single" or the file /etc/singleboot exists, then singleuser mode should
 * be entered. If /etc/securesingle exists ask for root password first.
 */
int boot_single(int singlearg, int argc, char *argv[])
{
	char *pass, *rootpass = NULL;
	struct passwd *pwd;
	int i;

	for(i = 1; i < argc; i++) {
	    if(argv[i] && !strcmp(argv[i], "single")) singlearg = 1;
	}

	if(access(_PATH_SINGLE, 04) == 0 || singlearg) {
		if(access(_PATH_SECURE, 04) == 0) {
			if((pwd = getpwnam("root")) || (pwd = getpwuid(0)))
			  rootpass = pwd->pw_passwd;
			else
			  return 1; /* a bad /etc/passwd should not lock out */

			for(i = 0; i < MAXTRIES; i++) {
				pass = getpass("Password: ");
				if(pass == NULL) continue;
				
				if(!strcmp(crypt(pass, rootpass), rootpass)) {
					return 1;
				}

				puts("\nWrong password.\n");
			}
		} else return 1;
	}
	return 0;
}

/*
 * run /etc/rc. The environment is passed to the script, so the RC environment
 * variable can be used to decide what to do. RC may be set from LILO.
 */
int do_rc()
{
	pid_t pid;
	int stat;
	
	if((pid = fork()) == 0) {
		/* the child */
		char *argv[2];

		argv[0] = _PATH_BSHELL;
		argv[1] = (char *)0;

		close(0);
		if(open(_PATH_RC, O_RDONLY, 0) == 0) {
			execv(_PATH_BSHELL, argv);
			err("exec rc failed\n");
			_exit(2);
		}
		err("open of rc file failed\n");
		_exit(1);
	} else if(pid > 0) {
		/* parent, wait till rc process dies before spawning */
		while(wait(&stat) != pid) /* nothing */;
	} else if(pid < 0) {
		err("fork of rc shell failed\n");
	}
	return WEXITSTATUS(stat);
}

void spawn(int i)
{
	pid_t pid;
	int j;
	
	if((pid = fork()) < 0) {
		inittab[i].pid = -1;
		err("fork failed\n");
		return;
	}
	if(pid) {
		/* this is the parent */
		inittab[i].pid = pid;
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
		err("exec failed\n");
		sleep(5);
		_exit(1);
	}
}

void read_inittab()
{
	FILE *f;
	char buf[CMDSIZ];
	int i,j,k;
	char *ptr, *getty;
#ifdef SPECIAL_CONSOLE_TERM
	char tty[50];
	struct stat stb;
#endif
	char *termenv, *getenv();
	
	termenv = getenv("TERM");	/* set by kernel */
	/* termenv = "vt100"; */
			
	if(!(f = fopen(_PATH_INITTAB, "r"))) {
		err("cannot open inittab\n");
		_exit(1);
	}

	i = 0;
	while(!feof(f) && i < NUMCMD - 2) {
		if(fgets(buf, CMDSIZ - 1, f) == 0) break;
		buf[CMDSIZ-1] = 0;

		for(k = 0; k < CMDSIZ && buf[k]; k++) {
			if(buf[k] == '#') { 
				buf[k] = 0; break; 
			}
		}

		if(buf[0] == 0 || buf[0] == '\n') continue;

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
			err("no TERM or cannot stat tty\n");
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
	/*
	 * After Linux 0.96b PL1, we get a SIGINT when
	 * the user presses Ctrl-Alt-Del...
	 */

	int pid;
	
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
