
#include "fdiskP.h"
#include "strutils.h"
#include "carefulputc.h"
#include "mangle.h"

/**
 * SECTION: script
 * @title: Script
 * @short_description: text based sfdisk compatible description of partition table
 *
 * The libfdisk scripts are based on original sfdisk script (dumps).  Each
 * script has two parts: script headers and partition table entries
 * (partitions).
 *
 * For more details about script format see sfdisk man page.
 */

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
	char			*(*fn_fgets)(struct fdisk_script *, char *, size_t, FILE *);
	void			*userdata;

	/* parser's state */
	size_t			nlines;
	struct fdisk_label	*label;

	unsigned int		json : 1,		/* JSON output */
				force_label : 1;	/* label: <name> specified */
};


static void fdisk_script_free_header(struct fdisk_scriptheader *fi)
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
 * @cxt: context
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

	DBG(SCRIPT, ul_debug("opening %s", filename));
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
 * Increments reference counter.
 */
void fdisk_ref_script(struct fdisk_script *dp)
{
	if (dp)
		dp->refcount++;
}

static void fdisk_reset_script(struct fdisk_script *dp)
{
	assert(dp);

	DBG(SCRIPT, ul_debugobj(dp, "reset"));
	fdisk_unref_table(dp->table);
	dp->table = NULL;

	while (!list_empty(&dp->headers)) {
		struct fdisk_scriptheader *fi = list_entry(dp->headers.next,
						  struct fdisk_scriptheader, headers);
		fdisk_script_free_header(fi);
	}
	INIT_LIST_HEAD(&dp->headers);
}

/**
 * fdisk_unref_script:
 * @dp: script pointer
 *
 * Decrements reference counter, on zero the @dp is automatically
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

/**
 * fdisk_script_set_userdata
 * @dp: script
 * @data: your data
 *
 * Sets data usable for example in callbacks (e.g fdisk_script_set_fgets()).
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_script_set_userdata(struct fdisk_script *dp, void *data)
{
	assert(dp);
	dp->userdata = data;
	return 0;
}

/**
 * fdisk_script_get_userdata
 * @dp: script
 *
 * Returns: user data or NULL.
 */
void *fdisk_script_get_userdata(struct fdisk_script *dp)
{
	assert(dp);
	return dp->userdata;
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
 * The headers are used as global options for whole partition
 * table, always one header per line.
 *
 * If no @data is specified then the header is removed. If header does not exist
 * and @data is specified then a new header is added.
 *
 * Note that libfdisk allows to specify arbitrary custom header, the default
 * built-in headers are "unit" and "label", and some label specific headers
 * (for example "uuid" and "name" for GPT).
 *
 * Returns: 0 on success, <0 on error
 */
int fdisk_script_set_header(struct fdisk_script *dp,
			    const char *name,
			    const char *data)
{
	struct fdisk_scriptheader *fi;

	if (!dp || !name)
		return -EINVAL;

	fi = script_get_header(dp, name);
	if (!fi && !data)
		return 0;	/* want to remove header that does not exist, success */

	if (!data) {
		DBG(SCRIPT, ul_debugobj(dp, "freeing header %s", name));

		/* no data, remove the header */
		fdisk_script_free_header(fi);
		return 0;
	}

	if (!fi) {
		DBG(SCRIPT, ul_debugobj(dp, "setting new header %s='%s'", name, data));

		/* new header */
		fi = calloc(1, sizeof(*fi));
		if (!fi)
			return -ENOMEM;
		INIT_LIST_HEAD(&fi->headers);
		fi->name = strdup(name);
		fi->data = strdup(data);
		if (!fi->data || !fi->name) {
			fdisk_script_free_header(fi);
			return -ENOMEM;
		}
		list_add_tail(&fi->headers, &dp->headers);
	} else {
		/* update existing */
		char *x = strdup(data);

		DBG(SCRIPT, ul_debugobj(dp, "update '%s' header '%s' -> '%s'", name, fi->data, data));

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
 * fdisk_script_has_force_label:
 * @dp: script
 *
 * Label has been explicitly specified in the script.
 *
 * Since: 2.30
 *
 * Returns: true if "label: name" has been parsed.
 */
int fdisk_script_has_force_label(struct fdisk_script *dp)
{
	assert(dp);
	return dp->force_label;
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

	if (!dp || (!cxt && !dp->cxt))
		return -EINVAL;

	if (!cxt)
		cxt = dp->cxt;

	DBG(SCRIPT, ul_debugobj(dp, "reading context into script"));
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

	if (!rc && fdisk_is_label(cxt, GPT)) {
		struct fdisk_labelitem item = FDISK_LABELITEM_INIT;
		char buf[64];

		/* first-lba */
		rc = fdisk_get_disklabel_item(cxt, GPT_LABELITEM_FIRSTLBA, &item);
		if (!rc) {
			snprintf(buf, sizeof(buf), "%"PRIu64, item.data.num64);
			rc = fdisk_script_set_header(dp, "first-lba", buf);
		}

		/* last-lba */
		if (!rc)
			rc = fdisk_get_disklabel_item(cxt, GPT_LABELITEM_LASTLBA, &item);
		if (!rc) {
			snprintf(buf, sizeof(buf), "%"PRIu64, item.data.num64);
			rc = fdisk_script_set_header(dp, "last-lba", buf);
		}

		/* table-length */
		if (!rc) {
			size_t n = fdisk_get_npartitions(cxt);
			if (n != FDISK_GPT_NPARTITIONS_DEFAULT) {
				snprintf(buf, sizeof(buf), "%zu", n);
				rc = fdisk_script_set_header(dp, "table-length", buf);
			}
		}
	}

	if (!rc && fdisk_get_grain_size(cxt) != 2048 * 512) {
		char buf[64];

		snprintf(buf, sizeof(buf), "%lu", fdisk_get_grain_size(cxt));
		rc = fdisk_script_set_header(dp, "grain", buf);
	}


	DBG(SCRIPT, ul_debugobj(dp, "read context done [rc=%d]", rc));
	return rc;
}

/**
 * fdisk_script_enable_json:
 * @dp: script
 * @json: 0 or 1
 *
 * Disable/Enable JSON output format.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_script_enable_json(struct fdisk_script *dp, int json)
{
	assert(dp);

	dp->json = json;
	return 0;
}

static void fput_indent(int indent, FILE *f)
{
	int i;

	for (i = 0; i <= indent; i++)
		fputs("   ", f);
}

static void fput_var_separator(int *nvars, FILE *f)
{
	if (*nvars > 0)
		fputs(", ", f);
	++(*nvars);
}

static int write_file_json(struct fdisk_script *dp, FILE *f)
{
	struct list_head *h;
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	const char *devname = NULL;
	int ct = 0, indent = 0;

	assert(dp);
	assert(f);

	DBG(SCRIPT, ul_debugobj(dp, "writing json dump to file"));

	fputs("{\n", f);

	fput_indent(indent, f);
	fputs("\"partitiontable\": {\n", f);
	indent++;

	/* script headers */
	list_for_each(h, &dp->headers) {
		struct fdisk_scriptheader *fi = list_entry(h, struct fdisk_scriptheader, headers);
		const char *name = fi->name;
		int num = 0;

		if (strcmp(name, "first-lba") == 0) {
			name = "firstlba";
			num = 1;
		} else if (strcmp(name, "last-lba") == 0) {
			name = "lastlba";
			num = 1;
		} else if (strcmp(name, "label-id") == 0)
			name = "id";

		fput_indent(indent, f);
		fputs_quoted_json_lower(name, f);
		fputs(":", f);
		if (!num)
			fputs_quoted_json(fi->data, f);
		else
			fputs(fi->data, f);

		if (!dp->table && fi == list_last_entry(&dp->headers, struct fdisk_scriptheader, headers))
			fputc('\n', f);
		else
			fputs(",\n", f);

		if (strcmp(name, "device") == 0)
			devname = fi->data;
	}


	if (!dp->table) {
		DBG(SCRIPT, ul_debugobj(dp, "script table empty"));
		goto done;
	}

	DBG(SCRIPT, ul_debugobj(dp, "%zu entries", fdisk_table_get_nents(dp->table)));

	fput_indent(indent, f);
	fputs("\"partitions\": [\n", f);
	indent++;

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(dp->table, &itr, &pa) == 0) {
		char *p = NULL;
		int nvars = 0;

		ct++;
		fput_indent(indent, f);
		fputc('{', f);
		if (devname)
			p = fdisk_partname(devname, pa->partno + 1);
		if (p) {
			DBG(SCRIPT, ul_debugobj(dp, "write %s entry", p));
			fputs("\"node\":", f);
			fputs_quoted_json(p, f);
			nvars++;
		}

		if (fdisk_partition_has_start(pa)) {
			fput_var_separator(&nvars, f);
			fprintf(f, "\"start\":%ju", (uintmax_t)pa->start);
		}
		if (fdisk_partition_has_size(pa)) {
			fput_var_separator(&nvars, f);
			fprintf(f, "\"size\":%ju", (uintmax_t)pa->size);
		}
		if (pa->type && fdisk_parttype_get_string(pa->type)) {
			fput_var_separator(&nvars, f);
			fputs("\"type\":", f);
			fputs_quoted_json(fdisk_parttype_get_string(pa->type), f);
		} else if (pa->type) {
			fput_var_separator(&nvars, f);
			fprintf(f, "\"type\":\"%x\"", fdisk_parttype_get_code(pa->type));
		}

		if (pa->uuid) {
			fput_var_separator(&nvars, f);
			fputs("\"uuid\":", f);
			fputs_quoted_json(pa->uuid, f);
		}
		if (pa->name && *pa->name) {
			fput_var_separator(&nvars, f);
			fputs("\"name\":", f),
			fputs_quoted_json(pa->name, f);
		}

		/* for MBR attr=80 means bootable */
		if (pa->attrs) {
			struct fdisk_label *lb = script_get_label(dp);

			if (!lb || fdisk_label_get_type(lb) != FDISK_DISKLABEL_DOS) {
				fput_var_separator(&nvars, f);
				fputs("\"attrs\":", f);
				fputs_quoted_json(pa->attrs, f);
			}
		}
		if (fdisk_partition_is_bootable(pa)) {
			fput_var_separator(&nvars, f);
			fprintf(f, "\"bootable\":true");
		}

		if ((size_t)ct < fdisk_table_get_nents(dp->table))
			fputs("},\n", f);
		else
			fputs("}\n", f);
	}

	indent--;
	fput_indent(indent, f);
	fputs("]\n", f);
done:
	indent--;
	fput_indent(indent, f);
	fputs("}\n}\n", f);

	DBG(SCRIPT, ul_debugobj(dp, "write script done"));
	return 0;
}

static int write_file_sfdisk(struct fdisk_script *dp, FILE *f)
{
	struct list_head *h;
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	const char *devname = NULL;

	assert(dp);
	assert(f);

	DBG(SCRIPT, ul_debugobj(dp, "writing sfdisk-like script to file"));

	/* script headers */
	list_for_each(h, &dp->headers) {
		struct fdisk_scriptheader *fi = list_entry(h, struct fdisk_scriptheader, headers);
		fprintf(f, "%s: %s\n", fi->name, fi->data);
		if (strcmp(fi->name, "device") == 0)
			devname = fi->data;
	}

	if (!dp->table) {
		DBG(SCRIPT, ul_debugobj(dp, "script table empty"));
		return 0;
	}

	DBG(SCRIPT, ul_debugobj(dp, "%zu entries", fdisk_table_get_nents(dp->table)));

	fputc('\n', f);

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(dp->table, &itr, &pa) == 0) {
		char *p = NULL;

		if (devname)
			p = fdisk_partname(devname, pa->partno + 1);
		if (p) {
			DBG(SCRIPT, ul_debugobj(dp, "write %s entry", p));
			fprintf(f, "%s :", p);
		} else
			fprintf(f, "%zu :", pa->partno + 1);

		if (fdisk_partition_has_start(pa))
			fprintf(f, " start=%12ju", (uintmax_t)pa->start);
		if (fdisk_partition_has_size(pa))
			fprintf(f, ", size=%12ju", (uintmax_t)pa->size);

		if (pa->type && fdisk_parttype_get_string(pa->type))
			fprintf(f, ", type=%s", fdisk_parttype_get_string(pa->type));
		else if (pa->type)
			fprintf(f, ", type=%x", fdisk_parttype_get_code(pa->type));

		if (pa->uuid)
			fprintf(f, ", uuid=%s", pa->uuid);
		if (pa->name && *pa->name) {
			fputs(", name=", f);
			fputs_quoted(pa->name, f);
		}

		/* for MBR attr=80 means bootable */
		if (pa->attrs) {
			struct fdisk_label *lb = script_get_label(dp);

			if (!lb || fdisk_label_get_type(lb) != FDISK_DISKLABEL_DOS)
				fprintf(f, ", attrs=\"%s\"", pa->attrs);
		}
		if (fdisk_partition_is_bootable(pa))
			fprintf(f, ", bootable");
		fputc('\n', f);
	}

	DBG(SCRIPT, ul_debugobj(dp, "write script done"));
	return 0;
}

/**
 * fdisk_script_write_file:
 * @dp: script
 * @f: output file
 *
 * Writes script @dp to the file @f.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_script_write_file(struct fdisk_script *dp, FILE *f)
{
	assert(dp);

	if (dp->json)
		return write_file_json(dp, f);

	return write_file_sfdisk(dp, f);
}

static inline int is_header_line(const char *s)
{
	const char *p = strchr(s, ':');

	if (!p || p == s || !*(p + 1) || strchr(s, '='))
		return 0;

	return 1;
}

/* parses "<name>: value", note modifies @s*/
static int parse_line_header(struct fdisk_script *dp, char *s)
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

	if (strcmp(name, "label") == 0) {
		if (dp->cxt && !fdisk_get_label(dp->cxt, value))
			goto done;			/* unknown label name */
		dp->force_label = 1;
	} else if (strcmp(name, "unit") == 0) {
		if (strcmp(value, "sectors") != 0)
			goto done;			/* only "sectors" supported */
	} else if (strcmp(name, "label-id") == 0
		   || strcmp(name, "device") == 0
		   || strcmp(name, "grain") == 0
		   || strcmp(name, "first-lba") == 0
		   || strcmp(name, "last-lba") == 0
		   || strcmp(name, "table-length") == 0) {
		;					/* whatever is possible */
	} else
		goto done;				/* unknown header */

	if (*name && *value)
		rc = fdisk_script_set_header(dp, name, value);
done:
	if (rc)
		DBG(SCRIPT, ul_debugobj(dp, "header parse error: "
				"[rc=%d, name='%s', value='%s']",
				rc, name, value));
	return rc;

}

/* returns zero terminated string with next token and @str is updated */
static char *next_token(char **str)
{
	char *tk_begin = NULL,
	     *tk_end = NULL,
	     *end = NULL,
	     *p;
	int open_quote = 0, terminated = 0;

	for (p = *str; p && *p; p++) {
		if (!tk_begin) {
			if (isblank(*p))
				continue;
			tk_begin = *p == '"' ? p + 1 : p;
		}
		if (*p == '"')
			open_quote ^= 1;
		if (open_quote)
			continue;
		if (isblank(*p) || *p == ',' || *p == ';' || *p == '"' )
			tk_end = p;
		else if (*(p + 1) == '\0')
			tk_end = p + 1;
		if (tk_begin && tk_end)
			break;
	}

	if (!tk_end)
		return NULL;

	end = tk_end;

	/* skip closing quotes */
	if (*end == '"')
		end++;

	/* token is terminated by blank (or blank is before "," or ";") */
	if (isblank(*end)) {
		end = (char *) skip_blank(end);
		terminated++;
	}

	/* token is terminated by "," or ";" */
	if (*end == ',' || *end == ';') {
		end++;
		terminated++;

	/* token is terminated by \0 */
	} else if (!*end)
		terminated++;

	if (!terminated) {
		DBG(SCRIPT, ul_debug("unterminated token '%s'", end));
		return NULL;
	}

	/* skip extra space after terminator */
	end = (char *) skip_blank(end);

	*tk_end = '\0';
	*str = end;
	return tk_begin;
}

static int next_number(char **s, uint64_t *num, int *power)
{
	char *tk;
	int rc = -EINVAL;

	assert(num);
	assert(s);

	tk = next_token(s);
	if (tk)
		rc = parse_size(tk, (uintmax_t *) num, power);
	return rc;
}

static int next_string(char **s, char **str)
{
	char *tk;
	int rc = -EINVAL;

	assert(s);
	assert(str);

	tk = next_token(s);
	if (tk) {
		*str = strdup(tk);
		rc = !*str ? -ENOMEM : 0;
	}
	return rc;
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
static int parse_line_nameval(struct fdisk_script *dp, char *s)
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

		DBG(SCRIPT, ul_debugobj(dp, " parsing '%s'", p));
		p = (char *) skip_blank(p);

		if (!strncasecmp(p, "start=", 6)) {
			int pow = 0;
			p += 6;
			rc = next_number(&p, &num, &pow);
			if (!rc) {
				if (pow)	/* specified as <num><suffix> */
					num /= dp->cxt->sector_size;
				fdisk_partition_set_start(pa, num);
				fdisk_partition_start_follow_default(pa, 0);
			}
		} else if (!strncasecmp(p, "size=", 5)) {
			int pow = 0;

			p += 5;
			rc = next_number(&p, &num, &pow);
			if (!rc) {
				if (pow)	/* specified as <num><suffix> */
					num /= dp->cxt->sector_size;
				else		/* specified as number of sectors */
					fdisk_partition_size_explicit(pa, 1);
				fdisk_partition_set_size(pa, num);
				fdisk_partition_end_follow_default(pa, 0);
			}

		} else if (!strncasecmp(p, "bootable", 8)) {
			/* we use next_token() to skip possible extra space */
			char *tk = next_token(&p);
			if (tk && strcasecmp(tk, "bootable") == 0)
				pa->boot = 1;
			else
				rc = -EINVAL;

		} else if (!strncasecmp(p, "attrs=", 6)) {
			p += 6;
			rc = next_string(&p, &pa->attrs);

		} else if (!strncasecmp(p, "uuid=", 5)) {
			p += 5;
			rc = next_string(&p, &pa->uuid);

		} else if (!strncasecmp(p, "name=", 5)) {
			p += 5;
			rc = next_string(&p, &pa->name);
			unhexmangle_string(pa->name);

		} else if (!strncasecmp(p, "type=", 5) ||
			   !strncasecmp(p, "Id=", 3)) {		/* backward compatibility */
			char *type;

			p += ((*p == 'I' || *p == 'i') ? 3 : 5); /* "Id=", "type=" */

			rc = next_string(&p, &type);
			if (rc)
				break;
			pa->type = fdisk_label_parse_parttype(
					script_get_label(dp), type);
			free(type);

			if (!pa->type) {
				rc = -EINVAL;
				fdisk_unref_parttype(pa->type);
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
		case 'U':	/* UEFI system */
			type = "EF";
			break;
		case 'R':	/* Linux RAID */
			type = "FD";
			break;
		case 'V':	/* LVM */
			type = "8E";
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
		case 'U':	/* UEFI system */
			type = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
			break;
		case 'R':	/* Linux RAID */
			type = "A19D880F-05FC-4D3B-A006-743F0F84911E";
			break;
		case 'V':	/* LVM */
			type = "E6D6D379-F507-44C2-A23C-238F2A3DF928";
			break;
		}
	}

	return type ? fdisk_label_parse_parttype(lb, type) : NULL;
}

#define TK_PLUS		1
#define TK_MINUS	-1

#define alone_sign(_sign, _p)	(_sign && (*_p == '\0' || isblank(*_p)))

/* simple format:
 * <start>, <size>, <type>, <bootable>, ...
 */
static int parse_line_valcommas(struct fdisk_script *dp, char *s)
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
		int sign = 0;

		p = (char *) skip_blank(p);
		item++;

		if (item != ITEM_BOOTABLE) {
			sign = *p == '-' ? TK_MINUS : *p == '+' ? TK_PLUS : 0;
			if (sign)
				p++;
		}

		DBG(SCRIPT, ul_debugobj(dp, " parsing item %d ('%s')", item, p));
		begin = p;

		switch (item) {
		case ITEM_START:
			if (*p == ',' || *p == ';' || alone_sign(sign, p))
				fdisk_partition_start_follow_default(pa, 1);
			else {
				int pow = 0;

				rc = next_number(&p, &num, &pow);
				if (!rc) {
					if (pow)	/* specified as <num><suffix> */
						num /= dp->cxt->sector_size;
					fdisk_partition_set_start(pa, num);
					pa->movestart = sign == TK_MINUS ? FDISK_MOVE_DOWN :
							sign == TK_PLUS  ? FDISK_MOVE_UP :
							FDISK_MOVE_NONE;
				}
				fdisk_partition_start_follow_default(pa, 0);
			}
			break;
		case ITEM_SIZE:
			if (*p == ',' || *p == ';' || alone_sign(sign, p)) {
				fdisk_partition_end_follow_default(pa, 1);
				if (sign == TK_PLUS)
					/* '+' alone means use all possible space, '-' alone means nothing */
					pa->resize = FDISK_RESIZE_ENLARGE;
			} else {
				int pow = 0;
				rc = next_number(&p, &num, &pow);
				if (!rc) {
					if (pow) /* specified as <size><suffix> */
						num /= dp->cxt->sector_size;
					else	 /* specified as number of sectors */
						fdisk_partition_size_explicit(pa, 1);
					fdisk_partition_set_size(pa, num);
					pa->resize = sign == TK_MINUS ? FDISK_RESIZE_REDUCE :
						     sign == TK_PLUS  ? FDISK_RESIZE_ENLARGE :
							FDISK_RESIZE_NONE;
				}
				fdisk_partition_end_follow_default(pa, 0);
			}
			break;
		case ITEM_TYPE:
			if (*p == ',' || *p == ';' || alone_sign(sign, p))
				break;	/* use default type */

			rc = next_string(&p, &str);
			if (rc)
				break;

			pa->type = translate_type_shortcuts(dp, str);
			if (!pa->type)
				pa->type = fdisk_label_parse_parttype(
						script_get_label(dp), str);
			free(str);

			if (!pa->type) {
				rc = -EINVAL;
				fdisk_unref_parttype(pa->type);
				pa->type = NULL;
				break;
			}
			break;
		case ITEM_BOOTABLE:
			if (*p == ',' || *p == ';')
				break;
			else {
				char *tk = next_token(&p);
				if (tk && *tk == '*' && *(tk + 1) == '\0')
					pa->boot = 1;
				else if (tk && *tk == '-' && *(tk + 1) == '\0')
					pa->boot = 0;
				else if (tk && *tk == '+' && *(tk + 1) == '\0')
					pa->boot = 1;
				else
					rc = -EINVAL;
			}
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
static int fdisk_script_read_buffer(struct fdisk_script *dp, char *s)
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
		rc = parse_line_header(dp, s);

	/* parse script format */
	else if (strchr(s, '='))
		rc = parse_line_nameval(dp, s);

	/* parse simple <value>, ... format */
	else
		rc = parse_line_valcommas(dp, s);

	if (rc)
		DBG(SCRIPT, ul_debugobj(dp, "%zu: parse error [rc=%d]",
				dp->nlines, rc));
	return rc;
}

/**
 * fdisk_script_set_fgets:
 * @dp: script
 * @fn_fgets: callback function
 *
 * The library uses fgets() function to read the next line from the script.
 * This default maybe overridden by another function. Note that the function has
 * to return the line terminated by \n (for example readline(3) removes \n).
 *
 * Return: 0 on success, <0 on error
 */
int fdisk_script_set_fgets(struct fdisk_script *dp,
			  char *(*fn_fgets)(struct fdisk_script *, char *, size_t, FILE *))
{
	assert(dp);

	dp->fn_fgets = fn_fgets;
	return 0;
}

/**
 * fdisk_script_read_line:
 * @dp: script
 * @f: file
 * @buf: buffer to store one line of the file
 * @bufsz: buffer size
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

	DBG(SCRIPT, ul_debugobj(dp, " parsing line %zu", dp->nlines));

	/* read the next non-blank non-comment line */
	do {
		if (dp->fn_fgets) {
			if (dp->fn_fgets(dp, buf, bufsz, f) == NULL)
				return 1;
		} else if (fgets(buf, bufsz, f) == NULL)
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
 * @f: input file
 *
 * Reads file @f into script @dp.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_script_read_file(struct fdisk_script *dp, FILE *f)
{
	char buf[BUFSIZ];
	int rc = 1;

	assert(dp);
	assert(f);

	DBG(SCRIPT, ul_debugobj(dp, "parsing file"));

	while (!feof(f)) {
		rc = fdisk_script_read_line(dp, f, buf, sizeof(buf));
		if (rc)
			break;
	}

	if (rc == 1)
		rc = 0;		/* end of file */

	DBG(SCRIPT, ul_debugobj(dp, "parsing file done [rc=%d]", rc));
	return rc;
}

/**
 * fdisk_set_script:
 * @cxt: context
 * @dp: script (or NULL to remove previous reference)
 *
 * Sets reference to the @dp script and remove reference to the previously used
 * script.
 *
 * The script headers might be used by label drivers to overwrite
 * built-in defaults (for example disk label Id) and label driver might
 * optimize the default semantic to be more usable for scripts (for example to
 * not ask for primary/logical/extended partition type).
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
 * @dp: script
 *
 * Associate context @cxt with script @dp and creates a new empty disklabel.
 * The script may be later unreference by fdisk_set_script() with NULL as script.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_apply_script_headers(struct fdisk_context *cxt, struct fdisk_script *dp)
{
	const char *name;
	const char *str;
	int rc;

	assert(cxt);
	assert(dp);

	DBG(SCRIPT, ul_debugobj(dp, "applying script headers"));
	fdisk_set_script(cxt, dp);

	str = fdisk_script_get_header(dp, "grain");
	if (str) {
		uintmax_t sz;

		rc = parse_size(str, &sz, NULL);
		if (rc == 0)
			rc = fdisk_save_user_grain(cxt, sz);
		if (rc)
			return rc;
	}

	if (fdisk_has_user_device_properties(cxt))
		fdisk_apply_user_device_properties(cxt);

	/* create empty label */
	name = fdisk_script_get_header(dp, "label");
	if (!name)
		return -EINVAL;

	rc = fdisk_create_disklabel(cxt, name);
	if (rc)
		return rc;

	str = fdisk_script_get_header(dp, "table-length");
	if (str) {
		uintmax_t sz;

		rc = parse_size(str, &sz, NULL);
		if (rc == 0)
			rc = fdisk_gpt_set_npartitions(cxt, sz);
	}

	return rc;
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

	DBG(CXT, ul_debugobj(cxt, "applying script %p", dp));

	old = fdisk_get_script(cxt);
	fdisk_ref_script(old);

	/* create empty disk label */
	rc = fdisk_apply_script_headers(cxt, dp);

	/* create partitions */
	if (!rc && dp->table)
		rc = fdisk_apply_table(cxt, dp->table);

	fdisk_set_script(cxt, old);
	fdisk_unref_script(old);

	DBG(CXT, ul_debugobj(cxt, "script done [rc=%d]", rc));
	return rc;
}

#ifdef TEST_PROGRAM
static int test_dump(struct fdisk_test *ts, int argc, char *argv[])
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

static int test_read(struct fdisk_test *ts, int argc, char *argv[])
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

static int test_stdin(struct fdisk_test *ts, int argc, char *argv[])
{
	char buf[BUFSIZ];
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
		rc = fdisk_script_read_line(dp, stdin, buf, sizeof(buf));

		if (rc == 0) {
			pa = fdisk_table_get_partition(dp->table, n);
			printf(" #%zu  %12ju %12ju\n",	n + 1,
						(uintmax_t)fdisk_partition_get_start(pa),
						(uintmax_t)fdisk_partition_get_size(pa));
		}
	} while (rc == 0);

	if (!rc)
		fdisk_script_write_file(dp, stdout);
	fdisk_unref_script(dp);
	fdisk_unref_context(cxt);

	return rc;
}

static int test_apply(struct fdisk_test *ts, int argc, char *argv[])
{
	char *devname = argv[1], *scriptname = argv[2];
	struct fdisk_context *cxt;
	struct fdisk_script *dp;
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
						(uintmax_t)fdisk_partition_get_start(pa),
						(uintmax_t)fdisk_partition_get_size(pa));
	}

done:
	fdisk_free_iter(itr);
	fdisk_unref_table(tb);

	/*fdisk_write_disklabel(cxt);*/
	fdisk_unref_context(cxt);
	return 0;
}

static int test_tokens(struct fdisk_test *ts, int argc, char *argv[])
{
	char *p, *str = argc == 2 ? strdup(argv[1]) : NULL;
	int i;

	for (i = 1, p = str; p && *p; i++) {
		char *tk = next_token(&p);

		if (!tk)
			break;

		printf("#%d: '%s'\n", i, tk);
	}

	free(str);
	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
	{ "--dump",    test_dump,    "<device>            dump PT as script" },
	{ "--read",    test_read,    "<file>              read PT script from file" },
	{ "--apply",   test_apply,   "<device> <file>     try apply script from file to device" },
	{ "--stdin",   test_stdin,   "                    read input like sfdisk" },
	{ "--tokens",  test_tokens,  "<string>            parse string" },
	{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
