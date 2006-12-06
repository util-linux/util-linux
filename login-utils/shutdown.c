/* shutdown.c - shutdown a Linux system
 * Initially written by poe@daimi.aau.dk 
 * Currently maintained at ftp://ftp.daimi.aau.dk/pub/linux/poe/
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
#include <sys/param.h>
#include <termios.h>
#include <mntent.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <syslog.h>
#include <sys/resource.h>
#include "pathnames.h"

void usage(), int_handler(), write_user(struct utmp *);
void wall(), write_wtmp(), unmount_disks(), unmount_disks_ourselves();
void swap_off();

char	*prog;		/* name of the program */
int	opt_reboot;	/* true if -r option or reboot command */
int	timeout;	/* number of seconds to shutdown */
int	opt_quiet;	/* true if no message is wanted */
int	opt_fast;	/* true if fast boot */
char	message[90];	/* reason for shutdown if any... */
int	opt_single = 0; /* true is we want to boot singleuser */
char	*whom;		/* who is shutting the system down */

/* #define DEBUGGING */

#define WR(s) write(fd, s, strlen(s))

void
usage()
{
	fprintf(stderr,
		"Usage: shutdown [-h|-r] [-fqs] [now|hh:ss|+mins]\n");
	exit(1);
}

void 
int_handler()
{
	unlink(_PATH_NOLOGIN);
	signal(SIGINT, SIG_DFL);
	puts("Shutdown process aborted\n");
	exit(1);
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int c,i;	
	int fd;
	char *ptr;
	
#ifndef DEBUGGING
	if(geteuid()) {
		fprintf(stderr, "%s: Only root can shut a system down.\n", argv[0]);
		exit(1);
	}
#endif

	if(*argv[0] == '-') argv[0]++;	/* allow shutdown as login shell */
	prog = argv[0];
	if((ptr = strrchr(argv[0], '/'))) prog = ++ptr;
	
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
		if(argc > 1 && !strcmp(argv[1], "-s")) opt_single = 1;
	} else if(!strcmp("fastboot", prog)) {
		opt_reboot = 1;
		opt_quiet = 1;
		opt_fast = 1;
		timeout = 0;
		if(argc > 1 && !strcmp(argv[1], "-s")) opt_single = 1;
	} else {
		/* defaults */
		opt_reboot = 0;
		opt_quiet = 0;
		opt_fast = 0;
		timeout = 2*60;
		
		c = 0;
		while(++c < argc) {
			if(argv[c][0] == '-') {
			    for(i = 1; argv[c][i]; i++) {
				switch(argv[c][i]) {
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
			} else {
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
				    fprintf(stderr, "That must be tomorrow, can't you wait till then?\n");
				    exit(1);
				}
			}
		}
	}

	if(!opt_quiet) {
		/* now ask for message, gets() is insecure */
		int cnt = sizeof(message)-1;
		char *ptr;
		
		printf("Why? "); fflush(stdout);
		
		ptr = message;
		while(--cnt >= 0 && (*ptr = getchar()) && *ptr != '\n') { 
			ptr++;
		}
		*ptr = '\0';
	} else
		strcpy(message, "for maintenance; bounce, bounce");

#ifdef DEBUGGING
	printf("timeout = %d, quiet = %d, reboot = %d\n",
		timeout, opt_quiet, opt_reboot);
#endif
	
	/* so much for option-processing, now begin termination... */
	if(!(whom = getlogin())) whom = "ghost";

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

	
	if((fd = open(_PATH_NOLOGIN, O_WRONLY|O_CREAT)) >= 0) {
		WR("\r\nThe system is being shut down within 5 minutes\r\n");
		write(fd, message, strlen(message));
		WR("\r\nLogin is therefore prohibited.\r\n");
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
	syslog(LOG_NOTICE, "%s by %s: %s", 
	       opt_reboot ? "rebooted" : "halted", whom, message);
	closelog();

	if(opt_fast)
		if((fd = open("/fastboot", O_WRONLY|O_CREAT)) >= 0)
			close(fd);

	kill(1, SIGTSTP);	/* tell init not to spawn more getty's */
	write_wtmp();
	if(opt_single)
		close(open(_PATH_SINGLE, O_CREAT|O_WRONLY));
		
	sync();

	signal(SIGTERM, SIG_IGN);
	if(fork() > 0) sleep(1000); /* the parent will die soon... */
	setpgrp();		/* so the shell wont kill us in the fall */

#ifndef DEBUGGING
	/* a gentle kill of all other processes except init */
	kill(-1, SIGTERM);
	sleep(2);

	/* now use brute force... */
	kill(-1, SIGKILL);

	/* turn off accounting */
	acct(NULL);
#endif
	sync();
	sleep(2);

	/* remove swap files and partitions using swapoff */
	swap_off();

	/* unmount disks... */
	unmount_disks();
	sync();
	sleep(1);
	
	if(opt_reboot) {
		reboot(0xfee1dead, 672274793, 0x1234567);
	} else {
		printf("\nNow you can turn off the power...\n");
		/* allow C-A-D now, faith@cs.unc.edu */
		reboot(0xfee1dead, 672274793, 0x89abcdef);
	}
	/* NOTREACHED */
	exit(0); /* to quiet gcc */
}

/*** end of main() ***/

void
write_user(struct utmp *ut)
{
	int fd;
	int minutes, hours;
	char term[40] = {'/','d','e','v','/',0};
	char msg[100];

	minutes = timeout / 60;
	(void) strncat(term, ut->ut_line, sizeof(ut->ut_line));

	/* try not to get stuck on a mangled ut_line entry... */
	if((fd = open(term, O_RDWR|O_NONBLOCK)) < 0)
	        return;

	sprintf(msg, "\007\r\nURGENT: broadcast message from %s:\r\n", whom);
	WR(msg);

	if(minutes == 0) {
	    sprintf(msg, "System going down IMMEDIATELY!\r\n\n");
	} else if(minutes > 60) {
	    hours = minutes / 60;
	    sprintf(msg, "System going down in %d hour%s %d minutes\r\n",
		    hours, hours == 1 ? "" : "s", minutes - 60*hours);
	} else {
	    sprintf(msg, "System going down in %d minute%s\r\n\n",
		    minutes, minutes == 1 ? "" : "s");
	}
	WR(msg);

	sprintf(msg, "\t... %s ...\r\n\n", message);
	WR(msg);

	close(fd);
}

void
wall()
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
write_wtmp()
{
	/* write in wtmp that we are dying */
	int fd;
	struct utmp ut;
	
	memset((char *)&ut, 0, sizeof(ut));
	strcpy(ut.ut_line, "~");
	memcpy(ut.ut_name, "shutdown", sizeof(ut.ut_name));

	time(&ut.ut_time);
	ut.ut_type = BOOT_TIME;
	
	if((fd = open(_PATH_WTMP, O_WRONLY|O_APPEND)) >= 0) {
		write(fd, (char *)&ut, sizeof(ut));
		close(fd);
	}
}

void
swap_off()
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
		printf("Cannot fork for swapoff. Shrug!\n");
		return;
	}
	if (!pid) {
		execl("/sbin/swapoff", SWAPOFF_ARGS, NULL);
		execl("/etc/swapoff", SWAPOFF_ARGS, NULL);
		execl("/bin/swapoff", SWAPOFF_ARGS, NULL);
		execlp("swapoff", SWAPOFF_ARGS, NULL);
		puts("Cannot exec swapoff, hoping umount will do the trick.");
		exit(0);
	}
	while ((result = wait(&status)) != -1 && result != pid)
		;
}

void
unmount_disks()
{
	/* better to use umount directly because it may be smarter than us */

	int pid;
	int result;
	int status;

	sync();
	if ((pid = fork()) < 0) {
		printf("Cannot fork for umount, trying manually.\n");
		unmount_disks_ourselves();
		return;
	}
	if (!pid) {
		execl(_PATH_UMOUNT, UMOUNT_ARGS, NULL);
		printf("Cannot exec %s, trying umount.\n", _PATH_UMOUNT);
		execlp("umount", UMOUNT_ARGS, NULL);
		puts("Cannot exec umount, giving up on umount.");
		exit(0);
	}
	while ((result = wait(&status)) != -1 && result != pid)
		;
	puts("Unmounting any remaining filesystems...");
	unmount_disks_ourselves();
}

void
unmount_disks_ourselves()
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
		printf("shutdown: Cannot open %s.\n", _PATH_MTAB);
		return;
	}
	n = 0;
	while (n < 100 && (mnt = getmntent(mtab))) {
		mntlist[n++] = strdup(mnt->mnt_fsname[0] == '/' ?
			mnt->mnt_fsname : mnt->mnt_dir);
	}
	endmntent(mtab);

	/* we are careful to do this in reverse order of the mtab file */

	for (i = n - 1; i >= 0; i--) {
		filesys = mntlist[i];
#ifdef DEBUGGING
		printf("umount %s\n", filesys);
#else
		if (umount(mntlist[i]) < 0)
			printf("shutdown: Couldn't umount %s\n", filesys);
#endif
	}
}
