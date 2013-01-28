
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "c.h"
#include "strutils.h"
#include "rpmatch.h"

#include "fdisk.h"

static int get_user_reply(struct fdisk_context *cxt, char *prompt,
			  char *buf, size_t bufsz)
{
	char *p;
	size_t sz;

	do {
	        fputs(prompt, stdout);
		fflush(stdout);

		if (!fgets(buf, bufsz, stdin)) {
			if (fdisk_label_is_changed(cxt->label)) {
				fprintf(stderr, _("Do you really want to quit? "));

				if (fgets(buf, bufsz, stdin) && !rpmatch(buf))
					continue;
			}
			fdisk_free_context(cxt);
			exit(EXIT_FAILURE);
		} else
			break;
	} while (1);

	for (p = buf; *p && !isgraph(*p); p++);	/* get first non-blank */

	if (p > buf)
		memmove(buf, p, p - buf);		/* remove blank space */
	sz = strlen(buf);
	if (sz && *(buf + sz - 1) == '\n')
		*(buf + sz - 1) = '\0';

	DBG(ASK, dbgprint("user's reply: >>>%s<<<", buf));
	return 0;
}

static int ask_number(struct fdisk_context *cxt,
		      struct fdisk_ask *ask,
		      char *buf, size_t bufsz)
{
	char prompt[128] = { '\0' };
	const char *q = fdisk_ask_get_question(ask);
	const char *range = fdisk_ask_number_get_range(ask);

	uint64_t dfl = fdisk_ask_number_get_default(ask),
		 low = fdisk_ask_number_get_low(ask),
		 hig = fdisk_ask_number_get_high(ask);

	assert(q);

	DBG(ASK, dbgprint("asking for number ['%s', <%jd,%jd>, default: %jd, range: %s]",
				q, low, hig, dfl, range));

	if (range && dfl)
		snprintf(prompt, sizeof(prompt), _("%s (%s, default %jd): "), q, range, dfl);
	else if (dfl)
		snprintf(prompt, sizeof(prompt), _("%s (%jd-%jd, default %jd): "), q, low, hig, dfl);
	else
		snprintf(prompt, sizeof(prompt), _("%s (%jd-%jd): "), q, low, hig);

	do {
		uint64_t num;
		int rc = get_user_reply(cxt, prompt, buf, bufsz);
		if (rc)
			return rc;

		if (!*buf && dfl)
			return fdisk_ask_number_set_result(ask, dfl);
		else if (isdigit_string(buf)) {
			char *end;
			errno = 0;
			num = strtoumax(buf, &end, 10);
			if (errno || buf == end || (end && *end))
				continue;
			if (num >= low && num <= hig)
				return fdisk_ask_number_set_result(ask, num);
			printf(_("Value out of range.\n"));
		}
	} while (1);

	return -1;
}

int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)))
{
	char buf[BUFSIZ];

	assert(cxt);
	assert(ask);

	switch(fdisk_ask_get_type(ask)) {
	case FDISK_ASKTYPE_NUMBER:
		return ask_number(cxt, ask, buf, sizeof(buf));
	default:
		return -EINVAL;
	}
	return 0;
}
