
#include "fdiskP.h"

/* dump header (e.g. unit: sectors) */
struct fdisk_dumpheader {
	struct list_head	headers;
	char			*name;
	char			*data;
};

/* dump control struct */
struct fdisk_dump {
	struct fdisk_table	*table;
	struct list_head	headers;

	int			refcount;

	/* parser's state */
	FILE			*file;
	size_t			line;
};


static void fdisk_dump_free_header(struct fdisk_dump *dp, struct fdisk_dumpheader *fi)
{
	if (!fi)
		return;

	DBG(DUMP, ul_debugobj(fi, "free"));
	free(fi->name);
	free(fi->data);
	list_del(&fi->headers);
	free(fi);
}

/**
 * fdisk_new_dump:
 *
 * The dump hold fdisk_table and additional information to read/write
 * dump to the file.
 *
 * Returns: newly allocated dump struct.
 */
struct fdisk_dump *fdisk_new_dump(void)
{
	struct fdisk_dump *dp = NULL;

	dp = calloc(1, sizeof(*dp));
	if (!dp)
		return NULL;

	DBG(DUMP, ul_debugobj(dp, "alloc"));
	dp->refcount = 1;
	INIT_LIST_HEAD(&dp->headers);
	return dp;
}

/**
 * fdisk_ref_dump:
 * @dp: dump pointer
 *
 * Incremparts reference counter.
 */
void fdisk_ref_dump(struct fdisk_dump *dp)
{
	if (dp)
		dp->refcount++;
}

static void fdisk_reset_dump(struct fdisk_dump *dp)
{
	assert(dp);

	fdisk_unref_table(dp->table);
	dp->table = NULL;

	while (!list_empty(&dp->headers)) {
		struct fdisk_dumpheader *fi = list_entry(dp->headers.next,
						  struct fdisk_dumpheader, headers);
		fdisk_dump_free_header(dp, fi);
	}
	INIT_LIST_HEAD(&dp->headers);
}

/**
 * fdisk_unref_dump:
 * @dp: dump pointer
 *
 * De-incremparts reference counter, on zero the @dp is automatically
 * deallocated.
 */
void fdisk_unref_dump(struct fdisk_dump *dp)
{
	if (!dp)
		return;

	dp->refcount--;
	if (dp->refcount <= 0) {
		fdisk_reset_dump(dp);
		DBG(DUMP, ul_debugobj(dp, "free"));
		free(dp);
	}
}

static struct fdisk_dumpheader *fdisk_dump_get_header(struct fdisk_dump *dp,
						     const char *name)
{
	struct list_head *p;

	list_for_each(p, &dp->headers) {
		struct fdisk_dumpheader *fi = list_entry(p, struct fdisk_dumpheader, headers);

		if (strcasecmp(fi->name, name) == 0)
			return fi;
	}

	return NULL;
}

/**
 * fdisk_dump_set_header:
 * @dp: dump instance
 * @name: header name
 * @data: header data (or NULL)
 *
 * The headers are used as global options (in dump) for whole partition table, always one
 * header per line.
 *
 * If no @data specified then the header is removed. If header does not exist
 * and @data speified then a new header added.
 *
 * Note that libfdisk allows to specify arbitrary custom header, the default
 * build-in headers are "unit" and "label", and some label specific headers
 * (for example "uuid" and "name" for GPT).
 *
 * Returns: 0 on success, <0 on error
 */
int fdisk_dump_set_header(struct fdisk_dump *dp,
			    const char *name,
			    const char *data)
{
	struct fdisk_dumpheader *fi;

	assert(dp);
	assert(name);

	if (!dp || !name)
		return -EINVAL;

	fi = fdisk_dump_get_header(dp, name);
	if (!fi && !data)
		return 0;	/* want to remove header that does not exist, success */

	if (!data) {
		/* no data, remove the header */
		fdisk_dump_free_header(dp, fi);
		return 0;
	}

	if (!fi) {
		/* new header */
		fi = calloc(1, sizeof(*fi));
		if (!fi)
			return -ENOMEM;
		INIT_LIST_HEAD(&fi->headers);
		fi->name = strdup(name);
		fi->data = strdup(data);
		if (!fi->data || !fi->name) {
			fdisk_dump_free_header(dp, fi);
			return -ENOMEM;
		}
		list_add_tail(&fi->headers, &dp->headers);
	} else {
		/* update existing */
		char *x = strdup(data);

		if (!x)
			return -ENOMEM;
		free(fi->data);
		fi->data = x;
	}

	return 0;
}

/**
 * fdisk_dump_get_table:
 * @dp: dump
 *
 * The table (container with partitions) is possible to create by
 * fdisk_dump_read_context() or fdisk_dump_read_file(), otherwise
 * this function returns NULL.
 *
 * Returns: NULL or dump.
 */
struct fdisk_table *fdisk_dump_get_table(struct fdisk_dump *dp)
{
	assert(dp);
	return dp ? dp->table : NULL;
}

/**
 * fdisk_dump_read_context:
 * @dp: dump
 * @cxt: context
 *
 * Reads data from the current context (on disk partition table).
 *
 * Return: 0 on success, <0 on error.
 */
int fdisk_dump_read_context(struct fdisk_dump *dp, struct fdisk_context *cxt)
{
	struct fdisk_label *lb;
	int rc;
	char *p = NULL;

	assert(dp);
	assert(cxt);

	if (!dp || !cxt)
		return -EINVAL;

	fdisk_reset_dump(dp);

	lb = fdisk_get_label(cxt, NULL);
	if (!lb)
		return -EINVAL;

	/* allocate and fill new table */
	rc = fdisk_get_partitions(cxt, &dp->table);
	if (rc)
		return rc;

	/* generate headers */
	rc = fdisk_dump_set_header(dp, "label", fdisk_label_get_name(lb));

	if (!rc && fdisk_get_disklabel_id(cxt, &p) == 0 && p) {
		rc = fdisk_dump_set_header(dp, "label-id", p);
		free(p);
	}
	if (!rc && cxt->dev_path)
		rc = fdisk_dump_set_header(dp, "device", cxt->dev_path);
	if (!rc)
		rc = fdisk_dump_set_header(dp, "unit", "sectors");

	/* TODO: label specific headers (e.g. uuid for GPT) */

	return rc;
}

int fdisk_dump_write_file(struct fdisk_dump *dp, FILE *f)
{
	struct list_head *h;
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	const char *devname = NULL;

	assert(dp);
	assert(f);

	/* dump headers */
	list_for_each(h, &dp->headers) {
		struct fdisk_dumpheader *fi = list_entry(h, struct fdisk_dumpheader, headers);
		fprintf(f, "%s: %s\n", fi->name, fi->data);
		if (strcmp(fi->name, "device") == 0)
			devname = fi->data;
	}

	if (!dp->table)
		return 0;

	fputc('\n', f);

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(dp->table, &itr, &pa) == 0) {
		char *p = NULL;

		if (devname)
			p = fdisk_partname(devname, pa->partno + 1);
		if (p)
			fprintf(f, "%s: ", p);
		else
			fprintf(f, "%zu :", pa->partno + 1);

		if (pa->start)
			fprintf(f, " start=%12ju", pa->start);
		if (pa->size)
			fprintf(f, ", size=%12ju", pa->size);
		else if (pa->end)
			fprintf(f, ", end=%12ju", pa->end);

		if (pa->type && fdisk_parttype_get_string(pa->type))
			fprintf(f, ", type=%s", fdisk_parttype_get_string(pa->type));
		else if (pa->type)
			fprintf(f, ", type=%x", fdisk_parttype_get_code(pa->type));

		if (pa->uuid)
			fprintf(f, ", uuid=%s", pa->uuid);
		if (pa->name && *pa->name)
			fprintf(f, ", name=\"%s\"", pa->name);
		if (pa->attrs)
			fprintf(f, ", attrs=%s", pa->attrs);
		if (pa->boot)
			fprintf(f, ", bootable");
		fputc('\n', f);
	}

	return 0;
}

#ifdef TEST_PROGRAM
int test_dump(struct fdisk_test *ts, int argc, char *argv[])
{
	char *devname = argv[1];
	struct fdisk_context *cxt;
	struct fdisk_dump *dp;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, devname, 1);

	dp = fdisk_new_dump();
	fdisk_dump_read_context(dp, cxt);
	fdisk_dump_set_header(dp, "custom-header-foo", "bar");

	fdisk_free_context(cxt);

	fdisk_dump_write_file(dp, stdout);
	fdisk_unref_dump(dp);

	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
	{ "--dump",  test_dump,    "<device>   print PT" },
	{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
