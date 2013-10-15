
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "c.h"
#include "strutils.h"
#include "rpmatch.h"
#include "xalloc.h"

#include "fdisk.h"

static unsigned int info_count;

int get_user_reply(struct fdisk_context *cxt, const char *prompt,
			  char *buf, size_t bufsz)
{
	char *p;
	size_t sz;

	do {
	        fputs(prompt, stdout);
		fflush(stdout);

		if (!fgets(buf, bufsz, stdin)) {
			if (fdisk_label_is_changed(cxt->label)) {
				fprintf(stderr, _("\nDo you really want to quit? "));

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

#define tochar(num)	((int) ('a' + num - 1))
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
	int inchar = fdisk_ask_number_inchars(ask);

	assert(q);

	DBG(ASK, dbgprint("asking for number "
			"['%s', <%ju,%ju>, default=%ju, range: %s]",
			q, low, high, dflt, range));

	if (range && dflt >= low && dflt <= high) {
		if (inchar)
			snprintf(prompt, sizeof(prompt), _("%s (%s, default %c): "),
					q, range, tochar(dflt));
		else
			snprintf(prompt, sizeof(prompt), _("%s (%s, default %ju): "),
					q, range, dflt);

	} else if (dflt >= low && dflt <= high) {
		if (inchar)
			snprintf(prompt, sizeof(prompt), _("%s (%c-%c, default %c): "),
					q, tochar(low), tochar(high), tochar(dflt));
		else
			snprintf(prompt, sizeof(prompt), _("%s (%ju-%ju, default %ju): "),
					q, low, high, dflt);
	} else if (inchar)
		snprintf(prompt, sizeof(prompt), _("%s (%c-%c): "),
				q, tochar(low), tochar(high));
	else
		snprintf(prompt, sizeof(prompt), _("%s (%ju-%ju): "),
				q, low, high);

	do {
		int rc = get_user_reply(cxt, prompt, buf, bufsz);
		uint64_t num;

		if (rc)
			return rc;
		if (!*buf && dflt >= low && dflt <= high)
			return fdisk_ask_number_set_result(ask, dflt);

		if (isdigit_string(buf)) {
			char *end;

			errno = 0;
			num = strtoumax(buf, &end, 10);
			if (errno || buf == end || (end && *end))
				continue;
		} else if (inchar && isalpha(*buf)) {
			num = tolower(*buf) - 'a' + 1;
		} else
			rc = -EINVAL;

		if (rc == 0 && num >= low && num <= high)
			return fdisk_ask_number_set_result(ask, num);

		fdisk_warnx(cxt, _("Value out of range."));
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

	DBG(ASK, dbgprint("asking for offset ['%s', <%ju,%ju>, base=%ju, default=%ju, range: %s]",
				q, low, high, base, dflt, range));

	if (range && dflt >= low && dflt <= high)
		snprintf(prompt, sizeof(prompt), _("%s (%s, default %ju): "), q, range, dflt);
	else if (dflt >= low && dflt <= high)
		snprintf(prompt, sizeof(prompt), _("%s (%ju-%ju, default %ju): "), q, low, high, dflt);
	else
		snprintf(prompt, sizeof(prompt), _("%s (%ju-%ju): "), q, low, high);

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
		DBG(ASK, dbgprint("parsed size: %ju", num));
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

		DBG(ASK, dbgprint("final offset: %ju [sig: %c, power: %d, %s]",
				num, sig, pwr,
				sig ? "relative" : "absolute"));
		if (num >= low && num <= high) {
			if (sig)
				fdisk_ask_number_set_relative(ask, 1);
			return fdisk_ask_number_set_result(ask, num);
		}
		fdisk_warnx(cxt, _("Value out of range."));
	} while (1);

	return -1;
}

static void fputs_info(struct fdisk_ask *ask, FILE *out, char *buf, size_t bufsz)
{
	const char *msg;
	unsigned int flags;

	assert(ask);

	msg = fdisk_ask_print_get_mesg(ask);
	flags = fdisk_ask_get_flags(ask);

	if (!msg)
		return;
	if (info_count == 1)
		fputc('\n', out);
	if (flags == 0 || !colors_wanted())
		goto simple;

	if (flags & FDISK_INFO_COLON) {
		size_t sz;
		char *sep = _(": ");
		char *p = strstr(msg, sep);

		if (!p)
			goto simple;

		sz = strlen(sep);
		strncpy(buf, msg, bufsz);
		buf[p - msg + sz] = '\0';

		color_enable(UL_COLOR_BROWN);
		fputs(buf, out);
		color_disable();
		fputs(p + sz, out);

	} else if (flags & FDISK_INFO_SUCCESS) {
		color_enable(UL_COLOR_BOLD);
		fputs(msg, out);
		color_disable();
	} else {
simple:
		fputs(msg, out);
	}
	fputc('\n', out);
}

int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)))
{
	int rc = 0;
	char buf[BUFSIZ];

	assert(cxt);
	assert(ask);

	if (fdisk_ask_get_type(ask) != FDISK_ASKTYPE_INFO)
		info_count = 0;

	switch(fdisk_ask_get_type(ask)) {
	case FDISK_ASKTYPE_NUMBER:
		return ask_number(cxt, ask, buf, sizeof(buf));
	case FDISK_ASKTYPE_OFFSET:
		return ask_offset(cxt, ask, buf, sizeof(buf));
	case FDISK_ASKTYPE_INFO:
		info_count++;
		fputs_info(ask, stdout, buf, sizeof(buf));
		break;
	case FDISK_ASKTYPE_WARNX:
		color_fenable(UL_COLOR_RED, stderr);
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		color_fdisable(stderr);
		fputc('\n', stderr);
		break;
	case FDISK_ASKTYPE_WARN:
		color_fenable(UL_COLOR_RED, stderr);
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		errno = fdisk_ask_print_get_errno(ask);
		fprintf(stderr, ": %m\n");
		color_fdisable(stderr);
		break;
	case FDISK_ASKTYPE_YESNO:
		fputc('\n', stdout);
		fputs(fdisk_ask_get_query(ask), stdout);
		rc = get_user_reply(cxt, _(" [Y]es/[N]o: "), buf, sizeof(buf));
		if (rc == 0)
			fdisk_ask_yesno_set_result(ask, rpmatch(buf));
		DBG(ASK, dbgprint("yes-no ask: reply '%s' [rc=%d]", buf, rc));
		break;
	case FDISK_ASKTYPE_TABLE:
		fputc('\n', stdout);
		tt_print_table(fdisk_ask_get_table(ask));
		break;
	case FDISK_ASKTYPE_STRING:
	{
		char prmt[BUFSIZ];
		snprintf(prmt, sizeof(prmt), "%s: ", fdisk_ask_get_query(ask));
		fputc('\n', stdout);
		rc = get_user_reply(cxt, prmt, buf, sizeof(buf));
		if (rc == 0)
			fdisk_ask_string_set_result(ask, xstrdup(buf));
		DBG(ASK, dbgprint("string ask: reply '%s' [rc=%d]", buf, rc));
		break;
	}
	default:
		warnx(_("internal error: unsupported dialog type %d"), fdisk_ask_get_type(ask));
		return -EINVAL;
	}
	return rc;
}

struct fdisk_parttype *ask_partition_type(struct fdisk_context *cxt)
{
	const char *q;

	if (!cxt || !cxt->label || !cxt->label->nparttypes)
		return NULL;

        q = fdisk_is_parttype_string(cxt) ?
		_("Partition type (type L to list all types): ") :
		_("Hex code (type L to list all codes): ");
	do {
		char buf[256];
		int rc = get_user_reply(cxt, q, buf, sizeof(buf));

		if (rc)
			break;

		if (buf[1] == '\0' && toupper(*buf) == 'L')
			list_partition_types(cxt);
		else if (*buf)
			return fdisk_parse_parttype(cxt, buf);
        } while (1);

	return NULL;
}
