/*
 * utmpdump
 *
 * Simple program to dump UTMP and WTMP files in raw format, so they can be
 * examined.
 *
 * Based on utmpdump dump from sysvinit suite.
 *
 * Copyright (C) 1991-2000 Miquel van Smoorenburg <miquels@cistron.nl>
 *
 * Copyright (C) 1998 Danek Duvall <duvall@alumni.princeton.edu>
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utmp.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"

char *timetostr(const time_t time)
{
	static char s[29];    /* [Sun Sep 01 00:00:00 1998 PST] */

	if (time != 0)
		strftime(s, 29, "%a %b %d %T %Y %Z", localtime(&time));
	else
		s[0] = '\0';

	return s;
}

time_t strtotime(const char *s_time)
{
	struct tm tm;

	memset(&tm, '\0', sizeof(struct tm));

	if (s_time[0] == ' ' || s_time[0] == '\0')
		return (time_t)0;

	strptime(s_time, "%a %b %d %T %Y", &tm);

	/* Cheesy way of checking for DST */
	if (s_time[26] == 'D')
		tm.tm_isdst = 1;

	return mktime(&tm);
}

#define cleanse(x) xcleanse(x, sizeof(x))
void xcleanse(char *s, int len)
{
	for ( ; *s && len-- > 0; s++)
		if (!isprint(*s) || *s == '[' || *s == ']')
			*s = '?';
}

void unspace(char *s, int len)
{
	while (*s && *s != ' ' && len--)
		++s;

	if (len > 0)
		*s = '\0';
}

void print_utline(struct utmp ut)
{
	char *addr_string, *time_string;
	struct in_addr in;

	in.s_addr = ut.ut_addr;
	addr_string = inet_ntoa(in);
	time_string = timetostr(ut.ut_time);
	cleanse(ut.ut_id);
	cleanse(ut.ut_user);
	cleanse(ut.ut_line);
	cleanse(ut.ut_host);

        /*            pid    id       user     line     host     addr       time */
	printf("[%d] [%05d] [%-4.4s] [%-*.*s] [%-*.*s] [%-*.*s] [%-15.15s] [%-28.28s]\n",
	       ut.ut_type, ut.ut_pid, ut.ut_id, 8, UT_NAMESIZE, ut.ut_user,
	       12, UT_LINESIZE, ut.ut_line, 20, UT_HOSTSIZE, ut.ut_host,
               addr_string, time_string);
}

void dump(FILE *fp, int forever)
{
	struct utmp ut;

	if (forever)
		fseek(fp, -10 * sizeof ut, SEEK_END);

	do {
		while (fread(&ut, sizeof ut, 1, fp) == 1)
			print_utline(ut);
		if (forever) sleep(1);
	} while (forever);
}

/* This function won't work properly if there's a ']' or a ' ' in the real
 * token.  Thankfully, this should never happen.  */
int gettok(char *line, char *dest, int size, int eatspace)
{
	int bpos, epos, eaten;
        char *t;

	bpos = strchr(line, '[') - line;
	if (bpos < 0) {
		fprintf(stderr, _("Extraneous newline in file.  Exiting."));
                exit(1);
        }
	line += 1 + bpos;

	epos = strchr(line, ']') - line;
	if (epos < 0) {
		fprintf(stderr, _("Extraneous newline in file.  Exiting."));
                exit(1);
        }
	line[epos] = '\0';

	eaten = bpos + epos + 1;

	if (eatspace)
                if ((t = strchr(line, ' ')))
                    *t = 0;

        strncpy(dest, line, size);

	return eaten + 1;
}

void undump(FILE *fp)
{
	struct utmp ut;
	char s_addr[16], s_time[29], *linestart, *line;
	int count = 0;

	line = linestart = malloc(1024 * sizeof *linestart);
	s_addr[15] = 0;
	s_time[28] = 0;

	while(fgets(linestart, 1023, fp))
	{
		line = linestart;
                memset(&ut, '\0', sizeof(ut));
                sscanf(line, "[%hd] [%d] [%4c] ", &ut.ut_type, &ut.ut_pid, ut.ut_id);

		line += 19;
                line += gettok(line, ut.ut_user, sizeof(ut.ut_user), 1);
                line += gettok(line, ut.ut_line, sizeof(ut.ut_line), 1);
                line += gettok(line, ut.ut_host, sizeof(ut.ut_host), 1);
		line += gettok(line, s_addr, sizeof(s_addr)-1, 1);
		line += gettok(line, s_time, sizeof(s_time)-1, 0);

                ut.ut_addr = inet_addr(s_addr);
                ut.ut_time = strtotime(s_time);

                fwrite(&ut, sizeof(ut), 1, stdout);

		++count;
	}

	free(linestart);
}

void
usage(int result)
{
	printf(_("Usage: utmpdump [ -frh ] [ filename ]\n"));
	exit(result);
}

int main(int argc, char **argv)
{
	int c;
	FILE *fp;
	int reverse = 0, forever = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt(argc, argv, "froh")) != EOF) {
		switch (c) {
		case 'r':
			reverse = 1;
			break;

		case 'f':
			forever = 1;
			break;

		case 'h':
			usage(0);
			break;

		default:
			usage(1);
		}
	}

	if (optind < argc) {
		fprintf(stderr, _("Utmp %sdump of %s\n"), reverse ? "un" : "", argv[optind]);
		if ((fp = fopen(argv[optind], "r")) == NULL) {
			perror("Unable to open file");
			exit(1);
		}
	}
	else {
		fprintf(stderr, _("Utmp %sdump of stdin\n"), reverse ? "un" : "");
		fp = stdin;
	}

	if (reverse)
		undump(fp);
	else
		dump(fp, forever);

	fclose(fp);
	return 0;
}
