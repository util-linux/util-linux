/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "issuefile-parser.h"
#include "xalloc.h"

static const struct agetty_idef itemdefs[__AGETTY_ESC_COUNT] = {
	[AGETTY_ESC_TEXT]	= { .code = 0 },

	[AGETTY_ESC_SYSNAME]	= { .code = 's' },
	[AGETTY_ESC_NODENAME]	= { .code = 'n' },
	[AGETTY_ESC_RELEASE]	= { .code = 'r' },
	[AGETTY_ESC_VERSION]	= { .code = 'v' },
	[AGETTY_ESC_MACHINE]	= { .code = 'm' },

	[AGETTY_ESC_NIS_DOMAIN]	= { .code = 'o' },
	[AGETTY_ESC_DNS_DOMAIN]	= { .code = 'O' },

	[AGETTY_ESC_DATE]	= { .code = 'd' },
	[AGETTY_ESC_TIME]	= { .code = 't' },

	[AGETTY_ESC_TTYNAME]	= { .code = 'l' },
	[AGETTY_ESC_BAUDRATE]	= { .code = 'b' },

	[AGETTY_ESC_OSRELEASE]	= { .code = 'S', .args = (const char *[]){ "variable", NULL } },

	[AGETTY_ESC_USERS]	= { .code = 'u' },
	[AGETTY_ESC_USERS_TEXT]	= { .code = 'U' },

	[AGETTY_ESC_ESCAPE]	= { .code = 'e', .args = (const char *[]){ "color", NULL } },

	[AGETTY_ESC_IPV4]	= { .code = '4', .args = (const char *[]){ "interface", NULL } },
	[AGETTY_ESC_IPV6]	= { .code = '6', .args = (const char *[]){ "interface", NULL } },
	[AGETTY_ESC_NET_GOOD]	= { .code = 'a' },
	[AGETTY_ESC_NET_ALL]	= { .code = 'A' },
};

void agetty_ifile_init(struct agetty_ifile *ls)
{
	if (ls)
		INIT_LIST_HEAD(&ls->items);
}

static void free_item(struct agetty_iitem *item)
{
	if (!item)
		return;
	list_del(&item->items);
	free(item->data);
	free(item->arg);
	free(item);
}

void agetty_ifile_free(struct agetty_ifile *ls)
{
	if (!ls)
		return;
	while (!list_empty(&ls->items)) {
		struct agetty_iitem *item = list_entry(ls->items.next,
						struct agetty_iitem, items);
		free_item(item);
	}
}

static int code_to_id(int c)
{
	size_t i;

	for (i = 1; i < __AGETTY_ESC_COUNT; i++)
		if (itemdefs[i].code == c)
			return (int) i;
	return -1;
}

static struct agetty_iitem *new_item(int id, char *data, char *arg)
{
	struct agetty_iitem *item = xcalloc(1, sizeof(*item));

	item->id = id;
	item->data = data;
	item->arg = arg;
	INIT_LIST_HEAD(&item->items);
	return item;
}

static char *parse_escape_arg(FILE *f)
{
	char buf[128];
	size_t i = 0;
	int c = fgetc(f);

	if (c == EOF || (unsigned char) c != '{') {
		ungetc(c, f);
		return NULL;
	}

	do {
		c = fgetc(f);
		if (c == EOF)
			return NULL;
		if ((unsigned char) c != '}' && i < sizeof(buf) - 1)
			buf[i++] = (unsigned char) c;
	} while ((unsigned char) c != '}');

	buf[i] = '\0';
	return xstrdup(buf);
}

static void flush_text(struct agetty_ifile *ls, char **buf, size_t *sz)
{
	struct agetty_iitem *item;

	if (!*sz)
		return;

	item = new_item(AGETTY_ESC_TEXT, *buf, NULL);
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

			item = new_item(id, NULL, parse_escape_arg(f));
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
