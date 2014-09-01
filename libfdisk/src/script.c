
#include "fdiskP.h"
#include "strutils.h"

/* script header (e.g. unit: sectors) */
struct fdisk_scriptheader {
	struct list_head	headers;
	char			*name;
	char			*data;
};

/* script control struct */
struct fdisk_script {
	struct fdisk_table	*table;
	struct list_head	headers;
	struct fdisk_context	*cxt;

	int			refcount;

	/* parser's state */
	size_t			nlines;
	int			fmt;		/* input format */
	struct fdisk_label	*label;
};


static void fdisk_script_free_header(struct fdisk_script *dp, struct fdisk_scriptheader *fi)
{
	if (!fi)
		return;

	DBG(DUMP, ul_debugobj(fi, "free header %s", fi->name));
	free(fi->name);
	free(fi->data);
	list_del(&fi->headers);
	free(fi);
}

/**
 * fdisk_new_script:
 *
 * The script hold fdisk_table and additional information to read/write
 * script to the file.
 *
 * Returns: newly allocated script struct.
 */
struct fdisk_script *fdisk_new_script(struct fdisk_context *cxt)
{
	struct fdisk_script *dp = NULL;

	dp = calloc(1, sizeof(*dp));
	if (!dp)
		return NULL;

	DBG(DUMP, ul_debugobj(dp, "alloc"));
	dp->refcount = 1;
	dp->cxt = cxt;
	fdisk_ref_context(cxt);

	INIT_LIST_HEAD(&dp->headers);
	return dp;
}

/**
 * fdisk_ref_script:
 * @dp: script pointer
 *
 * Incremparts reference counter.
 */
void fdisk_ref_script(struct fdisk_script *dp)
{
	if (dp)
		dp->refcount++;
}

static void fdisk_reset_script(struct fdisk_script *dp)
{
	assert(dp);

	fdisk_unref_table(dp->table);
	dp->table = NULL;

	while (!list_empty(&dp->headers)) {
		struct fdisk_scriptheader *fi = list_entry(dp->headers.next,
						  struct fdisk_scriptheader, headers);
		fdisk_script_free_header(dp, fi);
	}
	INIT_LIST_HEAD(&dp->headers);
}

/**
 * fdisk_unref_script:
 * @dp: script pointer
 *
 * De-incremparts reference counter, on zero the @dp is automatically
 * deallocated.
 */
void fdisk_unref_script(struct fdisk_script *dp)
{
	if (!dp)
		return;

	dp->refcount--;
	if (dp->refcount <= 0) {
		fdisk_reset_script(dp);
		fdisk_unref_context(dp->cxt);
		DBG(DUMP, ul_debugobj(dp, "free script"));
		free(dp);
	}
}

static struct fdisk_scriptheader *script_get_header(struct fdisk_script *dp,
						     const char *name)
{
	struct list_head *p;

	list_for_each(p, &dp->headers) {
		struct fdisk_scriptheader *fi = list_entry(p, struct fdisk_scriptheader, headers);

		if (strcasecmp(fi->name, name) == 0)
			return fi;
	}

	return NULL;
}

/**
 * fdisk_script_get_header:
 * @dp: script instance
 * @name: header name
 *
 * Returns: pointer to header data or NULL.
 */
const char *fdisk_script_get_header(struct fdisk_script *dp, const char *name)
{
	struct fdisk_scriptheader *fi;

	assert(dp);
	assert(name);

	fi = script_get_header(dp, name);
	return fi ? fi->data : NULL;
}


/**
 * fdisk_script_set_header:
 * @dp: script instance
 * @name: header name
 * @data: header data (or NULL)
 *
 * The headers are used as global options (in script) for whole partition table, always one
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
int fdisk_script_set_header(struct fdisk_script *dp,
			    const char *name,
			    const char *data)
{
	struct fdisk_scriptheader *fi;

	assert(dp);
	assert(name);

	if (!dp || !name)
		return -EINVAL;

	fi = script_get_header(dp, name);
	if (!fi && !data)
		return 0;	/* want to remove header that does not exist, success */

	if (!data) {
		/* no data, remove the header */
		fdisk_script_free_header(dp, fi);
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
			fdisk_script_free_header(dp, fi);
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
 * fdisk_script_get_table:
 * @dp: script
 *
 * The table (container with partitions) is possible to create by
 * fdisk_script_read_context() or fdisk_script_read_file(), otherwise
 * this function returns NULL.
 *
 * Returns: NULL or script.
 */
struct fdisk_table *fdisk_script_get_table(struct fdisk_script *dp)
{
	assert(dp);
	return dp ? dp->table : NULL;
}

static struct fdisk_label *script_get_label(struct fdisk_script *dp)
{
	assert(dp);
	assert(dp->cxt);

	if (!dp->label) {
		dp->label = fdisk_get_label(dp->cxt,
					fdisk_script_get_header(dp, "label"));
		DBG(DUMP, ul_debug("label '%s'", dp->label ? dp->label->name : ""));
	}
	return dp->label;
}

/**
 * fdisk_script_read_context:
 * @dp: script
 * @cxt: context
 *
 * Reads data from the current context (on disk partition table) into the script.
 * If the context is no specified than defaults to context used for fdisk_new_script().
 *
 * Return: 0 on success, <0 on error.
 */
int fdisk_script_read_context(struct fdisk_script *dp, struct fdisk_context *cxt)
{
	struct fdisk_label *lb;
	int rc;
	char *p = NULL;

	assert(dp);

	if (!cxt)
		cxt = dp->cxt;

	if (!dp || !cxt)
		return -EINVAL;

	fdisk_reset_script(dp);

	lb = fdisk_get_label(cxt, NULL);
	if (!lb)
		return -EINVAL;

	/* allocate and fill new table */
	rc = fdisk_get_partitions(cxt, &dp->table);
	if (rc)
		return rc;

	/* generate headers */
	rc = fdisk_script_set_header(dp, "label", fdisk_label_get_name(lb));

	if (!rc && fdisk_get_disklabel_id(cxt, &p) == 0 && p) {
		rc = fdisk_script_set_header(dp, "label-id", p);
		free(p);
	}
	if (!rc && cxt->dev_path)
		rc = fdisk_script_set_header(dp, "device", cxt->dev_path);
	if (!rc)
		rc = fdisk_script_set_header(dp, "unit", "sectors");

	/* TODO: label specific headers (e.g. uuid for GPT) */

	return rc;
}

/**
 * fdisk_script_write_file:
 * @dp: script
 * @f: output file
 *
 * Writes script @dp to the ile @f.
 *
 * Returns: 0 on sucess, <0 on error.
 */
int fdisk_script_write_file(struct fdisk_script *dp, FILE *f)
{
	struct list_head *h;
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	const char *devname = NULL;

	assert(dp);
	assert(f);

	/* script headers */
	list_for_each(h, &dp->headers) {
		struct fdisk_scriptheader *fi = list_entry(h, struct fdisk_scriptheader, headers);
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
			fprintf(f, ", attrs=\"%s\"", pa->attrs);
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
static int parse_header_line(struct fdisk_script *dp, char *s)
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
		rc = fdisk_script_set_header(dp, name, value);
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
	char *xend = NULL, *end = NULL;

	assert(str);
	assert(s);

	*s = (char *) skip_blank(*s);
	if (!**s)
		return -1;

	if (**s == '"') {
		++(*s);
		xend = strchr(*s, '"');
		if (!xend)
			return -EINVAL;
		end = strchr(xend, ',');
	} else
		xend = end = strchr(*s, ',');

	if (xend) {
		*str = strndup(*s, xend - *s);
		*s = end ? end : xend + 1;
	} else {
		*str = strdup(*s);
		while (**s) (*s)++;
	}

	if (!*str)
		return -ENOMEM;

	if (xend == end)
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

static int parse_script_line(struct fdisk_script *dp, char *s)
{
	char *p;
	struct fdisk_partition *pa;
	int rc = 0;
	uint64_t num;
	int pno;

	assert(dp);
	assert(s);

	DBG(DUMP, ul_debug("   parse script line: '%s'", s));

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

		} else if (!strncasecmp(p, "type=", 5) ||
			   !strncasecmp(p, "Id=", 3)) {		/* backward compatiility */
			char *type;
			p += 5;

			rc = next_string(&p, &type);
			if (rc)
				break;
			pa->type = fdisk_label_parse_parttype(
					script_get_label(dp), type);
			free(type);

			if (!pa->type || fdisk_parttype_is_unknown(pa->type)) {
				rc = -EINVAL;
				fdisk_free_parttype(pa->type);
				pa->type = NULL;
				break;
			}

		} else {
			DBG(DUMP, ul_debug("script parse error: unknown field '%s'", p));
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
		DBG(DUMP, ul_debug("script parse error: [rc=%d]", rc));

	fdisk_unref_partition(pa);
	return rc;
}

static int parse_commas_line(struct fdisk_script *dp, const char *s)
{
	DBG(DUMP, ul_debug("   commas line parse error"));
	return -EINVAL;
}

/* modifies @s ! */
int fdisk_script_read_buffer(struct fdisk_script *dp, char *s)
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

	/* parse script format */
	else if (strchr(s, '='))
		rc = parse_script_line(dp, s);

	/* parse simple <value>, ... format */
	else
		rc = parse_commas_line(dp, s);

	if (rc)
		DBG(DUMP, ul_debugobj(dp, "%zu: parse error [rc=%d]",
				dp->nlines, rc));
	return rc;
}

char fdisk_script_read_line(struct fdisk_script *dp, FILE *f)
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

	return fdisk_script_read_buffer(dp, s);
}


/**
 * fdisk_script_read_file:
 * @dp: script
 * @f input file
 *
 * Reads file @f into script @dp.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_script_read_file(struct fdisk_script *dp, FILE *f)
{
	int rc = NULL;

	assert(dp);
	assert(f);

	DBG(DUMP, ul_debug("parsing file"));

	while (!feof(f)) {
		rc = fdisk_script_read_line(dp, f);
		if (rc)
			break;
	}

	return rc;
}


#ifdef TEST_PROGRAM
int test_script(struct fdisk_test *ts, int argc, char *argv[])
{
	char *devname = argv[1];
	struct fdisk_context *cxt;
	struct fdisk_script *dp;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, devname, 1);

	dp = fdisk_new_script(cxt);
	fdisk_script_read_context(dp, NULL);
	fdisk_script_set_header(dp, "custom-header-foo", "bar");

	fdisk_script_write_file(dp, stdout);
	fdisk_unref_script(dp);
	fdisk_unref_context(cxt);

	return 0;
}

int test_read(struct fdisk_test *ts, int argc, char *argv[])
{
	char *filename = argv[1];
	struct fdisk_script *dp;
	struct fdisk_context *cxt;
	FILE *f;

	if (!(f = fopen(filename, "r")))
		err(EXIT_FAILURE, "%s: cannot open", filename);

	cxt = fdisk_new_context();
	dp = fdisk_new_script(cxt);

	fdisk_script_read_file(dp, f);
	fclose(f);

	fdisk_script_write_file(dp, stdout);
	fdisk_unref_script(dp);
	fdisk_unref_context(cxt);

	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
	{ "--script",  test_script,  "<device>   print PT" },
	{ "--read",    test_read,    "<file>     read PT scrit from file" },
	{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
