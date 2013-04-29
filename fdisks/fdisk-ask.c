
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
	const char *q = fdisk_ask_get_query(ask);
	const char *range = fdisk_ask_number_get_range(ask);

	uint64_t dflt = fdisk_ask_number_get_default(ask),
		 low = fdisk_ask_number_get_low(ask),
		 high = fdisk_ask_number_get_high(ask);

	assert(q);

	DBG(ASK, dbgprint("asking for number ['%s', <%jd,%jd>, default=%jd, range: %s]",
				q, low, high, dflt, range));
	if (range && dflt >= low && dflt <= high)
		snprintf(prompt, sizeof(prompt), _("%s (%s, default %jd): "), q, range, dflt);
	else if (dflt >= low && dflt <= high)
		snprintf(prompt, sizeof(prompt), _("%s (%jd-%jd, default %jd): "), q, low, high, dflt);
	else
		snprintf(prompt, sizeof(prompt), _("%s (%jd-%jd): "), q, low, high);

	do {
		int rc = get_user_reply(cxt, prompt, buf, bufsz);

		if (rc)
			return rc;
		if (!*buf && dflt >= low && dflt <= high)
			return fdisk_ask_number_set_result(ask, dflt);
		else if (isdigit_string(buf)) {
			char *end;
			uint64_t num;

			errno = 0;
			num = strtoumax(buf, &end, 10);
			if (errno || buf == end || (end && *end))
				continue;
			if (num >= low && num <= high)
				return fdisk_ask_number_set_result(ask, num);
			printf(_("Value out of range.\n"));
		}
	} while (1);

	return -1;
}

static int ask_offset(struct fdisk_context *cxt,
		      struct fdisk_ask *ask,
		      char *buf, size_t bufsz)
{
	char prompt[128] = { '\0' };
	const char *q = fdisk_ask_get_query(ask);
	const char *range = fdisk_ask_number_get_range(ask);

	uint64_t dflt = fdisk_ask_number_get_default(ask),
		 low = fdisk_ask_number_get_low(ask),
		 high = fdisk_ask_number_get_high(ask),
		 base = fdisk_ask_number_get_base(ask);

	assert(q);

	DBG(ASK, dbgprint("asking for offset ['%s', <%jd,%jd>, base=%jd, default=%jd, range: %s]",
				q, low, high, base, dflt, range));

	if (range && dflt >= low && dflt <= high)
		snprintf(prompt, sizeof(prompt), _("%s (%s, default %jd): "), q, range, dflt);
	else if (dflt >= low && dflt <= high)
		snprintf(prompt, sizeof(prompt), _("%s (%jd-%jd, default %jd): "), q, low, high, dflt);
	else
		snprintf(prompt, sizeof(prompt), _("%s (%jd-%jd): "), q, low, high);

	do {
		uint64_t num = 0;
		char sig = 0, *p;
		int pwr = 0;

		int rc = get_user_reply(cxt, prompt, buf, bufsz);
		if (rc)
			return rc;
		if (!*buf && dflt >= low && dflt <= high)
			return fdisk_ask_number_set_result(ask, dflt);

		p = buf;
		if (*p == '+' || *p == '-') {
			sig = *buf;
			p++;
		}

		rc = parse_size(p, &num, &pwr);
		if (rc)
			continue;
		DBG(ASK, dbgprint("parsed size: %jd", num));
		if (sig && pwr) {
			/* +{size}{K,M,...} specified, the "num" is in bytes */
			uint64_t unit = fdisk_ask_number_get_unit(ask);
			num += unit/2;	/* round */
			num /= unit;
		}
		if (sig == '+')
			num += base;
		else if (sig == '-')
			num = base - num;

		DBG(ASK, dbgprint("final offset: %jd [sig: %c, power: %d, %s]",
				num, sig, pwr,
				sig ? "relative" : "absolute"));
		if (num >= low && num <= high) {
			if (sig)
				fdisk_ask_number_set_relative(ask, 1);
			return fdisk_ask_number_set_result(ask, num);
		}
		printf(_("Value out of range.\n"));
	} while (1);

	return -1;
}

int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)))
{
	int rc = 0;
	char buf[BUFSIZ];

	assert(cxt);
	assert(ask);

	switch(fdisk_ask_get_type(ask)) {
	case FDISK_ASKTYPE_NUMBER:
		return ask_number(cxt, ask, buf, sizeof(buf));
	case FDISK_ASKTYPE_OFFSET:
		return ask_offset(cxt, ask, buf, sizeof(buf));
	case FDISK_ASKTYPE_INFO:
		fputs(fdisk_ask_print_get_mesg(ask), stdout);
		fputc('\n', stdout);
		break;
	case FDISK_ASKTYPE_WARNX:
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		fputc('\n', stderr);
		break;
	case FDISK_ASKTYPE_WARN:
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		errno = fdisk_ask_print_get_errno(ask);
		fprintf(stderr, ": %m\n");
		break;
	case FDISK_ASKTYPE_YESNO:
		fputc('\n', stdout);
		fputs(fdisk_ask_get_query(ask), stdout);
		rc = get_user_reply(cxt, _(" [Y]es/[N]o: "), buf, sizeof(buf));
		if (rc == 0)
			ask->data.yesno.result = rpmatch(buf);
		DBG(ASK, dbgprint("yes-no ask: reply '%s' [rc=%d]", buf, rc));
		break;
	case FDISK_ASKTYPE_TABLE:
		tt_print_table(fdisk_ask_get_table(ask));
		break;
	default:
		warnx(_("internal error: unsupported dialog type %d"), fdisk_ask_get_type(ask));
		return -EINVAL;
	}
	return rc;
}
