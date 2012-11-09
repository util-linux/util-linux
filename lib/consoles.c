/*
 * consoles.c	    Routines to detect the system consoles
 *
 * Copyright (c) 2011 SuSE LINUX Products GmbH, All rights reserved.
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Author: Werner Fink <werner@suse.de>
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef __linux__
#  include <sys/vt.h>
#  include <sys/kd.h>
#  include <linux/serial.h>
#endif
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include "c.h"
#include "canonicalize.h"
#include "consoles.h"

#ifdef __linux__
# include <linux/major.h>
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
# ifndef  typeof
#  define typeof		__typeof__
# endif
# ifndef  restrict
#  define restrict		__restrict__
# endif
#endif

#define alignof(type)		((sizeof(type)+(sizeof(void*)-1)) & ~(sizeof(void*)-1))

/*
 * Read and allocate one line from file,
 * the caller has to free the result
 */
static __attribute__((__nonnull__))
char *oneline(const char *file)
{
	FILE *fp;
	char *ret = NULL;
	size_t len = 0;

	if (!(fp = fopen(file, "re")))
		return NULL;
	if (getline(&ret, &len, fp) >= 0) {
		char *nl;

		if (len)
			ret[len-1] = '\0';
		if ((nl = strchr(ret, '\n')))
			*nl = '\0';
	}

	fclose(fp);
	return ret;
}

#ifdef __linux__
/*
 *  Read and determine active attribute for tty below
 *  /sys/class/tty, the caller has to free the result.
 */
static __attribute__((__malloc__))
char *actattr(const char *tty)
{
	char *ret, *path;

	if (!tty || !*tty)
		return NULL;
	if (asprintf(&path, "/sys/class/tty/%s/active", tty) < 0)
		return NULL;

	ret = oneline(path);
	free(path);
	return ret;
}

/*
 * Read and determine device attribute for tty below
 * /sys/class/tty.
 */
static
dev_t devattr(const char *tty)
{
	dev_t dev = 0;
	char *path, *value;

	if (!tty || !*tty)
		return 0;
	if (asprintf(&path, "/sys/class/tty/%s/dev", tty) < 0)
		return 0;

	value = oneline(path);
	if (value) {
		unsigned int maj, min;

		if (sscanf(value, "%u:%u", &maj, &min) == 2)
			dev = makedev(maj, min);
		free(value);
	}

	free(path);
	return dev;
}
#endif /* __linux__ */

/*
 * Search below /dev for the characer device in `dev_t comparedev' variable.
 */
static
#ifdef __GNUC__
__attribute__((__nonnull__,__malloc__,__hot__))
#endif
char* scandev(DIR *dir, dev_t comparedev)
{
	char *name = NULL;
	struct dirent *dent;
	int fd;

	fd = dirfd(dir);
	rewinddir(dir);
	while ((dent = readdir(dir))) {
		char path[PATH_MAX];
		struct stat st;
		if (fstatat(fd, dent->d_name, &st, 0) < 0)
			continue;
		if (!S_ISCHR(st.st_mode))
			continue;
		if (comparedev != st.st_rdev)
			continue;
		if ((size_t)snprintf(path, sizeof(path), "/dev/%s", dent->d_name) >= sizeof(path))
			continue;
		name = canonicalize_path(path);
		break;
	}

	return name;
}

/*
 * Default control characters for an unknown terminal line.
 */

/*
 * Allocate an aligned `struct console' memory area,
 * initialize its default values, and append it to
 * the global linked list.
 */
static
#ifdef __GNUC__
__attribute__((__nonnull__,__hot__))
#endif
int append_console(struct console **list, char * name)
{
	static const struct chardata initcp = {
		.erase	= CERASE,
		.kill	= CKILL,
		.eol	= CTRL('r'),
		.parity = 0
	};
	struct console *restrict tail;
	struct console *last;

	if (posix_memalign((void*)&tail, sizeof(void*), alignof(typeof(struct console))) != 0)
		return -ENOMEM;

	for (last = *list; last && last->next; last = last->next);

	tail->next = NULL;
	tail->tty = name;

	tail->file = (FILE*)0;
	tail->flags = 0;
	tail->fd = -1;
	tail->id = last ? last->id + 1 : 0;
	tail->pid = 0;
	memset(&tail->tio, 0, sizeof(tail->tio));
	memcpy(&tail->cp, &initcp, sizeof(struct chardata));

	if (!last)
		*list = tail;
	else
		last->next = tail;

	return 0;
}

#ifdef __linux__
/*
 * return codes:
 *	< 0	- fatal error (no mem or so... )
 *	  0	- success
 *	  1	- recoverable error
 *	  2	- detection not available
 */
static int detect_consoles_from_proc(struct console **consoles)
{
	char fbuf[16 + 1];
	DIR *dir = NULL;
	FILE *fc = NULL;
	int maj, min, rc = 1;

	fc = fopen("/proc/consoles", "re");
	if (!fc)
		return 2;

	dir = opendir("/dev");
	if (!dir)
		goto done;

	while (fscanf(fc, "%*s %*s (%16[^)]) %d:%d", fbuf, &maj, &min) == 3) {
		char *name;
		dev_t comparedev;

		if (!strchr(fbuf, 'E'))
			continue;
		comparedev = makedev(maj, min);
		name = scandev(dir, comparedev);
		if (!name)
			continue;
		rc = append_console(consoles, name);
		if (rc < 0)
			goto done;
	}

	rc = *consoles ? 0 : 1;
done:
	if (dir)
		closedir(dir);
	if (fc)
		fclose(fc);
	return rc;
}
#endif /* __linux__ */

/*
 * Try to detect the real device(s) used for the system console
 * /dev/console if but only if /dev/console is used.  On Linux
 * this can be more than one device, e.g. a serial line as well
 * as a virtual console as well as a simple printer.
 *
 * Returns 1 if stdout and stderr should be reconnected and 0
 * otherwise or less than zero on error.
 */
int detect_consoles(const char *device, int fallback, struct console **consoles)
{
	int fd, reconnect = 0, rc;
	dev_t comparedev = 0;
#ifdef __linux__
	char *attrib, *cmdline;
#endif
	if (!device || !*device)
		fd = dup(fallback);
	else {
		fd = open(device, O_RDWR|O_NONBLOCK|O_NOCTTY|O_CLOEXEC);
		reconnect = 1;
	}

	if (fd >= 0) {
		DIR *dir;
		char *name;
		struct stat st;
#ifdef TIOCGDEV
		unsigned int devnum;
#endif

		if (fstat(fd, &st) < 0) {
			close(fd);
			goto fallback;
		}
		comparedev = st.st_rdev;

		if (reconnect &&
		    (fstat(fallback, &st) < 0 || comparedev != st.st_rdev))
			dup2(fd, fallback);
#ifdef __linux__
		/*
		 * Check if the device detection for Linux system console should be used.
		 */
		if (comparedev == makedev(TTYAUX_MAJOR, 0)) {	/* /dev/tty	*/
			close(fd);
			device = "/dev/tty";
			goto fallback;
		}
		if (comparedev == makedev(TTYAUX_MAJOR, 1)) {	/* /dev/console */
			close(fd);
			goto console;
		}
		if (comparedev == makedev(TTYAUX_MAJOR, 2)) {	/* /dev/ptmx	*/
			close(fd);
			device = "/dev/tty";
			goto fallback;
		}
		if (comparedev == makedev(TTY_MAJOR, 0)) {	/* /dev/tty0	*/
			struct vt_stat vt;
			if (ioctl(fd, VT_GETSTATE, &vt) < 0) {
				close(fd);
				goto fallback;
			}
			comparedev = makedev(TTY_MAJOR, (int)vt.v_active);
		}
#endif
#ifdef TIOCGDEV
		if (ioctl (fd, TIOCGDEV, &devnum) < 0) {
			close(fd);
			goto fallback;
		}
		comparedev = (dev_t)devnum;
#endif
		close(fd);
		dir = opendir("/dev");
		if (!dir)
			goto fallback;
		name = scandev(dir, comparedev);
		if (name) {
			rc = append_console(consoles, name);
			if (rc < 0)
				return rc;
		}
		closedir(dir);
		if (!*consoles)
			goto fallback;
		return reconnect;
	}
#ifdef __linux__
console:
	/*
	 * Detection of devices used for Linux system consolei using
	 * the /proc/consoles API with kernel 2.6.38 and higher.
	 */
	rc = detect_consoles_from_proc(consoles);
	if (rc == 0)
		return reconnect;	/* success */
	if (rc < 0)
		return rc;		/* fatal error */
	if (rc == 1)
		goto fallback;		/* detection error */

	/*
	 * Detection of devices used for Linux system console using
	 * the sysfs /sys/class/tty/ API with kernel 2.6.37 and higher.
	 */
	if ((attrib = actattr("console"))) {
		char *words = attrib, *token;
		DIR *dir;

		dir = opendir("/dev");
		if (!dir) {
			free(attrib);
			goto fallback;
		}
		while ((token = strsep(&words, " \t\r\n"))) {
			char * name;

			if (*token == '\0')
				continue;
			comparedev = devattr(token);
			if (comparedev == makedev(TTY_MAJOR, 0)) {
				char *tmp = actattr(token);
				if (!tmp)
					continue;
				comparedev = devattr(tmp);
				free(tmp);
			}

			name = scandev(dir, comparedev);
			if (!name)
				continue;
			rc = append_console(consoles, name);
			if (rc < 0)
				return rc;
		}
		closedir(dir);
		free(attrib);
		if (!*consoles)
			goto fallback;
		return reconnect;

	}
	/*
	 * Detection of devices used for Linux system console using
	 * kernel parameter on the kernels command line.
	 */
	if ((cmdline = oneline("/proc/cmdline"))) {
		char *words= cmdline, *token;
		DIR *dir;

		dir = opendir("/dev");
		if (!dir) {
			free(cmdline);
			goto fallback;
		}
		while ((token = strsep(&words, " \t\r\n"))) {
#ifdef TIOCGDEV
			unsigned int devnum;
#else
			struct vt_stat vt;
			struct stat st;
#endif
			char *colon, *name;

			if (*token != 'c')
				continue;

			if (strncmp(token, "console=", 8) != 0)
				continue;
			token += 8;

			if (strcmp(token, "brl") == 0)
				token += 4;
			if ((colon = strchr(token, ',')))
				*colon = '\0';

			if (asprintf(&name, "/dev/%s", token) < 0)
				continue;

			if ((fd = open(name, O_RDWR|O_NONBLOCK|O_NOCTTY|O_CLOEXEC)) < 0) {
				free(name);
				continue;
			}
			free(name);
#ifdef TIOCGDEV
			if (ioctl (fd, TIOCGDEV, &devnum) < 0) {
				close(fd);
				continue;
			}
			comparedev = (dev_t)devnum;
#else
			if (fstat(fd, &st) < 0) {
				close(fd);
				continue;
			}
			comparedev = st.st_rdev;
			if (comparedev == makedev(TTY_MAJOR, 0)) {
				if (ioctl(fd, VT_GETSTATE, &vt) < 0) {
					close(fd);
					continue;
				}
				comparedev = makedev(TTY_MAJOR, (int)vt.v_active);
			}
#endif
			close(fd);

			name = scandev(dir, comparedev);
			if (!name)
				continue;
			rc = append_console(consoles, name);
			if (rc < 0)
				return rc;
		}
		closedir(dir);
		free(cmdline);
		/*
		 * Detection of the device used for Linux system console using
		 * the ioctl TIOCGDEV if available (e.g. official 2.6.38).
		 */
		if (!*consoles) {
#ifdef TIOCGDEV
			unsigned int devnum;
			const char *name;

			if (!device || *device == '\0')
				fd = dup(fallback);
			else	fd = open(device, O_RDWR|O_NONBLOCK|O_NOCTTY|O_CLOEXEC);

			if (fd < 0)
				goto fallback;

			if (ioctl (fd, TIOCGDEV, &devnum) < 0) {
				close(fd);
				goto fallback;
			}
			comparedev = (dev_t)devnum;
			close(fd);

			if (device && *device != '\0')
				name = device;
			else	name = ttyname(fallback);

			if (!name)
				name = "/dev/tty1";

			rc = append_console(consoles, strdup(name));
			if (rc < 0)
				return rc;
			if (*consoles) {
				if (!device || *device == '\0')
					(*consoles)->fd = fallback;
				return reconnect;
			}
#endif
			goto fallback;
		}
		return reconnect;
	}
#endif /* __linux __ */
fallback:
	if (fallback >= 0) {
		const char *name;

		if (device && *device != '\0')
			name = device;
		else	name = ttyname(fallback);

		if (!name)
			name = "/dev/tty";

		rc = append_console(consoles, strdup(name));
		if (rc < 0)
			return rc;
		if (*consoles)
			(*consoles)->fd = fallback;
	}
	return reconnect;
}


#ifdef TEST_PROGRAM
int main(int argc, char *argv[])
{
	char *name = NULL;
	int fd, re;
	struct console *p, *consoles = NULL;

	if (argc == 2) {
		name = argv[1];
		fd = open(name, O_RDWR);
	} else {
		name = ttyname(STDIN_FILENO);
		fd = STDIN_FILENO;
	}

	if (!name)
		errx(EXIT_FAILURE, "usage: %s [<tty>]\n", program_invocation_short_name);

	re = detect_consoles(name, fd, &consoles);

	for (p = consoles; p; p = p->next)
		printf("%s: id=%d %s\n", p->tty, p->id, re ? "(reconnect) " : "");

	return 0;
}
#endif
