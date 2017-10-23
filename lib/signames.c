/*
 * Copyright (c) 1988, 1993, 1994, 2017
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the University of
 *  California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 *  2017-10-14 Niklas Hambüchen <mail@nh2.me>
 *  - Extracted signal names mapping from kill.c
 *
 * Copyright (C) 2014 Sami Kerola <kerolasa@iki.fi>
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2017 Niklas Hambüchen <mail@nh2.me>
 */

#include <ctype.h>		/* for isdigit() */
#include <signames.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "c.h"
#include "strutils.h"


#ifdef SIGRTMIN
static int rtsig_to_signum(const char *sig)
{
	int num, maxi = 0;
	char *ep = NULL;

	if (strncasecmp(sig, "min+", 4) == 0)
		sig += 4;
	else if (strncasecmp(sig, "max-", 4) == 0) {
		sig += 4;
		maxi = 1;
	}
	if (!isdigit(*sig))
		return -1;
	errno = 0;
	num = strtol(sig, &ep, 10);
	if (!ep || sig == ep || errno || num < 0)
		return -1;
	num = maxi ? SIGRTMAX - num : SIGRTMIN + num;
	if (num < SIGRTMIN || SIGRTMAX < num)
		return -1;
	return num;
}
#endif

int signame_to_signum(const char *sig)
{
	size_t n;

	if (!strncasecmp(sig, "sig", 3))
		sig += 3;
#ifdef SIGRTMIN
	/* RT signals */
	if (!strncasecmp(sig, "rt", 2))
		return rtsig_to_signum(sig + 2);
#endif
	/* Normal signals */
	for (n = 0; n < ARRAY_SIZE(sys_signame); n++) {
		if (!strcasecmp(sys_signame[n].name, sig))
			return sys_signame[n].val;
	}
	return -1;
}
