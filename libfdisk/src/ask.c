
#include "strutils.h"

#include "fdiskP.h"

struct fdisk_ask *fdisk_new_ask(void)
{
	return calloc(1, sizeof(struct fdisk_ask));
}

void fdisk_reset_ask(struct fdisk_ask *ask)
{
	assert(ask);
	free(ask->query);

	memset(ask, 0, sizeof(*ask));
}

void fdisk_free_ask(struct fdisk_ask *ask)
{
	if (!ask)
		return;
	fdisk_reset_ask(ask);
	free(ask);
}

const char *fdisk_ask_get_query(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->query;
}

int fdisk_ask_set_query(struct fdisk_ask *ask, const char *str)
{
	assert(ask);
	return !strdup_to_struct_member(ask, query, str) ? -ENOMEM : 0;
}

int fdisk_ask_get_type(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->type;
}

int fdisk_ask_set_type(struct fdisk_ask *ask, int type)
{
	assert(ask);
	ask->type = type;
	return 0;
}

int fdisk_do_ask(struct fdisk_context *cxt, struct fdisk_ask *ask)
{
	int rc;

	assert(ask);
	assert(cxt);

	DBG(ASK, dbgprint("asking for '%s'", ask->query));

	if (!cxt->ask_cb) {
		DBG(ASK, dbgprint("no ask callback specified!"));
		return -EINVAL;
	}

	rc = cxt->ask_cb(cxt, ask, cxt->ask_data);

	DBG(ASK, dbgprint("do_ask done [rc=%d]", rc));
	return rc;
}

#define is_number_ask(a)  (fdisk_is_ask(a, NUMBER) || fdisk_is_ask(a, OFFSET))

const char *fdisk_ask_number_get_range(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.range;
}

int fdisk_ask_number_set_range(struct fdisk_ask *ask, const char *range)
{
	assert(ask);
	assert(is_number_ask(ask));
	ask->data.num.range = range;
	return 0;
}

uint64_t fdisk_ask_number_get_default(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.dfl;
}

int fdisk_ask_number_set_default(struct fdisk_ask *ask, uint64_t dflt)
{
	assert(ask);
	ask->data.num.dfl = dflt;
	return 0;
}

uint64_t fdisk_ask_number_get_low(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.low;
}

int fdisk_ask_number_set_low(struct fdisk_ask *ask, uint64_t low)
{
	assert(ask);
	ask->data.num.low = low;
	return 0;
}

uint64_t fdisk_ask_number_get_high(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.hig;
}

int fdisk_ask_number_set_high(struct fdisk_ask *ask, uint64_t high)
{
	assert(ask);
	ask->data.num.hig = high;
	return 0;
}

uint64_t fdisk_ask_number_get_result(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.result;
}

int fdisk_ask_number_set_result(struct fdisk_ask *ask, uint64_t result)
{
	assert(ask);
	ask->data.num.result = result;
	return 0;
}

uint64_t fdisk_ask_number_get_base(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.base;
}

int fdisk_ask_number_set_base(struct fdisk_ask *ask, uint64_t base)
{
	assert(ask);
	ask->data.num.base = base;
	return 0;
}

/* if numbers are not in bytes, then specify number of bytes per the unit */
uint64_t fdisk_ask_number_get_unit(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.unit;
}

int fdisk_ask_number_set_unit(struct fdisk_ask *ask, uint64_t unit)
{
	assert(ask);
	ask->data.num.unit = unit;
	return 0;
}

int fdisk_ask_number_is_relative(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_number_ask(ask));
	return ask->data.num.relative;
}

int fdisk_ask_number_set_relative(struct fdisk_ask *ask, int relative)
{
	assert(ask);
	ask->data.num.relative = relative ? 1 : 0;
	return 0;
}

/*
 * Generates string with list ranges (e.g. 1,2,5-8) for the 'cur'
 */
static char *mk_string_list(char *ptr, size_t *len, size_t *begin,
			    size_t *run, ssize_t cur)
{
	int rlen;

	if (cur != -1) {
		if (!*begin) {			/* begin of the list */
			*begin = cur + 1;
			return ptr;
		}

		if (*begin + *run == cur) {	/* no gap, continue */
			(*run)++;
			return ptr;
		}
	} else if (!*begin) {
		*ptr = '\0';
		return ptr;		/* end of empty list */
	}

					/* add to the list */
	if (!*run)
		rlen = snprintf(ptr, *len, "%zd,", *begin);
	else if (*run == 1)
		rlen = snprintf(ptr, *len, "%zd,%zd,", *begin, *begin + 1);
	else
		rlen = snprintf(ptr, *len, "%zd-%zd,", *begin, *begin + *run);

	if (rlen < 0 || (size_t) rlen + 1 > *len)
		return NULL;

	ptr += rlen;

	if (rlen > 0 && *len > (size_t) rlen)
		*len -= rlen;
	else
		*len = 0;

	if (cur == -1 && *begin) {
		/* end of the list */
		*(ptr - 1) = '\0';	/* remove tailing ',' from the list */
		return ptr;
	}

	*begin = cur + 1;
	*run = 0;

	return ptr;
}

/* returns: 1=0 on success, < 0 on error, 1 if no free/used partition */
int fdisk_ask_partnum(struct fdisk_context *cxt, size_t *partnum, int wantnew)
{
	int rc = 0;
	char range[BUFSIZ], *ptr = range;
	size_t i, len = sizeof(range), begin = 0, run = 0;
	struct fdisk_ask *ask = NULL;
	__typeof__(ask->data.num) *num;

	assert(cxt);
	assert(cxt->label);
	assert(partnum);

	DBG(ASK, dbgprint("%s: asking for %s partition number (max: %zd)",
				cxt->label->name, wantnew ? "new" : "used",
				cxt->label->nparts_max));

	ask = fdisk_new_ask();
	if (!ask)
		return -ENOMEM;

	fdisk_ask_set_type(ask, FDISK_ASKTYPE_NUMBER);
	num = &ask->data.num;

	for (i = 0; i < cxt->label->nparts_max; i++) {
		int status = 0;

		rc = fdisk_partition_get_status(cxt, i, &status);
		if (rc)
			break;
		if (wantnew && !(status & FDISK_PARTSTAT_USED)) {
			ptr = mk_string_list(ptr, &len, &begin, &run, i);
			if (!ptr) {
				rc = -EINVAL;
				break;
			}
			if (!num->low)
				num->dfl = num->low = i + 1;
			num->hig = i + 1;
		} else if (!wantnew && (status & FDISK_PARTSTAT_USED)) {
			ptr = mk_string_list(ptr, &len, &begin, &run, i);
			if (!num->low)
				num->low = i + 1;
			num->dfl = num->hig = i + 1;
		}
	}

	DBG(ASK, dbgprint("ask limits: low: %zd, high: %zd, default: %zd",
				num->low, num->hig, num->dfl));

	if (!rc && !wantnew && num->low == num->hig) {
		if (num->low > 0) {
			/* only one existing partiton, don't ask, return the number */
			fdisk_ask_number_set_result(ask, num->low);
			fdisk_info(cxt, _("Selected partition %d"), num->low);

		} else if (num->low == 0) {
			fdisk_info(cxt, _("No partition is defined yet!"));
			rc = 1;
		}
		goto dont_ask;
	}
	if (!rc && wantnew && num->low == num->hig) {
		if (num->low > 0) {
			/* only one free partition, don't ask, return the number */
			fdisk_ask_number_set_result(ask, num->low);
			fdisk_info(cxt, _("Selected partition %d"), num->low);
		}
		if (num->low == 0) {
			fdisk_info(cxt, _("No free partition available!"));
			rc = 1;
		}
		goto dont_ask;
	}
	if (!rc) {
		mk_string_list(ptr, &len, &begin, &run, -1);	/* terminate the list */
		rc = fdisk_ask_number_set_range(ask, range);
	}
	if (!rc)
		rc = fdisk_ask_set_query(ask, _("Partition number"));
	if (!rc)
		rc = fdisk_do_ask(cxt, ask);

dont_ask:
	if (!rc) {
		*partnum = fdisk_ask_number_get_result(ask);
		if (*partnum)
			*partnum -= 1;
	}
	DBG(ASK, dbgprint("result: %zd [rc=%d]\n", fdisk_ask_number_get_result(ask), rc));
	fdisk_free_ask(ask);
	return rc;
}

/* very basic wraper to ask numbers */
int fdisk_ask_number(struct fdisk_context *cxt,
		     uintmax_t low,
		     uintmax_t dflt,
		     uintmax_t high,
		     const char *query,
		     uintmax_t *result)
{
	struct fdisk_ask *ask;
	int rc;

	assert(cxt);

	ask = fdisk_new_ask();
	if (!ask)
		return -ENOMEM;

	rc = fdisk_ask_set_type(ask, FDISK_ASKTYPE_NUMBER);
	if (!rc)
		fdisk_ask_number_set_low(ask, low);
	if (!rc)
		fdisk_ask_number_set_default(ask, dflt);
	if (!rc)
		fdisk_ask_number_set_high(ask, high);
	if (!rc)
		fdisk_ask_set_query(ask, query);
	if (!rc)
		rc = fdisk_do_ask(cxt, ask);
	if (!rc)
		*result = fdisk_ask_number_get_result(ask);

	fdisk_free_ask(ask);
	DBG(ASK, dbgprint("result: %zd [rc=%d]\n", *result, rc));
	return rc;
}

char *fdisk_ask_string_get_result(struct fdisk_ask *ask)
{
	assert(ask);
	assert(fdisk_is_ask(ask, STRING));
	return ask->data.str.result;
}

/*
 * The @result has to be poiter to the allocated buffer.
 */
int fdisk_ask_string_set_result(struct fdisk_ask *ask, char *result)
{
	assert(ask);
	ask->data.str.result = result;
	return 0;
}

/*
 * Don't forget to deallocate @result.
 */
int fdisk_ask_string(struct fdisk_context *cxt,
		     const char *query,
		     char **result)
{
	struct fdisk_ask *ask;
	int rc;

	assert(cxt);

	ask = fdisk_new_ask();
	if (!ask)
		return -ENOMEM;

	rc = fdisk_ask_set_type(ask, FDISK_ASKTYPE_STRING);
	if (!rc)
		fdisk_ask_set_query(ask, query);
	if (!rc)
		rc = fdisk_do_ask(cxt, ask);
	if (!rc)
		*result = fdisk_ask_string_get_result(ask);

	fdisk_free_ask(ask);
	DBG(ASK, dbgprint("result: %s [rc=%d]\n", *result, rc));
	return rc;
}

int fdisk_ask_yesno(struct fdisk_context *cxt,
		     const char *query,
		     int *result)
{
	struct fdisk_ask *ask;
	int rc;

	assert(cxt);

	ask = fdisk_new_ask();
	if (!ask)
		return -ENOMEM;

	rc = fdisk_ask_set_type(ask, FDISK_ASKTYPE_YESNO);
	if (!rc)
		fdisk_ask_set_query(ask, query);
	if (!rc)
		rc = fdisk_do_ask(cxt, ask);
	if (!rc)
		*result = fdisk_ask_yesno_get_result(ask);

	fdisk_free_ask(ask);
	DBG(ASK, dbgprint("result: %d [rc=%d]\n", *result, rc));
	return rc;
}

uint64_t fdisk_ask_yesno_get_result(struct fdisk_ask *ask)
{
	assert(ask);
	assert(fdisk_is_ask(ask, YESNO));
	return ask->data.yesno.result;
}

int fdisk_ask_yesno_set_result(struct fdisk_ask *ask, uint64_t result)
{
	assert(ask);
	ask->data.yesno.result = result;
	return 0;
}

struct tt *fdisk_ask_get_table(struct fdisk_ask *ask)
{
	assert(ask);
	assert(fdisk_is_ask(ask, TABLE));
	return ask->data.table;
}

int fdisk_print_table(struct fdisk_context *cxt, struct tt *tb)
{
	struct fdisk_ask *ask;
	int rc;

	assert(cxt);
	assert(tb);

	ask = fdisk_new_ask();
	if (!ask)
		return -ENOMEM;

	fdisk_ask_set_type(ask, FDISK_ASKTYPE_TABLE);
	ask->data.table = tb;

	rc = fdisk_do_ask(cxt, ask);

	fdisk_free_ask(ask);
	return rc;
}

#define is_print_ask(a) (fdisk_is_ask(a, WARN) || fdisk_is_ask(a, WARNX) || fdisk_is_ask(a, INFO))

int fdisk_ask_print_get_errno(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_print_ask(ask));
	return ask->data.print.errnum;
}

int fdisk_ask_print_set_errno(struct fdisk_ask *ask, int errnum)
{
	assert(ask);
	ask->data.print.errnum = errnum;
	return 0;
}

const char *fdisk_ask_print_get_mesg(struct fdisk_ask *ask)
{
	assert(ask);
	assert(is_print_ask(ask));
	return ask->data.print.mesg;
}

/* does not reallocate the message! */
int fdisk_ask_print_set_mesg(struct fdisk_ask *ask, const char *mesg)
{
	assert(ask);
	ask->data.print.mesg = mesg;
	return 0;
}

static int do_vprint(struct fdisk_context *cxt, int errnum, int type,
		 const char *fmt, va_list va)
{
	struct fdisk_ask *ask;
	int rc;
	char *mesg;

	assert(cxt);

	if (vasprintf(&mesg, fmt, va) < 0)
		return -ENOMEM;

	ask = fdisk_new_ask();
	if (!ask) {
		free(mesg);
		return -ENOMEM;
	}

	fdisk_ask_set_type(ask, type);
	fdisk_ask_print_set_mesg(ask, mesg);
	if (errnum >= 0)
		fdisk_ask_print_set_errno(ask, errnum);
	rc = fdisk_do_ask(cxt, ask);

	fdisk_free_ask(ask);
	free(mesg);
	return rc;
}

int fdisk_info(struct fdisk_context *cxt, const char *fmt, ...)
{
	int rc;
	va_list ap;

	assert(cxt);
	va_start(ap, fmt);
	rc = do_vprint(cxt, -1, FDISK_ASKTYPE_INFO, fmt, ap);
	va_end(ap);
	return rc;
}

int fdisk_warn(struct fdisk_context *cxt, const char *fmt, ...)
{
	int rc;
	va_list ap;

	assert(cxt);
	va_start(ap, fmt);
	rc = do_vprint(cxt, errno, FDISK_ASKTYPE_WARN, fmt, ap);
	va_end(ap);
	return rc;
}

int fdisk_warnx(struct fdisk_context *cxt, const char *fmt, ...)
{
	int rc;
	va_list ap;

	assert(cxt);
	va_start(ap, fmt);
	rc = do_vprint(cxt, -1, FDISK_ASKTYPE_WARNX, fmt, ap);
	va_end(ap);
	return rc;
}

int fdisk_info_new_partition(
			struct fdisk_context *cxt,
			int num, sector_t start, sector_t stop,
			struct fdisk_parttype *t)
{
	int rc;
	char *str = size_to_human_string(SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE,
				     (uint64_t)(stop - start + 1) * cxt->sector_size);

	rc = fdisk_info(cxt, _("Partition %d of type %s and of size %s is set\n"),
			num, t ? t->name : _("Unknown"), str);
	free(str);
	return rc;
}

#ifdef TEST_PROGRAM
struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_mac_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt) { return NULL; }

int test_ranges(struct fdisk_test *ts, int argc, char *argv[])
{
	/*                1  -  3,       6,    8, 9,   11    13 */
	size_t nums[] = { 1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 1 };
	size_t numx[] = { 0, 0, 0 };
	char range[BUFSIZ], *ptr = range;
	size_t i, len = sizeof(range), begin = 0, run = 0;

	for (i = 0; i < ARRAY_SIZE(nums); i++) {
		if (!nums[i])
			continue;
		ptr = mk_string_list(ptr, &len, &begin, &run, i);
	}
	mk_string_list(ptr, &len, &begin, &run, -1);
	printf("list: '%s'\n", range);

	ptr = range;
	len = sizeof(range), begin = 0, run = 0;
	for (i = 0; i < ARRAY_SIZE(numx); i++) {
		if (!numx[i])
			continue;
		ptr = mk_string_list(ptr, &len, &begin, &run, i);
	}
	mk_string_list(ptr, &len, &begin, &run, -1);
	printf("empty list: '%s'\n", range);

	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
	{ "--ranges",  test_ranges,    "generates ranges" },
	{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
