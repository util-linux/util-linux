
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

	DBG(SCRIPT, ul_debugobj(fi, "free header %s", fi->name));
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

	DBG(SCRIPT, ul_debugobj(dp, "alloc"));
	dp->refcount = 1;
	dp->cxt = cxt;
	fdisk_ref_context(cxt);

	dp->table = fdisk_new_table();
	if (!dp->table) {
		fdisk_unref_script(dp);
		return NULL;
	}

	INIT_LIST_HEAD(&dp->headers);
	return dp;
}

/**
 * fdisk_new_script_from_file:
 * @cxt: context
 * @filename: path to the script file
 *
 * Allocates a new script and reads script from @filename.
 *
 * Returns: new script instance or NULL in case of error (check errno for more details).
 */
struct fdisk_script *fdisk_new_script_from_file(struct fdisk_context *cxt,
						 const char *filename)
{
	int rc;
	FILE *f;
	struct fdisk_script *dp, *res = NULL;

	assert(cxt);
	assert(filename);

	f = fopen(filename, "r");
	if (!f)
		return NULL;

	dp = fdisk_new_script(cxt);
	if (!dp)
		goto done;

	rc = fdisk_script_read_file(dp, f);
	if (rc) {
		errno = -rc;
		goto done;
	}

	res = dp;
done:
	fclose(f);
	if (!res)
		fdisk_unref_script(dp);
	else
		errno = 0;

	return res;
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
		DBG(SCRIPT, ul_debugobj(dp, "free script"));
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
 * The headers are used as global options (in script) for whole partition
 * table, always one header per line.
 *
 * If no @data specified then the header is removed. If header does not exist
 * and @data specified then a new header added.
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

	if (strcmp(name, "label") == 0)
		dp->label = NULL;

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
		DBG(SCRIPT, ul_debugobj(dp, "label '%s'", dp->label ? dp->label->name : ""));
	}
	return dp->label;
}

/**
 * fdisk_script_get_nlines:
 * @dp: script
 *
 * Returns: number of parsed lines or <0 on error.
 */
int fdisk_script_get_nlines(struct fdisk_script *dp)
{
	assert(dp);
	return dp->nlines;
}

/**
 * fdisk_script_read_context:
 * @dp: script
 * @cxt: context
 *
 * Reads data from the @cxt context (on disk partition table) into the script.
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
 * Returns: 0 on success, <0 on error.
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

	DBG(SCRIPT, ul_debugobj(dp, "   parse header '%s'", s));

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
		DBG(SCRIPT, ul_debugobj(dp, "header parse error: [rc=%d]", rc));
	return rc;

}

static char *next_separator(char *s)
{
	char *end;

	if ((end = strchr(s, ',')) ||
	    (end = strchr(s, ';')) ||
	    (end = strchr(s, ' ')))
		return end;

	return NULL;
}

static int next_number(char **s, uint64_t *num, int *power)
{
	char *end = NULL;
	int rc;

	assert(num);
	assert(s);

	*s = (char *) skip_blank(*s);
	if (!**s)
		return -1;

	end = next_separator(*s);
	if (end)
		*end = '\0';

	rc = parse_size(*s, (uintmax_t *) num, power);
	if (end)
		*s = ++end;
	else
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
		end = next_separator(xend);
	} else
		xend = end = next_separator(*s);

	if (xend) {
		*str = strndup(*s, xend - *s);
		*s = end ? end + 1 : xend + 1;
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

/* dump format
 * <device>: start=<num>, size=<num>, type=<string>, ...
 */
static int parse_script_line(struct fdisk_script *dp, char *s)
{
	char *p, *x;
	struct fdisk_partition *pa;
	int rc = 0;
	uint64_t num;
	int pno;

	assert(dp);
	assert(s);

	DBG(SCRIPT, ul_debugobj(dp, "   parse script line: '%s'", s));

	pa = fdisk_new_partition();
	if (!pa)
		return -ENOMEM;

	fdisk_partition_start_follow_default(pa, 1);
	fdisk_partition_end_follow_default(pa, 1);
	fdisk_partition_partno_follow_default(pa, 1);

	/* set partno */
	p = strchr(s, ':');
	x = strchr(s, '=');
	if (p && (!x || p < x)) {
		*p = '\0';
		p++;

		pno = partno_from_devname(s);
		if (pno >= 0) {
			fdisk_partition_partno_follow_default(pa, 0);
			fdisk_partition_set_partno(pa, pno);
		}
	} else
		p = s;

	while (rc == 0 && p && *p) {
		while (isblank(*p)) p++;
		if (!*p)
			break;

		DBG(SCRIPT, ul_debugobj(dp, " parsing '%s'", p));

		if (!strncasecmp(p, "start=", 6)) {
			p += 6;
			rc = next_number(&p, &num, NULL);
			if (!rc) {
				fdisk_partition_set_start(pa, num);
				fdisk_partition_start_follow_default(pa, 0);
			}
		} else if (!strncasecmp(p, "size=", 5)) {
			int pow = 0;

			p += 5;
			rc = next_number(&p, &num, &pow);
			if (!rc) {
				if (pow)
					num /= dp->cxt->sector_size;
				fdisk_partition_set_size(pa, num);
				fdisk_partition_end_follow_default(pa, 0);
			}

		} else if (!strncasecmp(p, "bootable", 8)) {
			char *x;

			p += 8;
			x = next_separator(p);
			if (x)
				p = x + 1;
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

			p += (*p == 'I' ? 3 : 5);		/* "Id=" or "type=" */

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
			DBG(SCRIPT, ul_debugobj(dp, "script parse error: unknown field '%s'", p));
			rc = -EINVAL;
			break;
		}
	}

	if (!rc)
		rc = fdisk_table_add_partition(dp->table, pa);
	if (rc)
		DBG(SCRIPT, ul_debugobj(dp, "script parse error: [rc=%d]", rc));

	fdisk_unref_partition(pa);
	return rc;
}

/* original sfdisk supports partition types shortcuts like 'L' = Linux native
 */
static struct fdisk_parttype *translate_type_shortcuts(struct fdisk_script *dp, char *str)
{
	struct fdisk_label *lb;
	const char *type = NULL;

	if (strlen(str) != 1)
		return NULL;

	lb = script_get_label(dp);
	if (!lb)
		return NULL;

	if (lb->id == FDISK_DISKLABEL_DOS) {
		switch (*str) {
		case 'L':	/* Linux */
			type = "83";
			break;
		case 'S':	/* Swap */
			type = "82";
			break;
		case 'E':	/* Dos extended */
			type = "05";
			break;
		case 'X':	/* Linux extended */
			type = "85";
			break;
		}
	} else if (lb->id == FDISK_DISKLABEL_GPT) {
		switch (*str) {
		case 'L':	/* Linux */
			type = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
			break;
		case 'S':	/* Swap */
			type = "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F";
			break;
		case 'H':	/* Home */
			type = "933AC7E1-2EB4-4F13-B844-0E14E2AEF915";
			break;
		}
	}

	return type ? fdisk_label_parse_parttype(lb, type) : NULL;
}

/* simple format:
 * <start>, <size>, <type>, <bootable>, ...
 */
static int parse_commas_line(struct fdisk_script *dp, char *s)
{
	int rc = 0;
	char *p = s, *str;
	struct fdisk_partition *pa;
	enum { ITEM_START, ITEM_SIZE, ITEM_TYPE, ITEM_BOOTABLE };
	int item = -1;

	assert(dp);
	assert(s);

	pa = fdisk_new_partition();
	if (!pa)
		return -ENOMEM;

	fdisk_partition_start_follow_default(pa, 1);
	fdisk_partition_end_follow_default(pa, 1);
	fdisk_partition_partno_follow_default(pa, 1);

	while (rc == 0 && p && *p) {
		uint64_t num;
		char *begin;

		while (isblank(*p)) p++;
		if (!*p)
			break;
		item++;

		DBG(SCRIPT, ul_debugobj(dp, " parsing item %d ('%s')", item, p));
		begin = p;

		switch (item) {
		case ITEM_START:
			if (*p == ',' || *p == ';')
				fdisk_partition_start_follow_default(pa, 1);
			else {
				rc = next_number(&p, &num, NULL);
				if (!rc)
					fdisk_partition_set_start(pa, num);
				fdisk_partition_start_follow_default(pa, 0);
			}
			break;
		case ITEM_SIZE:
			if (*p == ',' || *p == ';' || *p == '+')
				fdisk_partition_end_follow_default(pa, 1);
			else {
				int pow = 0;
				rc = next_number(&p, &num, &pow);
				if (!rc) {
					if (pow)
						num /= dp->cxt->sector_size;
					fdisk_partition_set_size(pa, num);
				}
				fdisk_partition_end_follow_default(pa, 0);
			}
			break;
		case ITEM_TYPE:
			if (*p == ',' || *p == ';')
				break;	/* use default type */

			rc = next_string(&p, &str);
			if (rc)
				break;

			pa->type = translate_type_shortcuts(dp, str);
			if (!pa->type)
				pa->type = fdisk_label_parse_parttype(
						script_get_label(dp), str);
			free(str);

			if (!pa->type || fdisk_parttype_is_unknown(pa->type)) {
				rc = -EINVAL;
				fdisk_free_parttype(pa->type);
				pa->type = NULL;
				break;
			}
			break;
		case ITEM_BOOTABLE:
			if (*p == ',')
				break;
			if (*p == '*' &&
			    (!*(p + 1) || *(p + 1) == ' ' || *(p + 1) == ',' || *(p + 1) == ';')) {
				pa->boot = 1;
				p++;
			} else if (*p == '-' &&
			    (!*(p + 1) || *(p + 1) == ' ' || *(p + 1) == ',' || *(p + 1) == ';')) {
				pa->boot = 0;
				p++;
			} else
				rc = -EINVAL;
			break;
		default:
			break;
		}

		if (begin == p)
			p++;
	}

	if (!rc)
		rc = fdisk_table_add_partition(dp->table, pa);
	if (rc)
		DBG(SCRIPT, ul_debugobj(dp, "script parse error: [rc=%d]", rc));

	fdisk_unref_partition(pa);
	return rc;
}

/* modifies @s ! */
int fdisk_script_read_buffer(struct fdisk_script *dp, char *s)
{
	int rc = 0;

	assert(dp);
	assert(s);

	DBG(SCRIPT, ul_debugobj(dp, "  parsing buffer"));

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
		DBG(SCRIPT, ul_debugobj(dp, "%zu: parse error [rc=%d]",
				dp->nlines, rc));
	return rc;
}

/**
 * fdisk_script_read_line:
 * @dp: script
 * @f: file
 *
 * Reads next line into dump.
 *
 * Returns: 0 on success, <0 on error, 1 when nothing to read.
 */
int fdisk_script_read_line(struct fdisk_script *dp, FILE *f, char *buf, size_t bufsz)
{
	char *s;

	assert(dp);
	assert(f);

	DBG(SCRIPT, ul_debugobj(dp, " parsing line"));

	/* read the next non-blank non-comment line */
	do {
		if (fgets(buf, bufsz, f) == NULL)
			return 1;
		dp->nlines++;
		s = strchr(buf, '\n');
		if (!s) {
			/* Missing final newline?  Otherwise an extremely */
			/* long line - assume file was corrupted */
			if (feof(f)) {
				DBG(SCRIPT, ul_debugobj(dp, "no final newline"));
				s = strchr(buf, '\0');
			} else {
				DBG(SCRIPT, ul_debugobj(dp,
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
	char buf[BUFSIZ];
	int rc = NULL;

	assert(dp);
	assert(f);

	DBG(SCRIPT, ul_debugobj(dp, "parsing file"));

	while (!feof(f)) {
		rc = fdisk_script_read_line(dp, f, buf, sizeof(buf));
		if (rc)
			break;
	}

	return rc;
}

/**
 * fdisk_set_script:
 * @cxt: context
 * @dp: script (or NULL to remove previous reference)
 *
 * Sets reference to the @dp script. The script headers might be used by label
 * drivers to overwrite built-in defaults (for example disk label Id) and label
 * driver might optimize the default semantic to be more usable for scripts
 * (for example to not ask for primary/logical/extended partition type).
 *
 * Note that script also contains reference to the fdisk context (see
 * fdisk_new_script()). This context may be completely independent on
 * context used for fdisk_set_script().
 *
 * Returns: <0 on error, 0 on success.
 */
int fdisk_set_script(struct fdisk_context *cxt, struct fdisk_script *dp)
{
	assert(cxt);

	/* unref old */
	if (cxt->script)
		fdisk_unref_script(cxt->script);

	/* ref new */
	cxt->script = dp;
	if (cxt->script) {
		DBG(CXT, ul_debugobj(cxt, "setting reference to script %p", cxt->script));
		fdisk_ref_script(cxt->script);
	}

	return 0;
}

/**
 * fdisk_get_script:
 * @cxt: context
 *
 * Returns: the current script or NULL.
 */
struct fdisk_script *fdisk_get_script(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->script;
}

/**
 * fdisk_apply_script_headers:
 * @cxt: context
 * dp: script
 *
 * Associte context @cxt with script @dp and creates a new empty disklabel.
 *
 * Return: 0 on success, <0 on error.
 */
int fdisk_apply_script_headers(struct fdisk_context *cxt, struct fdisk_script *dp)
{
	const char *name;

	assert(cxt);
	assert(dp);

	DBG(SCRIPT, ul_debugobj(dp, "applying script headers"));
	fdisk_set_script(cxt, dp);

	/* create empty label */
	name = fdisk_script_get_header(dp, "label");
	if (!name)
		return -EINVAL;

	return fdisk_create_disklabel(cxt, name);
}

/**
 * fdisk_apply_script:
 * @cxt: context
 * @dp: script
 *
 * This function creates a new disklabel and partition within context @cxt. You
 * have to call fdisk_write_disklabel() to apply changes to the device.
 *
 * Returns: 0 on error, <0 on error.
 */
int fdisk_apply_script(struct fdisk_context *cxt, struct fdisk_script *dp)
{
	int rc;
	struct fdisk_script *old;

	assert(dp);
	assert(cxt);

	DBG(CXT, ul_debugobj(cxt, "appling script %p", dp));

	old = fdisk_get_script(cxt);

	/* create empty disk label */
	rc = fdisk_apply_script_headers(cxt, dp);

	/* create partitions */
	if (!rc && dp->table)
		rc = fdisk_apply_table(cxt, dp->table);

	fdisk_set_script(cxt, old);
	DBG(CXT, ul_debugobj(cxt, "script done [rc=%d]", rc));
	return rc;
}

#ifdef TEST_PROGRAM
int test_dump(struct fdisk_test *ts, int argc, char *argv[])
{
	char *devname = argv[1];
	struct fdisk_context *cxt;
	struct fdisk_script *dp;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, devname, 1);

	dp = fdisk_new_script(cxt);
	fdisk_script_read_context(dp, NULL);

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

int test_stdin(struct fdisk_test *ts, int argc, char *argv[])
{
	struct fdisk_script *dp;
	struct fdisk_context *cxt;
	int rc = 0;

	cxt = fdisk_new_context();
	dp = fdisk_new_script(cxt);
	fdisk_script_set_header(dp, "label", "dos");

	printf("<start>, <size>, <type>, <bootable: *|->\n");
	do {
		struct fdisk_partition *pa;
		size_t n = fdisk_table_get_nents(dp->table);

		printf(" #%zu :\n", n + 1);
		rc = fdisk_script_read_line(dp, stdin);

		if (rc == 0) {
			pa = fdisk_table_get_partition(dp->table, n);
			printf(" #%zu  %12ju %12ju\n",	n + 1,
						fdisk_partition_get_start(pa),
						fdisk_partition_get_size(pa));
		}
	} while (rc == 0);

	if (!rc)
		fdisk_script_write_file(dp, stdout);
	fdisk_unref_script(dp);
	fdisk_unref_context(cxt);

	return rc;
}

int test_apply(struct fdisk_test *ts, int argc, char *argv[])
{
	char *devname = argv[1], *scriptname = argv[2];
	struct fdisk_context *cxt;
	struct fdisk_script *dp = NULL;
	struct fdisk_table *tb = NULL;
	struct fdisk_iter *itr = NULL;
	struct fdisk_partition *pa = NULL;
	int rc;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, devname, 0);

	dp = fdisk_new_script_from_file(cxt, scriptname);
	if (!dp)
		return -errno;

	rc = fdisk_apply_script(cxt, dp);
	if (rc)
		goto done;
	fdisk_unref_script(dp);

	/* list result */
	fdisk_list_disklabel(cxt);
	fdisk_get_partitions(cxt, &tb);

	itr = fdisk_new_iter(FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
		printf(" #%zu  %12ju %12ju\n",	fdisk_partition_get_partno(pa),
						fdisk_partition_get_start(pa),
						fdisk_partition_get_size(pa));
	}

done:
	fdisk_free_iter(itr);
	fdisk_unref_table(tb);

	/*fdisk_write_disklabel(cxt);*/
	fdisk_unref_context(cxt);
	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
	{ "--dump",    test_dump,    "<device>            dump PT as script" },
	{ "--read",    test_read,    "<file>              read PT script from file" },
	{ "--apply",   test_apply,   "<device> <file>     try apply script from file to device" },
	{ "--stdin",   test_stdin,   "                    read input like sfdisk" },
	{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
