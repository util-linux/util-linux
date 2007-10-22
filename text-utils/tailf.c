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
#include <sys/stat.h>
#include <ctype.h>
#include <err.h>
#include "nls.h"

#define DEFAULT_LINES  10

static size_t filesize(const char *filename)
{
    struct stat sb;

    if (!stat(filename, &sb)) return sb.st_size;
    return 0;
}

static void tailf(const char *filename, int lines)
{
    char **buffer;
    int  head = 0;
    int  tail = 0;
    FILE *str;
    int  i;

    if (!(str = fopen(filename, "r")))
	err(1, _("cannot open \"%s\" for read"), filename);

    buffer = malloc(lines * sizeof(*buffer));
    for (i = 0; i < lines; i++) buffer[i] = malloc(BUFSIZ + 1);

    while (fgets(buffer[tail], BUFSIZ, str)) {
	if (++tail >= lines) {
	    tail = 0;
	    head = 1;
	}
    }

    if (head) {
	for (i = tail; i < lines; i++) fputs(buffer[i], stdout);
	for (i = 0; i < tail; i++)     fputs(buffer[i], stdout);
    } else {
	for (i = head; i < tail; i++)  fputs(buffer[i], stdout);
    }
    fflush(stdout);

    for (i = 0; i < lines; i++) free(buffer[i]);
    free(buffer);

    fclose(str);
}

int main(int argc, char **argv)
{
    char       buffer[BUFSIZ];
    size_t     osize, nsize;
    FILE       *str;
    const char *filename;
    int        count, wcount;
    int        lines = DEFAULT_LINES;

    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    argc--;
    argv++;

    for (; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
	if (!strcmp(*argv, "-n") || !strcmp(*argv, "--lines")) {
	    argc--;
	    argv++;
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
    tailf(filename, lines);

    for (osize = filesize(filename);;) {
	nsize = filesize(filename);
	if (nsize != osize) {
	    if (!(str = fopen(filename, "r")))
		err(EXIT_FAILURE, _("cannot open \"%s\" for read"), filename);

	    if (!fseek(str, osize, SEEK_SET))
                while ((count = fread(buffer, 1, sizeof(buffer), str)) > 0) {
                    wcount = fwrite(buffer, 1, count, stdout);
                    if (wcount != count)
			warnx(_("incomplete write to \"%s\" (written %d, expected %d)\n"),
				filename, wcount, count);
		}
	    fflush(stdout);
	    fclose(str);
	    osize = nsize;
	}
	usleep(250000);		/* 250mS */
    }
    return EXIT_SUCCESS;
}
