
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "c.h"
#include "fdisk.h"

struct menu_entry {
	const char	key;
	const char	*title;
	unsigned int	normal : 1,
			expert : 1,
			hidden : 1;

	enum fdisk_labeltype	exclude;
};

#define IS_MENU_SEP(e)	((e)->key == '-')
#define IS_MENU_HID(e)	((e)->hidden)

struct menu {
	enum fdisk_labeltype	label;		/* only for this label */
	enum fdisk_labeltype	exclude;	/* all labels except this */

	int (*callback)(struct fdisk_context *, struct menu *, int);

	struct menu_entry	entries[];	/* NULL terminated array */
};

struct menu_context {
	size_t		menu_idx;		/* the current menu */
	size_t		entry_idx;		/* index with in the current menu */
};

#define MENU_CXT_EMPTY	{ 0, 0 }

/*
 * Menu entry macros:
 *	MENU_X*    expert mode only
 *      MENU_B*    both -- expert + normal mode
 *
 *      *_E exclude
 *      *_H hidden
 */

/* separator */
#define MENU_SEP(t)		{ .title = t, .key = '-', .normal = 1 }
#define MENU_XSEP(t)		{ .title = t, .key = '-', .expert = 1 }
#define MENU_BSEP(t)		{ .title = t, .key = '-', .expert = 1, .normal = 1 }

/* entry */
#define MENU_ENT(k, t)		{ .title = t, .key = k, .normal = 1 }
#define MENU_ENT_E(k, t, l)	{ .title = t, .key = k, .normal = 1, .exclude = l }

#define MENU_XENT(k, t)		{ .title = t, .key = k, .expert = 1 }
#define MENU_XENT_H(k, t)	{ .title = t, .key = k, .expert = 1, .hidden = 1 }

#define MENU_BENT(k, t)		{ .title = t, .key = k, .expert = 1, .normal = 1 }


/* Generic menu */
struct menu menu_generic = {
/*	.callback	= generic_menu_cb,*/
	.entries	= {
		MENU_XENT('d', N_("print the raw data of the first sector")),

		MENU_SEP(N_("Alter partition table")),
		MENU_ENT  ('d', N_("delete a partition")),
		MENU_ENT  ('l', N_("list known partition types")),
		MENU_ENT  ('n', N_("add a new partition")),
		MENU_BENT ('p', N_("print the partition table")),
		MENU_ENT  ('t', N_("change a partition's system id")),
		MENU_ENT  ('v', N_("verify the partition table")),

		MENU_SEP(N_("Misc")),
		MENU_BENT ('m', N_("print this menu")),
		MENU_ENT_E('u', N_("change display/entry units"), FDISK_DISKLABEL_GPT),
		MENU_ENT  ('x', N_("extra functionality (experts only)")),

		MENU_SEP(N_("Save & Exit")),
		MENU_ENT_E('w', N_("write table to disk and exit"), FDISK_DISKLABEL_OSF),
		MENU_BENT ('q', N_("quit without saving changes")),
		MENU_XENT ('r', N_("return to main menu")),

		{ 0, NULL }
	}
};

struct menu menu_createlabel = {
/*	.callback = createlabel_menu_cb, */
	.exclude = FDISK_DISKLABEL_OSF,
	.entries = {
		MENU_SEP(N_("Create a new label")),
		MENU_ENT('g', N_("create a new empty GPT partition table")),
		MENU_ENT('G', N_("create a new empty SGI (IRIX) partition table")),
		MENU_ENT('o', N_("create a new empty DOS partition table")),
		MENU_ENT('s', N_("create a new empty Sun partition table")),

		/* backward compatibility -- be sensitive to 'g', but don't
		 * print it in the expert menu */
		MENU_XENT_H('g', N_("create an IRIX (SGI) partition table")),
		{ 0, NULL }
	}
};

struct menu menu_gpt = {
/*	.callback = gpt_menu_cb, */
	.label = FDISK_DISKLABEL_GPT,
	.entries = {
		MENU_XSEP(N_("GPT")),
		MENU_XENT('u', N_("change partition UUID")),
		MENU_XENT('n', N_("change partition name")),
		{ 0, NULL }
	}
};

static const struct menu *menus[] = {
	&menu_generic,
	&menu_createlabel,
	&menu_gpt
};

static const struct menu_entry *next_menu_entry(
			struct fdisk_context *cxt,
			struct menu_context *mc)
{
	while (mc->menu_idx < ARRAY_SIZE(menus)) {
		const struct menu *m = menus[mc->menu_idx];
		const struct menu_entry *e = &(m->entries[mc->entry_idx]);

		/* move to the next submenu if there is no more entries */
		if (e->title == NULL ||
		    (m->label && cxt->label && !(m->label & cxt->label->id))) {
			mc->menu_idx++;
			mc->entry_idx = 0;
			continue;
		}

		/* is the entry excluded for the current label? */
		if ((e->exclude && cxt->label &&
		     e->exclude & cxt->label->id) ||
		/* exclude non-expert entries in expect mode */
		    (e->expert == 0 && fdisk_context_display_details(cxt)) ||
		/* exclude non-normal entries in normal mode */
		    (e->normal == 0 && !fdisk_context_display_details(cxt))) {

			mc->entry_idx++;
			continue;
		}
		mc->entry_idx++;
		return e;

	}
	return NULL;
}

static int print_fdisk_menu(struct fdisk_context *cxt)
{
	struct menu_context mc = MENU_CXT_EMPTY;
	const struct menu_entry *e;

	if (fdisk_context_display_details(cxt))
		printf(_("\nExpert commands:\n"));
	else
		printf(_("\nCommands:\n"));

	while ((e = next_menu_entry(cxt, &mc))) {
		if (IS_MENU_HID(e))
			continue;	/* hidden entry */
		if (IS_MENU_SEP(e))
			printf("\n  %s\n", _(e->title));
		else
			printf("   %c   %s\n", e->key, _(e->title));
	}
	fputc('\n', stdout);

	return 0;
}

#ifdef TEST_PROGRAM
struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_mac_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt) { return NULL; }

int main(int argc, char *argv[])
{
	struct fdisk_context *cxt;
	int idx = 1;

	fdisk_init_debug(0);
	cxt = fdisk_new_context();

	if (argc > idx && strcmp(argv[idx], "--expert") == 0) {
		fdisk_context_enable_details(cxt, 1);
		idx++;
	}
	fdisk_context_switch_label(cxt, argc > idx ? argv[idx] : "gpt");

	print_fdisk_menu(cxt);
	return 0;
}
#endif
