/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef ISSUEDIR_SUPPORT
# include <dirent.h>
#endif

#include "issuefile-parser.h"
#include "c.h"
#include "fileutils.h"
#include "strutils.h"
#include "xalloc.h"

static const struct agetty_idef itemdefs[__AGETTY_ESC_COUNT] = {
	[AGETTY_ESC_TEXT]	= { .name = "TEXT" },

	[AGETTY_ESC_SYSNAME]	= { .name = "SYSNAME",    .code = 's' },
	[AGETTY_ESC_NODENAME]	= { .name = "NODENAME",   .code = 'n' },
	[AGETTY_ESC_RELEASE]	= { .name = "RELEASE",    .code = 'r' },
	[AGETTY_ESC_VERSION]	= { .name = "VERSION",    .code = 'v' },
	[AGETTY_ESC_MACHINE]	= { .name = "MACHINE",    .code = 'm' },

	[AGETTY_ESC_NIS_DOMAIN]	= { .name = "NIS_DOMAIN", .code = 'o' },
	[AGETTY_ESC_DNS_DOMAIN]	= { .name = "DNS_DOMAIN", .code = 'O' },

	[AGETTY_ESC_DATE]	= { .name = "DATE",       .code = 'd' },
	[AGETTY_ESC_TIME]	= { .name = "TIME",       .code = 't' },

	[AGETTY_ESC_TTYNAME]	= { .name = "TTYNAME",    .code = 'l' },
	[AGETTY_ESC_BAUDRATE]	= { .name = "BAUDRATE",   .code = 'b' },

	[AGETTY_ESC_OSRELEASE]	= { .name = "OSRELEASE",  .code = 'S',
				    .args = (const char *[]){ "variable", NULL } },

	[AGETTY_ESC_USERS]	= { .name = "USERS",      .code = 'u' },
	[AGETTY_ESC_USERS_TEXT]	= { .name = "USERS_TEXT", .code = 'U' },

	[AGETTY_ESC_ESCAPE]	= { .name = "ESCAPE",     .code = 'e',
				    .args = (const char *[]){ "color", NULL } },

	[AGETTY_ESC_IPV4]	= { .name = "IPV4",       .code = '4',
				    .args = (const char *[]){ "interface", NULL } },
	[AGETTY_ESC_IPV6]	= { .name = "IPV6",       .code = '6',
				    .args = (const char *[]){ "interface", NULL } },
	[AGETTY_ESC_NET_GOOD]	= { .name = "NET_GOOD",   .code = 'a' },
	[AGETTY_ESC_NET_ALL]	= { .name = "NET_ALL",    .code = 'A' },
};

void agetty_ifile_init(struct agetty_ifile *ls)
{
	if (!ls)
		return;
	INIT_LIST_HEAD(&ls->items);
	memset(ls->handlers, 0, sizeof(ls->handlers));
	ls->initialized = true;
}

bool agetty_ifile_is_ready(struct agetty_ifile *ls)
{
	return ls && ls->initialized;
}

bool agetty_ifile_is_empty(struct agetty_ifile *ls)
{
	if (!ls || !ls->initialized)
		return true;
	return list_empty(&ls->items);
}

int agetty_ifile_set_handler(struct agetty_ifile *ls, int id,
		int (*printer)(struct agetty_iitem *, struct agetty_issue *,
			       struct agetty_ihandler *),
		void (*deinit)(void *),
		void *data)
{
	if (!ls || id < 0 || id >= __AGETTY_ESC_COUNT)
		return -EINVAL;

	ls->handlers[id].printer = printer;
	ls->handlers[id].deinit = deinit;
	ls->handlers[id].data = data;
	return 0;
}

static size_t idef_nargs(int id)
{
	const char * const *p;
	size_t n = 0;

	if (id < 0 || id >= __AGETTY_ESC_COUNT)
		return 0;
	for (p = itemdefs[id].args; p && *p; p++)
		n++;
	return n;
}

static void free_item(struct agetty_iitem *item)
{
	if (!item)
		return;

	list_del(&item->items);
	free(item->data);
	free(item->arg);

	if (item->args) {
		size_t i, n = idef_nargs(item->id);

		for (i = 0; i < n; i++)
			free(item->args[i]);
		free(item->args);
	}
	free(item);
}

void agetty_ifile_free(struct agetty_ifile *ls)
{
	size_t i;

	if (!ls)
		return;

	while (!list_empty(&ls->items)) {
		struct agetty_iitem *item = list_entry(ls->items.next,
						struct agetty_iitem, items);
		free_item(item);
	}

	for (i = 0; i < __AGETTY_ESC_COUNT; i++) {
		if (!ls->handlers[i].data)
			continue;
		if (ls->handlers[i].deinit)
			ls->handlers[i].deinit(ls->handlers[i].data);
		else
			free(ls->handlers[i].data);
	}

	ls->initialized = false;
}

const struct agetty_idef *agetty_idef_by_code(int c)
{
	size_t i;

	for (i = 1; i < __AGETTY_ESC_COUNT; i++)
		if (itemdefs[i].code == c)
			return &itemdefs[i];
	return NULL;
}

const char *agetty_idef_get_code(int id)
{
	if (id > 0 && id < __AGETTY_ESC_COUNT && itemdefs[id].code)
		return &itemdefs[id].code;
	return NULL;
}

void agetty_iiter_reset(struct agetty_iiter *itr)
{
	if (itr)
		memset(itr, 0, sizeof(*itr));
}

int agetty_ifile_next_item(struct agetty_ifile *ls,
			   struct agetty_iiter *itr,
			   struct agetty_iitem **item,
			   int filter_low, int filter_high)
{
	if (!ls || !itr || !item)
		return -EINVAL;

	if (!itr->head) {
		itr->head = &ls->items;
		itr->p = ls->items.next;
	}

	while (itr->p != itr->head) {
		struct agetty_iitem *it = list_entry(itr->p,
						struct agetty_iitem, items);
		itr->p = itr->p->next;

		if (filter_low >= 0 &&
		    (it->id < filter_low || it->id > filter_high))
			continue;

		*item = it;
		return 0;
	}

	return 1;
}

bool agetty_ifile_has_item(struct agetty_ifile *ls, int id)
{
	struct agetty_iiter itr = AGETTY_IITER_INIT;
	struct agetty_iitem *item = NULL;

	if (!ls)
		return false;
	return agetty_ifile_next_item(ls, &itr, &item, id, id) == 0;
}

int agetty_iitem_get_id(struct agetty_iitem *item)
{
	return item ? item->id : -EINVAL;
}

const char *agetty_iitem_get_data(struct agetty_iitem *item)
{
	return item ? item->data : NULL;
}

const char *agetty_iitem_get_arg(struct agetty_iitem *item, const char *name)
{
	const char * const *p;
	size_t i;

	if (!item)
		return NULL;
	if (!name)
		return item->arg;
	if (!item->args)
		return NULL;

	for (p = itemdefs[item->id].args, i = 0; p && *p; p++, i++)
		if (strcmp(*p, name) == 0)
			return item->args[i];
	return NULL;
}

static int code_to_id(int c)
{
	size_t i;

	for (i = 1; i < __AGETTY_ESC_COUNT; i++)
		if (itemdefs[i].code == c)
			return (int) i;
	return -1;
}

static struct agetty_iitem *new_item(int id, char *data)
{
	struct agetty_iitem *item = xcalloc(1, sizeof(*item));

	item->id = id;
	item->data = data;
	INIT_LIST_HEAD(&item->items);
	return item;
}

static int find_arg_index(int id, const char *name, size_t namesz)
{
	const char * const *p;
	int i;

	for (p = itemdefs[id].args, i = 0; p && *p; p++, i++)
		if (strncmp(*p, name, namesz) == 0 && (*p)[namesz] == '\0')
			return i;
	return -1;
}

static char *unquote(const char *s, size_t sz)
{
	if (sz >= 2 && s[0] == '"' && s[sz - 1] == '"')
		return xstrndup(s + 1, sz - 2);
	return xstrndup(s, sz);
}

static void parse_escape_args(struct agetty_iitem *item, FILE *f)
{
	char buf[128];
	size_t i = 0, n;
	int c = fgetc(f);
	char *p, *name, *value;
	size_t namesz, valsz;

	if (c == EOF || (unsigned char) c != '{') {
		ungetc(c, f);
		return;
	}

	do {
		c = fgetc(f);
		if (c == EOF)
			return;
		if ((unsigned char) c != '}' && i < sizeof(buf) - 1)
			buf[i++] = (unsigned char) c;
	} while ((unsigned char) c != '}');

	buf[i] = '\0';

	n = idef_nargs(item->id);

	/* try to parse as comma-separated name=value options */
	p = buf;
	while (ul_optstr_next(&p, &name, &namesz, &value, &valsz) == 0) {
		if (valsz && n) {
			int idx = find_arg_index(item->id, name, namesz);

			if (idx >= 0) {
				if (!item->args)
					item->args = xcalloc(n, sizeof(char *));
				free(item->args[idx]);
				item->args[idx] = unquote(value, valsz);
				continue;
			}
		}

		/* unnamed argument (no '=' or unknown name) */
		free(item->arg);
		item->arg = unquote(name, namesz);
	}
}

static void flush_text(struct agetty_ifile *ls, char **buf, size_t *sz)
{
	struct agetty_iitem *item;

	if (!*sz)
		return;

	item = new_item(AGETTY_ESC_TEXT, *buf);
	list_add_tail(&item->items, &ls->items);

	*buf = NULL;
	*sz = 0;
}

int agetty_ifile_parse_stream(struct agetty_ifile *ls, FILE *f)
{
	char *text = NULL;
	size_t textsz = 0;
	FILE *textf;
	int c;

	if (!ls || !f)
		return -EINVAL;

	textf = open_memstream(&text, &textsz);
	if (!textf)
		return -errno;

	while ((c = fgetc(f)) != EOF) {
		if (c == '\\') {
			struct agetty_iitem *item;
			int id;

			c = fgetc(f);
			if (c == EOF)
				break;

			id = code_to_id(c);
			if (id < 0) {
				fputc('\\', textf);
				fputc(c, textf);
				continue;
			}

			fflush(textf);
			if (textsz) {
				fclose(textf);
				textf = NULL;
				flush_text(ls, &text, &textsz);
			}

			item = new_item(id, NULL);
			parse_escape_args(item, f);
			list_add_tail(&item->items, &ls->items);

			if (!textf) {
				textf = open_memstream(&text, &textsz);
				if (!textf)
					return -errno;
			}
		} else
			fputc(c, textf);
	}

	fclose(textf);
	flush_text(ls, &text, &textsz);
	free(text);
	return 0;
}

int agetty_ifile_parse_file(struct agetty_ifile *ls, const char *filename)
{
	FILE *f;
	int rc;

	if (!ls || !filename)
		return -EINVAL;

	f = fopen(filename, "r");
	if (!f)
		return -errno;

	rc = agetty_ifile_parse_stream(ls, f);
	fclose(f);
	return rc;
}

#define ISSUEDIR_EXTSIZ	sizeof(ISSUEDIR_EXT)

#ifdef ISSUEDIR_SUPPORT
static int issuedir_filter(const struct dirent *d)
{
	size_t namesz;

#ifdef _DIRENT_HAVE_D_TYPE
	if (d->d_type != DT_UNKNOWN && d->d_type != DT_REG &&
	    d->d_type != DT_LNK)
		return 0;
#endif
	if (*d->d_name == '.')
		return 0;

	namesz = strlen(d->d_name);
	if (!namesz || namesz < ISSUEDIR_EXTSIZ + 1 ||
	    strcmp(d->d_name + (namesz - ISSUEDIR_EXTSIZ), "." ISSUEDIR_EXT) != 0)
		return 0;

	return 1;
}

int agetty_ifile_parse_dir(struct agetty_ifile *ls, const char *dirname)
{
	int dd, nfiles, i;
	struct dirent **namelist = NULL;

	if (!ls || !dirname)
		return -EINVAL;

	dd = open(dirname, O_RDONLY|O_CLOEXEC|O_DIRECTORY);
	if (dd < 0)
		return -errno;

	nfiles = scandirat(dd, ".", &namelist, issuedir_filter, versionsort);
	if (nfiles <= 0)
		goto done;

	for (i = 0; i < nfiles; i++) {
		struct dirent *d = namelist[i];
		FILE *f;

		f = fopen_at(dd, d->d_name, O_RDONLY|O_CLOEXEC, "r" UL_CLOEXECSTR);
		if (f) {
			agetty_ifile_parse_stream(ls, f);
			fclose(f);
		}
	}

	for (i = 0; i < nfiles; i++)
		free(namelist[i]);
	free(namelist);
done:
	close(dd);
	return 0;
}
#else
int agetty_ifile_parse_dir(struct agetty_ifile *ls __attribute__((__unused__)),
			   const char *dirname __attribute__((__unused__)))
{
	return 1;
}
#endif /* ISSUEDIR_SUPPORT */

int agetty_ifile_parse_spec(struct agetty_ifile *ls, const char *spec)
{
	char *list, *file;

	if (!ls || !spec)
		return -EINVAL;

	list = strdup(spec);
	if (!list)
		return -ENOMEM;

	for (file = strtok(list, ":"); file; file = strtok(NULL, ":")) {
		struct stat st;

		if (stat(file, &st) < 0)
			continue;
		if (S_ISDIR(st.st_mode))
			agetty_ifile_parse_dir(ls, file);
		else
			agetty_ifile_parse_file(ls, file);
	}

	free(list);
	return 0;
}

int agetty_ifile_print(struct agetty_ifile *ls,
		       struct agetty_issue *ie, FILE *out)
{
	struct agetty_iiter itr = AGETTY_IITER_INIT;
	struct agetty_iitem *item = NULL;

	if (!ls || !out)
		return -EINVAL;

	while (agetty_ifile_next_item(ls, &itr, &item, -1, -1) == 0) {
		int id = agetty_iitem_get_id(item);

		if (id == AGETTY_ESC_TEXT) {
			const char *data = agetty_iitem_get_data(item);

			if (data)
				fputs(data, out);
		} else if (id >= 0 && ls->handlers[id].printer)
			ls->handlers[id].printer(item, ie,
						 &ls->handlers[id]);
	}

	return 0;
}

void agetty_ifile_dump(struct agetty_ifile *ls, FILE *out)
{
	struct agetty_iiter itr = AGETTY_IITER_INIT;
	struct agetty_iitem *item = NULL;

	if (!ls || !out)
		return;

	while (agetty_ifile_next_item(ls, &itr, &item, -1, -1) == 0) {
		int id = agetty_iitem_get_id(item);
		const char *iname = (id >= 0 && id < __AGETTY_ESC_COUNT) ?
					itemdefs[id].name : "UNKNOWN";
		const char * const *p;

		fprintf(out, "%-12s", iname);

		if (agetty_iitem_get_arg(item, NULL))
			fprintf(out, " arg=\"%s\"",
					agetty_iitem_get_arg(item, NULL));

		for (p = itemdefs[id].args; p && *p; p++) {
			const char *v = agetty_iitem_get_arg(item, *p);

			if (v)
				fprintf(out, " %s=\"%s\"", *p, v);
		}

		if (agetty_iitem_get_data(item))
			fprintf(out, " data=\"%s\"",
					agetty_iitem_get_data(item));
		fputc('\n', out);
	}
}

#ifdef TEST_PROGRAM

static int fake_sysname_printer(struct agetty_iitem *item __attribute__((__unused__)),
				struct agetty_issue *ie __attribute__((__unused__)),
				struct agetty_ihandler *handler)
{
	if (!handler->data)
		handler->data = xstrdup("FakeOS");

	printf("%s", (const char *) handler->data);
	return 0;
}

int main(int argc, char *argv[])
{
	struct agetty_ifile ls;
	int rc;

	agetty_ifile_init(&ls);

	if (argc > 1)
		rc = agetty_ifile_parse_file(&ls, argv[1]);
	else
		rc = agetty_ifile_parse_stream(&ls, stdin);

	if (rc) {
		fprintf(stderr, "parse error: %s\n", strerror(-rc));
		return EXIT_FAILURE;
	}

	/* register handler only if the parsed file uses \s */
	if (agetty_ifile_has_item(&ls, AGETTY_ESC_SYSNAME))
		agetty_ifile_set_handler(&ls, AGETTY_ESC_SYSNAME,
					 fake_sysname_printer, NULL, NULL);

	printf("--- dump ---\n");
	agetty_ifile_dump(&ls, stdout);

	printf("--- print ---\n");
	agetty_ifile_print(&ls, NULL, stdout);

	printf("--- query ---\n");
	printf("has IPV4: %s\n",
		agetty_ifile_has_item(&ls, AGETTY_ESC_IPV4) ? "yes" : "no");
	printf("has IPV6: %s\n",
		agetty_ifile_has_item(&ls, AGETTY_ESC_IPV6) ? "yes" : "no");

	agetty_ifile_free(&ls);
	return EXIT_SUCCESS;
}

#endif /* TEST_PROGRAM */
