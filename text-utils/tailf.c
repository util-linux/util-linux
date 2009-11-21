/* tailf.c -- tail a log file and then follow it
 * Created: Tue Jan  9 15:49:21 1996 by faith@acm.org
 * Copyright 1996, 2003 Rickard E. Faith (faith@acm.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * less -F and tail -f cause a disk access every five seconds.  This
 * program avoids this problem by waiting for the file size to change.
 * Hence, the file is not accessed, and the access time does not need to be
 * flushed back to disk.  This is sort of a "stealth" tail.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#ifdef HAVE_INOTIFY_INIT
#include <sys/inotify.h>
#endif
#include "nls.h"
#include "usleep.h"

#define DEFAULT_LINES  10

static void
tailf(const char *filename, int lines)
{
	char *buf, *p;
	int  head = 0;
	int  tail = 0;
	FILE *str;
	int  i;

	if (!(str = fopen(filename, "r")))
		err(EXIT_FAILURE, _("cannot open \"%s\" for read"), filename);

	buf = malloc(lines * BUFSIZ);
	p = buf;
	while (fgets(p, BUFSIZ, str)) {
		if (++tail >= lines) {
			tail = 0;
			head = 1;
		}
		p = buf + (tail * BUFSIZ);
	}

	if (head) {
		for (i = tail; i < lines; i++)
			fputs(buf + (i * BUFSIZ), stdout);
		for (i = 0; i < tail; i++)
			fputs(buf + (i * BUFSIZ), stdout);
	} else {
		for (i = head; i < tail; i++)
			fputs(buf + (i * BUFSIZ), stdout);
	}

	fflush(stdout);
	free(buf);
	fclose(str);
}

static void
roll_file(const char *filename, off_t *size)
{
	char buf[BUFSIZ];
	int fd;
	struct stat st;

	if (!(fd = open(filename, O_RDONLY)))
		err(EXIT_FAILURE, _("cannot open \"%s\" for read"), filename);

	if (fstat(fd, &st) == -1)
		err(EXIT_FAILURE, _("cannot stat \"%s\""), filename);

	if (st.st_size == *size) {
		close(fd);
		return;
	}

	if (lseek(fd, *size, SEEK_SET) != (off_t)-1) {
		ssize_t rc, wc;

		while ((rc = read(fd, buf, sizeof(buf))) > 0) {
			wc = write(STDOUT_FILENO, buf, rc);
			if (rc != wc)
				warnx(_("incomplete write to \"%s\" (written %zd, expected %zd)\n"),
					filename, wc, rc);
		}
		fflush(stdout);
	}
	close(fd);
	*size = st.st_size;
}

static void
watch_file(const char *filename, off_t *size)
{
	do {
		roll_file(filename, size);
		usleep(250000);
	} while(1);
}


#ifdef HAVE_INOTIFY_INIT

#define EVENTS		(IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT)
#define NEVENTS		4

static int
watch_file_inotify(const char *filename, off_t *size)
{
	char buf[ NEVENTS * sizeof(struct inotify_event) ];
	int fd, ffd, e;
	ssize_t len;

	fd = inotify_init();
	if (fd == -1)
		return 0;

	ffd = inotify_add_watch(fd, filename, EVENTS);
	if (ffd == -1) {
		if (errno == ENOSPC)
			errx(EXIT_FAILURE, _("%s: cannot add inotify watch "
				"(limit of inotify watches was reached)."),
				filename);

		err(EXIT_FAILURE, _("%s: cannot add inotify watch."), filename);
	}

	while (ffd >= 0) {
		len = read(fd, buf, sizeof(buf));
		if (len < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (len < 0)
			err(EXIT_FAILURE,
				_("%s: cannot read inotify events"), filename);

		for (e = 0; e < len; ) {
			struct inotify_event *ev = (struct inotify_event *) &buf[e];

			if (ev->mask & IN_MODIFY)
				roll_file(filename, size);
			else {
				close(ffd);
				ffd = -1;
				break;
			}
			e += sizeof(struct inotify_event) + ev->len;
		}
	}
	close(fd);
	return 1;
}

#endif /* HAVE_INOTIFY_INIT */

int main(int argc, char **argv)
{
	const char *filename;
	int lines = DEFAULT_LINES;
	struct stat st;
	off_t size = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	argc--;
	argv++;

	for (; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
		if (!strcmp(*argv, "-n") || !strcmp(*argv, "--lines")) {
			argc--;	argv++;
			if (argc > 0 && (lines = atoi(argv[0])) <= 0)
				errx(EXIT_FAILURE, _("invalid number of lines"));
		}
		else if (isdigit(argv[0][1])) {
			if ((lines = atoi(*argv + 1)) <= 0)
				errx(EXIT_FAILURE, _("invalid number of lines"));
		}
		else
			errx(EXIT_FAILURE, _("invalid option"));
	}

	if (argc != 1)
		errx(EXIT_FAILURE, _("usage: tailf [-n N | -N] logfile"));

	filename = argv[0];

	if (stat(filename, &st) != 0)
		err(EXIT_FAILURE, _("cannot stat \"%s\""), filename);

	size = st.st_size;;
	tailf(filename, lines);

#ifdef HAVE_INOTIFY_INIT
	if (!watch_file_inotify(filename, &size))
#endif
		watch_file(filename, &size);

	return EXIT_SUCCESS;
}

