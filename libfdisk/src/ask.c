
#include "strutils.h"

#include "fdiskP.h"

struct fdisk_ask *fdisk_new_ask(void)
{
	return calloc(1, sizeof(struct fdisk_ask));
}

void fdisk_reset_ask(struct fdisk_ask *ask)
{
	assert(ask);
	free(ask->data.num.range);
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


const char *fdisk_ask_number_get_range(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->data.num.range;
}

int fdisk_ask_number_set_range(struct fdisk_ask *ask, const char *range)
{
	assert(ask);
	return !strdup_to_struct_member(ask, data.num.range, range) ? -ENOMEM : 0;
}

uint64_t fdisk_ask_number_get_default(struct fdisk_ask *ask)
{
	assert(ask);
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

/* returns: 1=0 on success, < 0 on error, 1 if no free partition */
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

	if (!rc) {
		mk_string_list(ptr, &len, &begin, &run, -1);	/* terminate the list */
		rc = fdisk_ask_number_set_range(ask, range);
	}
	if (!rc && wantnew && num->low == 0 && num->hig == 0) {
		DBG(ASK, dbgprint("no free partition"));
		rc = 1;
	}
	if (!rc)
		rc = fdisk_ask_set_query(ask, _("Partition number"));
	if (!rc)
		rc = fdisk_ask_set_type(ask, FDISK_ASKTYPE_NUMBER);
	if (!rc)
		rc = fdisk_do_ask(cxt, ask);
	if (!rc) {
		*partnum = fdisk_ask_number_get_result(ask);
		if (*partnum)
			*partnum -= 1;
	}

	fdisk_free_ask(ask);
	DBG(ASK, dbgprint("result: %zd [rc=%d]\n", *partnum, rc));
	return rc;
}


#ifdef TEST_PROGRAM
struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_aix_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_mac_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sun_label(struct fdisk_context *cxt) { return NULL; }

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
