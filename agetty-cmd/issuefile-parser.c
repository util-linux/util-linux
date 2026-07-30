/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <stdlib.h>
#include <string.h>

#include "issuefile-parser.h"

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
