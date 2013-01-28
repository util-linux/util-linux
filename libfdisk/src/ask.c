
#include "fdiskP.h"


const char *fdisk_ask_get_question(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->query;
}

int fdisk_ask_get_type(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->type;
}

const char *fdisk_ask_number_get_range(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->data.num.range;
}
uint64_t fdisk_ask_number_get_default(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->data.num.dfl;
}

uint64_t fdisk_ask_number_get_low(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->data.num.low;
}

uint64_t fdisk_ask_number_get_high(struct fdisk_ask *ask)
{
	assert(ask);
	return ask->data.num.hig;
}

int fdisk_ask_number_set_result(struct fdisk_ask *ask, uint64_t result)
{
	assert(ask);
	ask->data.num.result = result;
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

int fdisk_ask_partnum(struct fdisk_context *cxt, size_t *partnum, int wantnew)
{
	int rc;
	char range[BUFSIZ], *ptr = range;
	size_t i, len = sizeof(range), begin = 0, run = 0;
	struct fdisk_ask ask = { .name = "partnum" };
	__typeof__(ask.data.num) *num = &ask.data.num;

	assert(cxt);
	assert(cxt->label);
	assert(partnum);

	if (!cxt->ask_cb) {
		DBG(ASK, dbgprint("no ask callback specified!"));
		return -EINVAL;	/* ask callback undefined */
	}

	DBG(ASK, dbgprint("%s: asking for %s partition number (max: %zd)",
				cxt->label->name,
				wantnew ? "new" : "used",
				cxt->label->nparts_max));

	for (i = 0; i < cxt->label->nparts_max; i++) {
		int status = 0;

		rc = fdisk_partition_get_status(cxt, i, &status);
		if (rc)
			return rc;

		if (wantnew && !(status & FDISK_PARTSTAT_USED)) {
			ptr = mk_string_list(ptr, &len, &begin, &run, i);
			if (!ptr)
				return -EINVAL;
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

	if (wantnew && num->low == 0 && num->hig == 0) {
		DBG(ASK, dbgprint("no free partition"));
		return 1;
	}

	mk_string_list(ptr, &len, &begin, &run, -1);	/* terminate the list */
	num->range = range;

	ask.query = _("Partition number");
	ask.type = FDISK_ASKTYPE_NUMBER;

	rc = cxt->ask_cb(cxt, &ask, cxt->ask_data);
	*partnum = num->result ? num->result - 1 : 0;
	DBG(ASK, dbgprint("result: %zd [rc=%d]\n", num->result, rc));
	return rc;
}


#ifdef TEST_PROGRAM
struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_aix_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_mac_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sun_label(struct fdisk_context *cxt) { return NULL; }

unsigned int read_int_with_suffix(struct fdisk_context *cxt,
				  unsigned int low, unsigned int dflt, unsigned int high,
				  unsigned int base, char *mesg, int *is_suffix_used)
{
	return 0;
}

unsigned int read_int(struct fdisk_context *cxt,
			     unsigned int low, unsigned int dflt,
			     unsigned int high, unsigned int base, char *mesg)
{
	return 0;
}

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
