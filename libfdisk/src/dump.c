
#include "fdiskP.h"
#include "strutils.h"

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
	size_t			nlines;
	int			fmt;		/* input format */
	struct fdisk_label	*label;
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

static struct fdisk_dumpheader *dump_get_header(struct fdisk_dump *dp,
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
 * fdisk_dump_get_header:
 * @dp: dump instance
 * @name: header name
 *
 * Returns: pointer to header data or NULL.
 */
const char *fdisk_dump_get_header(struct fdisk_dump *dp, const char *name)
{
	struct fdisk_dumpheader *fi;

	assert(dp);
	assert(name);

	fi = dump_get_header(dp, name);
	return fi ? fi->data : NULL;
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

	fi = dump_get_header(dp, name);
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

static struct fdisk_label *dump_get_label(struct fdisk_context *cxt, struct fdisk_dump *dp)
{
	assert(cxt);
	assert(dp);

	if (!dp->label) {
		dp->label = fdisk_get_label(cxt, fdisk_dump_get_header(dp, "label"));
		DBG(DUMP, ul_debug("label '%s'", dp->label ? dp->label->name : ""));
	}
	return dp->label;
}

/**
 * fdisk_dump_read_context:
 * @dp: dump
 * @cxt: context
 *
 * Reads data from the current context (on disk partition table) into the dump.
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

/**
 * fdisk_dump_write_file:
 * @dp: dump
 * @f: output file
 *
 * Writes dump @dp to the ile @f.
 *
 * Returns: 0 on sucess, <0 on error.
 */
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
			fprintf(f, "%s : ", p);
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

static inline int is_header_line(const char *s)
{
	const char *p = strchr(s, ':');

	if (!p || p == s || !*(p + 1) || strchr(s, '='))
		return 0;

	return 1;
}

/* parses "<name>: value", note modifies @s*/
static int parse_header_line(struct fdisk_dump *dp, char *s)
{
	int rc = -EINVAL;
	char *name, *value;

	DBG(DUMP, ul_debug("   parse header '%s'", s));

	if (!s || !*s)
		return -EINVAL;

	name = s;
	value = strchr(s, ':');
	if (!value)
		goto done;
	*value = '\0';
	value++;

	ltrim_whitespace((unsigned char *) name);
	rtrim_whitespace((unsigned char *) name);
	ltrim_whitespace((unsigned char *) value);
	rtrim_whitespace((unsigned char *) value);

	if (*name && *value)
		rc = fdisk_dump_set_header(dp, name, value);
done:
	if (rc)
		DBG(DUMP, ul_debug("header parse error: [rc=%d]", rc));
	return rc;

}

static int next_number(char **s, uint64_t *num)
{
	char *end = NULL;
	int rc;

	assert(num);
	assert(s);

	*s = (char *) skip_blank(*s);
	if (!**s)
		return -1;

	end = strchr(*s, ',');
	if (end)
		*end = '\0';

	rc = strtosize(*s, (uintmax_t *) num);
	if (end) {
		*end = ',';
		*s = end;
	} else
		while (**s) (*s)++;

	return rc;
}

static int next_string(char **s, char **str)
{
	char *end = NULL;

	assert(str);
	assert(s);

	*s = (char *) skip_blank(*s);
	if (!**s)
		return -1;

	end = strchr(*s, ',');
	if (end) {
		*str = strndup(*s, end - *s);
		*s = end;
	} else {
		*str = strdup(*s);
		while (**s) (*s)++;
	}

	if (!*str)
		return -ENOMEM;

	ltrim_whitespace((unsigned char *) *str);
	if (!**str) {
		free(*str);
		*str = NULL;
	}
	return 0;
}

static int partno_from_devname(char *s)
{
	int pno;
	size_t sz;
	char *end, *p;

	sz = rtrim_whitespace((unsigned char *)s);
	p = s + sz - 1;

	while (p > s && isdigit(*(p - 1)))
		p--;

	errno = 0;
	pno = strtol(p, &end, 10);
	if (errno || !end || p == end)
		return -1;
	return pno - 1;
}

static int parse_dump_line(struct fdisk_dump *dp, char *s,
			   struct fdisk_context *cxt)
{
	char *p;
	struct fdisk_partition *pa;
	int rc = 0;
	uint64_t num;
	int pno;

	assert(dp);
	assert(s);

	DBG(DUMP, ul_debug("   parse dump line: '%s'", s));

	pa = fdisk_new_partition();
	if (!pa)
		return -ENOMEM;

	p = strchr(s, ':');
	if (!p)
		return -EINVAL;
	*p = '\0';
	p++;

	pno = partno_from_devname(s);
	if (pno < 0)
		fdisk_partition_partno_follow_default(pa, 1);
	else
		fdisk_partition_set_partno(pa, pno);


	while (rc == 0 && p && *p) {
		while (isblank(*p)) p++;
		if (!*p)
			break;

		if (!strncasecmp(p, "start=", 6)) {
			p += 6;
			rc = next_number(&p, &num);
			if (!rc)
				fdisk_partition_set_start(pa, num);

		} else if (!strncasecmp(p, "size=", 5)) {
			p += 5;
			rc = next_number(&p, &num);
			if (!rc)
				fdisk_partition_set_size(pa, num);

		} else if (!strncasecmp(p, "end=", 4)) {
			p += 4;
			rc = next_number(&p, &num);
			if (!rc)
				fdisk_partition_set_end(pa, num);

		} else if (!strncasecmp(p, "bootable", 8)) {
			p += 8;
			pa->boot = 1;

		} else if (!strncasecmp(p, "attrs=", 6)) {
			p += 6;
			rc = next_string(&p, &pa->attrs);
			if (rc)
				break;

		} else if (!strncasecmp(p, "uuid=", 5)) {
			p += 5;
			rc = next_string(&p, &pa->uuid);
			if (rc)
				break;

		} else if (!strncasecmp(p, "name=", 5)) {
			p += 5;
			rc = next_string(&p, &pa->name);
			if (rc)
				break;

		} else if (!strncasecmp(p, "type=", 5)) {
			char *type;
			p += 5;

			rc = next_string(&p, &type);
			if (rc)
				break;
			pa->type = fdisk_label_parse_parttype(
					dump_get_label(cxt, dp), type);
			free(type);

			if (!pa->type || fdisk_parttype_is_unknown(pa->type)) {
				rc = -EINVAL;
				fdisk_free_parttype(pa->type);
				pa->type = NULL;
				break;
			}

		} else {
			DBG(DUMP, ul_debug("dump parse error: unknown field '%s'", p));
			rc = -EINVAL;
			break;
		}

		while (isblank(*p)) p++;
		if (*p == ',')
			p++;
	}

	if (!rc)
		rc = fdisk_table_add_partition(dp->table, pa);
	if (rc)
		DBG(DUMP, ul_debug("dump parse error: [rc=%d]", rc));

	fdisk_unref_partition(pa);
	return rc;
}

static int parse_commas_line(struct fdisk_dump *dp, const char *s,
			     struct fdisk_context *cxt)
{
	DBG(DUMP, ul_debug("   commas line parse error"));
	return -EINVAL;
}

/* modifies @s ! */
int fdisk_dump_read_buffer(struct fdisk_dump *dp, char *s,
			   struct fdisk_context *cxt)
{
	int rc = 0;

	assert(dp);
	assert(s);

	DBG(DUMP, ul_debug("  parsing buffer"));

	s = (char *) skip_blank(s);
	if (!s || !*s)
		return 0;	/* nothing baby, ignore */

	if (!dp->table) {
		dp->table = fdisk_new_table();
		if (!dp->table)
			return -ENOMEM;
	}

	/* parse header lines only if no partition specified yet */
	if (fdisk_table_is_empty(dp->table) && is_header_line(s))
		rc = parse_header_line(dp, s);

	/* parse dump format */
	else if (strchr(s, '='))
		rc = parse_dump_line(dp, s, cxt);

	/* parse simple <value>, ... format */
	else
		rc = parse_commas_line(dp, s, cxt);

	if (rc)
		DBG(DUMP, ul_debugobj(dp, "%zu: parse error [rc=%d]",
				dp->nlines, rc));
	return rc;
}

char fdisk_dump_read_line(struct fdisk_dump *dp, FILE *f,
			  struct fdisk_context *cxt)
{
	char buf[BUFSIZ];
	char *s;

	assert(dp);
	assert(f);

	DBG(DUMP, ul_debug(" parsing line"));

	/* read the next non-blank non-comment line */
	do {
		if (fgets(buf, sizeof(buf), f) == NULL)
			return -EINVAL;
		dp->nlines++;
		s = strchr(buf, '\n');
		if (!s) {
			/* Missing final newline?  Otherwise an extremely */
			/* long line - assume file was corrupted */
			if (feof(f)) {
				DBG(DUMP, ul_debugobj(dp, "no final newline"));
				s = strchr(buf, '\0');
			} else {
				DBG(DUMP, ul_debugobj(dp,
					"%zu: missing newline at line", dp->nlines));
				return -EINVAL;
			}
		}

		*s = '\0';
		if (--s >= buf && *s == '\r')
			*s = '\0';
		s = (char *) skip_blank(buf);
	} while (*s == '\0' || *s == '#');

	return fdisk_dump_read_buffer(dp, s, cxt);
}


/**
 * fdisk_dump_read_file:
 * @dp: dump
 * @f input file
 * @cxt: context
 *
 * Reads file @f into dump @dp. The @cxt is never modified by this function,
 * it's used to parse label specific stuff (context contais pointers to all
 * enebled labels).
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_dump_read_file(struct fdisk_dump *dp, FILE *f,
			 struct fdisk_context *cxt)
{
	int rc = NULL;

	assert(dp);
	assert(f);

	DBG(DUMP, ul_debug("parsing file"));

	while (!feof(f)) {
		rc = fdisk_dump_read_line(dp, f, cxt);
		if (rc)
			break;
	}

	return rc;
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

int test_read(struct fdisk_test *ts, int argc, char *argv[])
{
	char *filename = argv[1];
	struct fdisk_dump *dp;
	struct fdisk_context *cxt;
	FILE *f;

	if (!(f = fopen(filename, "r")))
		err(EXIT_FAILURE, "%s: cannot open", filename);

	cxt = fdisk_new_context();
	dp = fdisk_new_dump();

	fdisk_dump_read_file(dp, f, cxt);
	fclose(f);

	fdisk_dump_write_file(dp, stdout);
	fdisk_unref_dump(dp);
	fdisk_free_context(cxt);

	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
	{ "--dump",  test_dump,    "<device>   print PT" },
	{ "--read",  test_read,    "<file>     read PT scrit from file" },
	{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
