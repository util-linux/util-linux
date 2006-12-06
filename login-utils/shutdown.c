/* shutdown.c - shutdown a Linux system
 * Initially written by poe@daimi.aau.dk 
 * Currently maintained at ftp://ftp.daimi.aau.dk/pub/Software/Linux/
 */

/*
 * Modified by jrs@world.std.com to try to exec "umount -a" and if
 * that doesn't work, then umount filesystems ourselves in reverse
 * order.  The old-way was in forward order.  Also if the device
 * field of the mtab does not start with a "/" then give umount
 * the mount point instead.  This is needed for the nfs and proc
 * filesystems and yet is compatible with older systems.
 *
 * We also use the mntent library interface to read the mtab file
 * instead of trying to parse it directly and no longer give a
 * warning about not being able to umount the root.
 *
 * The reason "umount -a" should be tried first is because it may do
 * special processing for some filesystems (such as informing an
 * nfs server about nfs umounts) that we don't want to cope with here.
 */

/*
 * Various changes and additions to resemble SunOS 4 shutdown/reboot/halt(8)
 * more closely by Scott Telford (s.telford@ed.ac.uk) 93/05/18.
 * (I butchered Scotts patches somewhat. - poe)
 *
 * Changes by Richard Gooch <rgooch@atnf.csiro.au> (butchered by aeb)
 * introducing shutdown.conf.
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2000-03-02 Richard Gooch <rgooch@atnf.csiro.au>
 * - pause forever if (pid == 1) and send SIGQUIT to pid = 1
 *
 * 2000-11-04 Richard Gooch <rgooch@atnf.csiro.au>
 * - continue reaping if (pid == 1)
 *
 * 2000-11-06 Richard Gooch <rgooch@atnf.csiro.au>
 * - shut down "finalprog" from /etc/inittab
 * - kill normal user (non-root and non-daemon) processes first with SIGTERM
 *
 * 2000-11-08 Richard Gooch <rgooch@atnf.csiro.au>
 * - rollback services
 * - do not unmount devfs (otherwise get harmless but annoying messages)
 * - created syncwait() for faster shutting down
 * - kill getty processes
 * 2001-05-12 Richard Gooch <rgooch@atnf.csiro.au>
 * - unblock all signals (sigmask from simpleinit(8) stopped sleep(3))
 * - close all files
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <utmp.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/param.h>
#include <termios.h>
#include <mntent.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <syslog.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include "linux_reboot.h"
#include "pathnames.h"
#include "xstrncpy.h"
#include "nls.h"

static void usage(void), int_handler(int), write_user(struct utmp *);
static void wall(void), write_wtmp(void), unmount_disks(void);
static void unmount_disks_ourselves(void);
static void swap_off(void), do_halt(char *);
static void kill_mortals (int sig);
static void stop_finalprog (void);
static void syncwait (int timeval);


char	*prog;		/* name of the program */
int	opt_reboot;	/* true if -r option or reboot command */
int	timeout;	/* number of seconds to shutdown */
int	opt_quiet;	/* true if no message is wanted */
int	opt_fast;	/* true if fast boot */
char	message[90];	/* reason for shutdown if any... */
int	opt_single = 0; /* true is we want to boot singleuser */
char	*whom;		/* who is shutting the system down */
int	opt_msgset = 0; /* message set on command line */
			/* change 1 to 0 if no file is to be used by default */
int	opt_use_config_file = 1;	/* read _PATH_SHUTDOWN_CONF */
char	halt_action[256];		/* to find out what to do upon halt */

/* #define DEBUGGING */

#define WR(s) write(fd, s, strlen(s))
#define WRCRLF	write(fd, "\r\n", 2)
#define ERRSTRING strerror(errno)


void
usage(void)
{
	fprintf(stderr,
		_("Usage: shutdown [-h|-r] [-fqs] [now|hh:ss|+mins]\n"));
	exit(1);
}

static void
my_puts(char *s)
{
	/* Use a fresh stdout after forking */
	freopen(_PATH_CONSOLE, "w", stdout);
	puts(s);
	fflush(stdout);
}

void 
int_handler(int sig)
{
	unlink(_PATH_NOLOGIN);
	signal(SIGINT, SIG_DFL);
	my_puts(_("Shutdown process aborted"));
	exit(1);
}

static int
iswhitespace(int a) {
	return (a == ' ' || a == '\t');
}

int
main(int argc, char *argv[])
{
	int c, i, fd;
	char *ptr;

	i = getdtablesize ();
	for (fd = 3; fd < i; fd++) close (fd);
	if (getpid () == 1)
	{
	    for (fd = 0; fd < 3; fd++) close (fd);
	    while (1) wait (NULL);  /*  Grim reaper never stops  */
	}
	sigsetmask (0); /*  simpleinit(8) blocks all signals: undo for ALRM  */
	for (i = 1; i < NSIG; i++) signal (i, SIG_DFL);

        setlocale(LC_ALL, "");
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);

#ifndef DEBUGGING
	if(setreuid (0, 0)) {
		fprintf(stderr, _("%s: Only root can shut a system down.\n"),
			argv[0]);
		exit(1);
	}
#endif

	if(*argv[0] == '-') argv[0]++;	/* allow shutdown as login shell */
	prog = argv[0];
	if((ptr = strrchr(argv[0], '/'))) prog = ++ptr;

	/* All names (halt, reboot, fasthalt, fastboot, shutdown)
	   refer to the same program with the same options,
	   only the defaults differ. */
	if(!strcmp("halt", prog)) {
		opt_reboot = 0;
		opt_quiet = 1;
		opt_fast = 0;
		timeout = 0;
	} else if(!strcmp("fasthalt", prog)) {
		opt_reboot = 0;
		opt_quiet = 1;
		opt_fast = 1;
		timeout = 0;
	} else if(!strcmp("reboot", prog)) {
		opt_reboot = 1;
		opt_quiet = 1;
		opt_fast = 0;
		timeout = 0;
	} else if(!strcmp("fastboot", prog)) {
		opt_reboot = 1;
		opt_quiet = 1;
		opt_fast = 1;
		timeout = 0;
	} else {
		/* defaults */
		opt_reboot = 0;
		opt_quiet = 0;
		opt_fast = 0;
		timeout = 2*60;
	}
		
	c = 0;
	while(++c < argc) {
		if(argv[c][0] == '-') {
			for(i = 1; argv[c][i]; i++) {
				switch(argv[c][i]) {
				case 'C':
					opt_use_config_file = 1;
					break;
				case 'h': 
					opt_reboot = 0;
					break;
				case 'r':
					opt_reboot = 1;
					break;
				case 'f':
					opt_fast = 1;
					break;
				case 'q':
					opt_quiet = 1;
					break;
				case 's':
					opt_single = 1;
					break;
				    
				default:
					usage();
				}
			}
		} else if(!strcmp("now", argv[c])) {
			timeout = 0;
		} else if(argv[c][0] == '+') {
			timeout = 60 * atoi(&argv[c][1]);
		} else if (isdigit(argv[c][0])) {
			char *colon;
			int hour = 0;
			int minute = 0;
			time_t tics;
			struct tm *tt;
			int now, then;
				
			if((colon = strchr(argv[c], ':'))) {
				*colon = '\0';
				hour = atoi(argv[c]);
				minute = atoi(++colon);
			} else usage();
				
			(void) time(&tics);
			tt = localtime(&tics);
				
			now = 3600 * tt->tm_hour + 60 * tt->tm_min;
			then = 3600 * hour + 60 * minute;
			timeout = then - now;
			if(timeout < 0) {
				fprintf(stderr, _("That must be tomorrow, "
					          "can't you wait till then?\n"));
				exit(1);
			}
		} else {
			xstrncpy(message, argv[c], sizeof(message));
			opt_msgset = 1;
		}
	}

	halt_action[0] = 0;

	/* No doubt we shall want to extend this some day
	   and register a series of commands to be executed
	   at various points during the shutdown sequence,
	   and to define the number of milliseconds to sleep, etc. */
	if (opt_use_config_file) {
		char line[256], *p;
		FILE *fp;

		/*  Read and parse the config file */
		halt_action[0] = '\0';
		if ((fp = fopen (_PATH_SHUTDOWN_CONF, "r")) != NULL) {
			if (fgets (line, sizeof(line), fp) != NULL &&
			    strncasecmp (line, "HALT_ACTION", 11) == 0 &&
			    iswhitespace(line[11])) {
				p = index(line, '\n');
				if (p)
					*p = 0;		/* strip final '\n' */
				p = line+11;
				while(iswhitespace(*p))
					p++;
				strcpy(halt_action, p);
			}
			fclose (fp);
		}
	}

	if(!opt_quiet && !opt_msgset) {
		/* now ask for message, gets() is insecure */
		int cnt = sizeof(message)-1;
		char *ptr;
		
		printf("Why? "); fflush(stdout);
		
		ptr = message;
		while(--cnt >= 0 && (*ptr = getchar()) && *ptr != '\n') { 
			ptr++;
		}
		*ptr = '\0';
	} else if (!opt_msgset) {
		strcpy(message, _("for maintenance; bounce, bounce"));
	}

#ifdef DEBUGGING
	printf(_("timeout = %d, quiet = %d, reboot = %d\n"),
		timeout, opt_quiet, opt_reboot);
#endif
	
	/* so much for option-processing, now begin termination... */
	if(!(whom = getlogin()) || !*whom) whom = "ghost";
	if(strlen(whom) > 40) whom[40] = 0; /* see write_user() */

	setpriority(PRIO_PROCESS, 0, PRIO_MIN);
	signal(SIGINT,  int_handler);
	signal(SIGHUP,  int_handler);
	signal(SIGQUIT, int_handler);
	signal(SIGTERM, int_handler);

	chdir("/");

	if(timeout > 5*60) {
		sleep(timeout - 5*60);
		timeout = 5*60;
	}

	
	if((fd = open(_PATH_NOLOGIN, O_WRONLY|O_CREAT, 0644)) >= 0) {
		/* keep xgettext happy and leave \r\n outside strings */
		WRCRLF;
		WR(_("The system is being shut down within 5 minutes"));
		WRCRLF;
		write(fd, message, strlen(message));
		WRCRLF;
		WR(_("Login is therefore prohibited."));
		WRCRLF;
		close(fd);
	}
	
	signal(SIGPIPE, SIG_IGN);

	if(timeout > 0) {
		wall();
		sleep(timeout);
	}

	timeout = 0;
	wall();
	sleep(3);

	/* now there's no turning back... */
	signal(SIGINT,  SIG_IGN);

	/* do syslog message... */
	openlog(prog, LOG_CONS, LOG_AUTH);
	if (opt_reboot)
		syslog(LOG_NOTICE, _("rebooted by %s: %s"), 
		       whom, message);
	else
		syslog(LOG_NOTICE, _("halted by %s: %s"), 
		       whom, message);
	closelog();

	if(opt_fast)
		if((fd = open("/fastboot", O_WRONLY|O_CREAT, 0644)) >= 0)
			close(fd);

	kill(1, SIGTSTP);	/* tell init not to spawn more getty's */
	write_wtmp();
	if(opt_single)
		if((fd = open(_PATH_SINGLE, O_CREAT|O_WRONLY, 0644)) >= 0)
			close(fd);
		
	sync();

	signal(SIGTERM, SIG_IGN);
	if(fork() > 0) sleep(1000); /* the parent will die soon... */
	setpgrp();		/* so the shell wont kill us in the fall */

#ifndef DEBUGGING
	/* a gentle kill of all other processes except init */
	kill_mortals (SIGTERM);
	for (fd = 0; fd < 3; fd++) close (fd);
	stop_finalprog ();
	sleep (1);                    /*  Time for saves to start           */
	kill (1, SIGTERM);            /*  Tell init to kill spawned gettys  */
	usleep (100000);              /*  Wait for gettys to die            */
	my_puts ("");                 /*  Get past the login prompt         */
	system ("/sbin/initctl -r");  /*  Roll back services                */
	syncwait (1);
	my_puts ("Sending SIGTERM to all remaining processes...");
	kill (-1, SIGTERM);
	sleep (2);                    /*  Default 2, some people need 5     */

	kill (-1, SIGKILL);           /*  Now use brute force...            */

	/* turn off accounting */
	acct(NULL);
#endif
	/* RedHat and SuSE like to remove /etc/nologin.
	   Perhaps the usual sequence is
	      touch nologin; shutdown -h; fiddle with hardware;
	      boot; fiddle with software; rm nologin
	   and removing it here will be counterproductive.
	   Let us see whether people complain. */
	unlink(_PATH_NOLOGIN);

	/*  Tell init(8) to exec so that the old inode may be freed cleanly if
	    required. Need to sleep before remounting root read-only  */
	kill (1, SIGQUIT);

	sleep (1);	/* Time for processes to die and close files */
	syncwait (2);

	/* remove swap files and partitions using swapoff */
	swap_off();

	/* unmount disks... */
	unmount_disks();
	syncwait (1);

	if(opt_reboot) {
		my_reboot(LINUX_REBOOT_CMD_RESTART); /* RB_AUTOBOOT */
		my_puts(_("\nWhy am I still alive after reboot?"));
	} else {
		my_puts(_("\nNow you can turn off the power..."));

		/* allow C-A-D now, faith@cs.unc.edu, re-fixed 8-Jul-96 */
		my_reboot(LINUX_REBOOT_CMD_CAD_ON); /* RB_ENABLE_CAD */
		sleep (1);  /*  Wait for devices to finish writing to media  */
		do_halt(halt_action);
	}
	/* NOTREACHED */
	exit(0); /* to quiet gcc */
}

/*** end of main() ***/

void
do_halt(char *action) {
	if (strcasecmp (action, "power_off") == 0) {
		printf(_("Calling kernel power-off facility...\n"));
		fflush(stdout);
		my_reboot(LINUX_REBOOT_CMD_POWER_OFF);
		printf(_("Error powering off\t%s\n"), ERRSTRING);
		fflush(stdout);
		sleep (2);
	} else

	/* This should be improved; e.g. Mike Jagdis wants "/sbin/mdstop -a" */
	/* Maybe we should also fork and wait */
	if (action[0] == '/') {
		printf(_("Executing the program \"%s\" ...\n"), action);
		fflush(stdout);
		execl(action, action, NULL);
		printf(_("Error executing\t%s\n"), ERRSTRING);
		fflush(stdout);
		sleep (2);
	}

	my_reboot(LINUX_REBOOT_CMD_HALT); /* RB_HALT_SYSTEM */
}

void
write_user(struct utmp *ut)
{
	int fd;
	int minutes, hours;
	char term[40] = {'/','d','e','v','/',0};
	char msg[100];

	minutes = timeout / 60;
	hours = minutes / 60;
	minutes %= 60;

	(void) strncat(term, ut->ut_line, sizeof(ut->ut_line));

	/* try not to get stuck on a mangled ut_line entry... */
	if((fd = open(term, O_WRONLY|O_NONBLOCK)) < 0)
	        return;

	msg[0] = '\007';	/* gettext crashes on \a */
	sprintf(msg+1, _("URGENT: broadcast message from %s:"), whom);
	WRCRLF;
	WR(msg);
	WRCRLF;

	if (hours > 1)
		sprintf(msg, _("System going down in %d hours %d minutes"),
			hours, minutes);
	else if (hours == 1)
		sprintf(msg, _("System going down in 1 hour %d minutes"),
			minutes);
	else if (minutes > 1)
		sprintf(msg, _("System going down in %d minutes\n"),
			minutes);
	else if (minutes == 1)
		sprintf(msg, _("System going down in 1 minute\n"));
	else
		sprintf(msg, _("System going down IMMEDIATELY!\n"));

	WR(msg);
	WRCRLF;

	sprintf(msg, _("\t... %s ...\n"), message);
	WR(msg);
	WRCRLF;

	close(fd);
}

void
wall(void)
{
	/* write to all users, that the system is going down. */
	struct utmp *ut;
		
	utmpname(_PATH_UTMP);
	setutent();
	
	while((ut = getutent())) {
		if(ut->ut_type == USER_PROCESS)
			write_user(ut);
	}
	endutent();
}

void
write_wtmp(void)
{
	/* write in wtmp that we are dying */
	int fd;
	struct utmp ut;
	
	memset((char *)&ut, 0, sizeof(ut));
	strcpy(ut.ut_line, "~");
	memcpy(ut.ut_name, "shutdown", sizeof(ut.ut_name));

	time(&ut.ut_time);
	ut.ut_type = BOOT_TIME;
	
	if((fd = open(_PATH_WTMP, O_WRONLY|O_APPEND, 0644)) >= 0) {
		write(fd, (char *)&ut, sizeof(ut));
		close(fd);
	}
}

void
swap_off(void)
{
	/* swapoff esp. swap FILES so the underlying partition can be
	   unmounted. It you don't have swapoff(1) or use mount to
	   add swapspace, this may not be necessary, but I guess it
	   won't hurt */

	int pid;
	int result;
	int status;

	sync();
	if ((pid = fork()) < 0) {
		my_puts(_("Cannot fork for swapoff. Shrug!"));
		return;
	}
	if (!pid) {
		execl("/sbin/swapoff", SWAPOFF_ARGS, NULL);
		execl("/etc/swapoff", SWAPOFF_ARGS, NULL);
		execl("/bin/swapoff", SWAPOFF_ARGS, NULL);
		execlp("swapoff", SWAPOFF_ARGS, NULL);
		my_puts(_("Cannot exec swapoff, "
			  "hoping umount will do the trick."));
		exit(0);
	}
	while ((result = wait(&status)) != -1 && result != pid)
		;
}

void
unmount_disks(void)
{
	/* better to use umount directly because it may be smarter than us */

	int pid;
	int result;
	int status;

	sync();
	if ((pid = fork()) < 0) {
		my_puts(_("Cannot fork for umount, trying manually."));
		unmount_disks_ourselves();
		return;
	}
	if (!pid) {
		execl(_PATH_UMOUNT, UMOUNT_ARGS, NULL);

		/* need my_printf instead of my_puts here */
		freopen(_PATH_CONSOLE, "w", stdout);
		printf(_("Cannot exec %s, trying umount.\n"), _PATH_UMOUNT);
		fflush(stdout);

		execlp("umount", UMOUNT_ARGS, NULL);
		my_puts(_("Cannot exec umount, giving up on umount."));
		exit(0);
	}
	while ((result = wait(&status)) != -1 && result != pid)
		;
	my_puts(_("Unmounting any remaining filesystems..."));
	unmount_disks_ourselves();
}

void
unmount_disks_ourselves(void)
{
	/* unmount all disks */

	FILE *mtab;
	struct mntent *mnt;
	char *mntlist[128];
	int i;
	int n;
	char *filesys;
	
	sync();
	if (!(mtab = setmntent(_PATH_MTAB, "r"))) {
		my_puts("shutdown: Cannot open " _PATH_MTAB ".");
		return;
	}
	n = 0;
	while (n < 100 && (mnt = getmntent(mtab))) {
		/*
		 * Neil Phillips: trying to unmount temporary / kernel
		 * filesystems is pointless and may cause error messages;
		 * /dev can be a ramfs managed by udev.
		 */
		if (strcmp(mnt->mnt_type, "devfs") == 0 ||
		    strcmp(mnt->mnt_type, "proc") == 0 ||
		    strcmp(mnt->mnt_type, "sysfs") == 0 ||
		    strcmp(mnt->mnt_type, "ramfs") == 0 ||
		    strcmp(mnt->mnt_type, "tmpfs") == 0 ||
		    strcmp(mnt->mnt_type, "devpts") == 0)
			continue;
		mntlist[n++] = strdup(mnt->mnt_dir);
	}
	endmntent(mtab);

	/* we are careful to do this in reverse order of the mtab file */

	for (i = n - 1; i >= 0; i--) {
		filesys = mntlist[i];
#ifdef DEBUGGING
		printf("umount %s\n", filesys);
#else
		if (umount(mntlist[i]) < 0)
			printf(_("shutdown: Couldn't umount %s: %s\n"),
			       filesys, ERRSTRING);
#endif
	}
}

static void kill_mortals (int sig)
{
    int npids = 0;
    int index = 0;
    int pid;
    struct stat statbuf;
    DIR *dp;
    struct dirent *de;
    pid_t *pids = NULL;
    char path[256];

    if ( ( dp = opendir ("/proc") ) == NULL ) return;
    while ( ( de = readdir (dp) ) != NULL )
    {
	if ( !isdigit (de->d_name[0]) ) continue;
	pid = atoi (de->d_name);
	sprintf (path, "/proc/%d", pid);
	if (stat (path, &statbuf) != 0) continue;
	if (statbuf.st_uid < 100) continue;
	if (index <= npids)
	{
	    pids = realloc (pids, npids + 16384);
	    if (pids == NULL) return;
	    npids += 16384;
	}
	pids[index++] = pid;
    }
    fputs ("Sending SIGTERM to mortals...", stderr);
    for (--index; index >= 0; --index) kill (pids[index], sig);
    free (pids);
    closedir (dp);
}   /*  End Function kill_mortals  */

static void stop_finalprog (void)
{
    char *p1, *p2;
    FILE *fp;
    char line[256];

    if ( ( fp = fopen (_PATH_INITTAB, "r") ) == NULL ) return;
    while (fgets (line, 256, fp) != NULL)
    {
	pid_t pid;

	line[strlen (line) - 1] = '\0';
	p1 = line;
	while ( isspace (*p1) ) ++p1;
	if (strncmp (p1, "finalprog", 9) != 0) continue;
	if ( ( p1 = strchr (p1 + 9, '=') ) == NULL ) continue;
	for (++p1; isspace (*p1); ++p1);
	if (*p1 == '\0') continue;
	for (p2 = p1; !isspace (*p2); ++p2);
	*p2 = '\0';
	switch ( pid = fork () )
	{
	  case 0:   /*  Child   */
	    execl (p1, p1, "stop", NULL);
	    break;
	  case -1:  /*  Error   */
	    break;
	  default:  /*  Parent  */
	    waitpid (pid, NULL, 0);
	    break;
	}
	fclose (fp);
	return;
    }
    fclose (fp);
}   /*  End Function stop_finalprog  */

static void syncwait (int timeval)
{
    static int do_wait = 0;
    static int first_time = 1;

    sync ();
    /*  Kernel version 1.3.20 and after are supposed to wait automatically  */
    if (first_time)
    {
	struct utsname uts;

	first_time = 0;
	uname (&uts);
	if (uts.release[0] < '2') do_wait = 1;
    }
    if (do_wait) sleep (timeval);
}   /*  End Function syncwait  */
