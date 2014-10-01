/*
 * cfdisk.c - Display or manipulate a disk partition table.
 *
 *     Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *     Copyright (C) 1994 Kevin E. Martin (martin@cs.unc.edu)
 *
 *     The original cfdisk was inspired by the fdisk program
 *           by A. V. Le Blanc (leblanc@mcc.ac.uk.
 *
 * cfdisk is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <libsmartcols.h>
#include <sys/ioctl.h>

#ifdef HAVE_SLANG_H
# include <slang.h>
#elif defined(HAVE_SLANG_SLANG_H)
# include <slang/slang.h>
#endif

#ifdef HAVE_SLCURSES_H
# include <slcurses.h>
#elif defined(HAVE_SLANG_SLCURSES_H)
# include <slang/slcurses.h>
#elif defined(HAVE_NCURSESW_NCURSES_H) && defined(HAVE_WIDECHAR)
# include <ncursesw/ncurses.h>
#elif defined(HAVE_NCURSES_H)
# include <ncurses.h>
#elif defined(HAVE_NCURSES_NCURSES_H)
# include <ncurses/ncurses.h>
#endif

#ifdef HAVE_WIDECHAR
# include <wctype.h>
# include <wchar.h>
#endif

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "mbsalign.h"
#include "colors.h"

#include "fdiskP.h"

#ifdef __GNU__
# define DEFAULT_DEVICE "/dev/hd0"
# define ALTERNATE_DEVICE "/dev/sd0"
#elif defined(__FreeBSD__)
# define DEFAULT_DEVICE "/dev/ad0"
# define ALTERNATE_DEVICE "/dev/da0"
#else
# define DEFAULT_DEVICE "/dev/sda"
# define ALTERNATE_DEVICE "/dev/hda"
#endif

#define ARROW_CURSOR_STRING	">>  "
#define ARROW_CURSOR_DUMMY	"    "
#define ARROW_CURSOR_WIDTH	(sizeof(ARROW_CURSOR_STRING) - 1)

#define MENU_PADDING		2
#define TABLE_START_LINE	4
#define MENU_START_LINE		(ui_lines - 5)
#define INFO_LINE		(ui_lines - 2)
#define HINT_LINE		(ui_lines - 1)

#define CFDISK_ERR_ESC		5000

#ifndef KEY_ESC
# define KEY_ESC	'\033'
#endif
#ifndef KEY_DELETE
# define KEY_DELETE	'\177'
#endif

/* colors */
enum {
	CFDISK_CL_NONE = 0,
	CFDISK_CL_WARNING,
	CFDISK_CL_FREESPACE,
};
static const int color_pairs[][2] = {
	/* color            foreground, background */
	[CFDISK_CL_WARNING]   = { COLOR_RED, -1 },
	[CFDISK_CL_FREESPACE] = { COLOR_GREEN, -1 }
};

struct cfdisk;

static struct cfdisk_menuitem *menu_get_menuitem(struct cfdisk *cf, size_t idx);
static struct cfdisk_menuitem *menu_get_menuitem_by_key(struct cfdisk *cf, int key, size_t *idx);
static struct cfdisk_menu *menu_push(struct cfdisk *cf, struct cfdisk_menuitem *item);
static struct cfdisk_menu *menu_pop(struct cfdisk *cf);
static void menu_refresh_size(struct cfdisk *cf);

static int ui_refresh(struct cfdisk *cf);
static void ui_warnx(const char *fmt, ...);
static void ui_warn(const char *fmt, ...);
static void ui_info(const char *fmt, ...);
static void ui_draw_menu(struct cfdisk *cf);
static int ui_menu_move(struct cfdisk *cf, int key);
static void ui_menu_resize(struct cfdisk *cf);

static int ui_get_size(struct cfdisk *cf, const char *prompt, uintmax_t *res,
		       uintmax_t low, uintmax_t up);

static int ui_enabled;
static int ui_resize;

/* ncurses LINES and COLS may be actual variables or *macros*, but we need
 * something portable and writable */
size_t ui_lines;
size_t ui_cols;

/* menu item */
struct cfdisk_menuitem {
	int		key;		/* keyboard shortcut */
	const char	*name;		/* item name */
	const char	*desc;		/* item description (hint) */
	void		*userdata;
};

/* menu */
struct cfdisk_menu {
	char			*title;	/* optional menu title */
	struct cfdisk_menuitem	*items;	/* array with menu items */
	char			*ignore;/* string with keys to ignore */
	size_t			width;	/* maximal width of the menu item */
	size_t			nitems;	/* number of the active menu items */
	size_t			page_sz;/* when menu longer than screen */
	size_t			idx;	/* the current menu item */
	struct cfdisk_menu	*prev;

	/* @ignore keys generator */
	int (*ignore_cb)	(struct cfdisk *, char *, size_t);

	unsigned int		vertical : 1;	/* enable vertical mode */
};

/* main menu */
static struct cfdisk_menuitem main_menuitems[] = {
	{ 'b', N_("Bootable"), N_("Toggle bootable flag of the current partition") },
	{ 'd', N_("Delete"), N_("Delete the current partition") },
	{ 'n', N_("New"), N_("Create new partition from free space") },
	{ 'q', N_("Quit"), N_("Quit program without writing partition table") },
	{ 't', N_("Type"), N_("Change the partition type") },
	{ 'h', N_("Help"), N_("Print help screen") },
	{ 's', N_("Sort"), N_("Fix partitions order") },
	{ 'W', N_("Write"), N_("Write partition table to disk (this might destroy data)") },
	{ 0, NULL, NULL }
};

/* top level control struct */
struct cfdisk {
	struct fdisk_context	*cxt;	/* libfdisk context */
	struct fdisk_table	*table;	/* partition table */
	struct cfdisk_menu	*menu;	/* the current menu */

	int	*cols;		/* output columns */
	size_t	ncols;		/* number of columns */

	char	*linesbuf;	/* table as string */
	size_t	linesbufsz;	/* size of the tb_buf */

	char	**lines;	/* array with lines */
	size_t	nlines;		/* number of lines */
	size_t	lines_idx;	/* current line <0..N>, exclude header */
	size_t  page_sz;

	unsigned int nwrites;	/* fdisk_write_disklabel() counter */

	unsigned int	wrong_order :1,		/* PT not in right order */
			zero_start :1;		/* ignore existing partition table */
};

/* Initialize output columns -- we follow libcfdisk columns (usually specific
 * to the label type.
 */
static int cols_init(struct cfdisk *cf)
{
	assert(cf);

	free(cf->cols);
	cf->cols = NULL;
	cf->ncols = 0;

	return fdisk_get_columns(cf->cxt, 0, &cf->cols, &cf->ncols);
}

static void resize(void)
{
	struct winsize ws;

	if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) != -1
	    && ws.ws_row && ws.ws_col) {
		ui_lines = ws.ws_row;
		ui_cols  = ws.ws_col;
#if HAVE_RESIZETERM
		resizeterm(ws.ws_row, ws.ws_col);
#endif
		clearok(stdscr, TRUE);
	}
	touchwin(stdscr);

	DBG(FRONTEND, ul_debug("ui: resize refresh ui_cols=%zu, ui_lines=%zu",
				ui_cols, ui_lines));
	ui_resize = 0;
}

/* Reads partition in tree-like order from scols
 */
static int partition_from_scols(struct fdisk_table *tb,
				struct libscols_line *ln)
{
	struct fdisk_partition *pa = scols_line_get_userdata(ln);

	fdisk_table_add_partition(tb, pa);
	fdisk_unref_partition(pa);

	if (scols_line_has_children(ln)) {
		struct libscols_line *chln;
		struct libscols_iter *itr = scols_new_iter(SCOLS_ITER_FORWARD);

		if (!itr)
			return -EINVAL;
		while (scols_line_next_child(ln, itr, &chln) == 0)
			partition_from_scols(tb, chln);
		scols_free_iter(itr);
	}
	return 0;
}

/* It would be possible to use fdisk_table_to_string(), but we want some
 * extension to the output format, so let's do it without libfdisk
 */
static char *table_to_string(struct cfdisk *cf, struct fdisk_table *tb)
{
	const struct fdisk_column *col;
	struct fdisk_partition *pa;
	struct fdisk_label *lb;
	struct fdisk_iter *itr = NULL;
	struct libscols_table *table = NULL;
	struct libscols_iter *s_itr = NULL;
	char *res = NULL;
	size_t i;
	int tree = 0;
	struct libscols_line *ln, *ln_cont = NULL;

	DBG(FRONTEND, ul_debug("table: convert to string"));

	assert(cf);
	assert(cf->cxt);
	assert(cf->cols);
	assert(tb);

	lb = fdisk_context_get_label(cf->cxt, NULL);
	assert(lb);

	itr = fdisk_new_iter(FDISK_ITER_FORWARD);
	if (!itr)
		goto done;

	/* get container (e.g. extended partition) */
	while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
		if (fdisk_partition_is_nested(pa)) {
			DBG(FRONTEND, ul_debug("table: nested detected, using tree"));
			tree = SCOLS_FL_TREE;
			break;
		}
	}

	table = scols_new_table();
	if (!table)
		goto done;
	scols_table_enable_maxout(table, 1);

	/* headers */
	for (i = 0; i < cf->ncols; i++) {
		col = fdisk_label_get_column(lb, cf->cols[i]);
		if (col) {
			int fl = col->scols_flags;
			if (tree && col->id == FDISK_COL_DEVICE)
				fl |= SCOLS_FL_TREE;
			if (!scols_table_new_column(table, col->name, col->width, fl))
				goto done;
		}
	}

	/* data */
	fdisk_reset_iter(itr, FDISK_ITER_FORWARD);

	while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
		struct libscols_line *parent = fdisk_partition_is_nested(pa) ? ln_cont : NULL;

		ln = scols_table_new_line(table, parent);
		if (!ln)
			goto done;
		for (i = 0; i < cf->ncols; i++) {
			char *cdata = NULL;
			col = fdisk_label_get_column(lb, cf->cols[i]);
			if (!col)
				continue;
			if (fdisk_partition_to_string(pa, cf->cxt, col->id, &cdata))
				continue;
			scols_line_refer_data(ln, i, cdata);
		}
		if (tree && fdisk_partition_is_container(pa))
			ln_cont = ln;

		scols_line_set_userdata(ln, (void *) pa);
		fdisk_ref_partition(pa);
	}

	if (scols_table_is_empty(table))
		goto done;

	scols_table_reduce_termwidth(table, ARROW_CURSOR_WIDTH);
	scols_print_table_to_string(table, &res);

	/* scols_* code might reorder lines, let's reorder @tb according to the
	 * final output (it's no problem because partitions are addressed by
	 * parno stored within struct fdisk_partition)  */

	/* remove all */
	fdisk_reset_iter(itr, FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(tb, itr, &pa) == 0)
		fdisk_table_remove_partition(tb, pa);

	s_itr = scols_new_iter(SCOLS_ITER_FORWARD);
	if (!s_itr)
		goto done;

	/* add all in the right order (don't forget the output is tree) */
	while (scols_table_next_line(table, s_itr, &ln) == 0) {
		if (scols_line_get_parent(ln))
			continue;
		if (partition_from_scols(tb, ln))
			break;
	}
done:
	scols_unref_table(table);
	scols_free_iter(s_itr);
	fdisk_free_iter(itr);

	return res;
}

/*
 * Read data about partitions from libfdisk and prepare output lines.
 */
static int lines_refresh(struct cfdisk *cf)
{
	int rc;
	char *p;
	size_t i;

	assert(cf);

	DBG(FRONTEND, ul_debug("refreshing buffer"));

	free(cf->linesbuf);
	free(cf->lines);
	cf->linesbuf = NULL;
	cf->linesbufsz = 0;
	cf->lines = NULL;
	cf->nlines = 0;

	fdisk_unref_table(cf->table);
	cf->table = NULL;

	/* read partitions and free spaces into cf->table */
	rc = fdisk_get_partitions(cf->cxt, &cf->table);
	if (!rc)
		rc = fdisk_get_freespaces(cf->cxt, &cf->table);
	if (rc)
		return rc;

	cf->linesbuf = table_to_string(cf, cf->table);
	if (!cf->linesbuf)
		return -ENOMEM;

	cf->linesbufsz = strlen(cf->linesbuf);
	cf->nlines = fdisk_table_get_nents(cf->table) + 1;	/* 1 for header line */
	cf->page_sz = 0;
	cf->wrong_order = fdisk_table_wrong_order(cf->table) ? 1 : 0;

	if (MENU_START_LINE - TABLE_START_LINE < cf->nlines)
		cf->page_sz = MENU_START_LINE - TABLE_START_LINE - 1;

	cf->lines = xcalloc(cf->nlines, sizeof(char *));

	for (p = cf->linesbuf, i = 0; p && i < cf->nlines; i++) {
		cf->lines[i] = p;
		p = strchr(p, '\n');
		if (p) {
			*p = '\0';
			p++;
		}
	}

	return 0;
}

static struct fdisk_partition *get_current_partition(struct cfdisk *cf)
{
	assert(cf);
	assert(cf->table);

	return fdisk_table_get_partition(cf->table, cf->lines_idx);
}

static int is_freespace(struct cfdisk *cf, size_t i)
{
	struct fdisk_partition *pa;

	assert(cf);
	assert(cf->table);

	pa = fdisk_table_get_partition(cf->table, i);
	return fdisk_partition_is_freespace(pa);
}

/* converts libfdisk FDISK_ASKTYPE_MENU to cfdisk menu and returns user's
 * responseback to libfdisk
 */
static int ask_menu(struct fdisk_ask *ask, struct cfdisk *cf)
{
	struct cfdisk_menuitem *d, *cm;
	int key;
	size_t i = 0, nitems;
	const char *name, *desc;

	assert(ask);
	assert(cf);

	/* create cfdisk menu according to libfdisk ask-menu, note that the
	 * last cm[] item has to be empty -- so nitems + 1 */
	nitems = fdisk_ask_menu_get_nitems(ask);
	cm = xcalloc(nitems + 1, sizeof(struct cfdisk_menuitem));

	for (i = 0; i < nitems; i++) {
		if (fdisk_ask_menu_get_item(ask, i, &key, &name, &desc))
			break;
		cm[i].key = key;
		cm[i].desc = desc;
		cm[i].name = name;
	}

	/* make the new menu active */
	menu_push(cf, cm);
	ui_draw_menu(cf);
	refresh();

	/* wait for keys */
	do {
		int key = getch();

		if (ui_resize)
			ui_menu_resize(cf);
		if (ui_menu_move(cf, key) == 0)
			continue;

		switch (key) {
		case KEY_ENTER:
		case '\n':
		case '\r':
			d = menu_get_menuitem(cf, cf->menu->idx);
			if (d)
				fdisk_ask_menu_set_result(ask, d->key);
			menu_pop(cf);
			free(cm);
			return 0;
		}
	} while (1);

	menu_pop(cf);
	free(cm);
	return -1;
}

/* libfdisk callback
 */
static int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)))
{
	int rc = 0;

	assert(cxt);
	assert(ask);

	switch(fdisk_ask_get_type(ask)) {
	case FDISK_ASKTYPE_INFO:
		ui_info(fdisk_ask_print_get_mesg(ask));
		break;
	case FDISK_ASKTYPE_WARNX:
		ui_warnx(fdisk_ask_print_get_mesg(ask));
		break;
	case FDISK_ASKTYPE_WARN:
		ui_warn(fdisk_ask_print_get_mesg(ask));
		break;
	case FDISK_ASKTYPE_MENU:
		ask_menu(ask, (struct cfdisk *) data);
		break;
	default:
		ui_warnx(_("internal error: unsupported dialog type %d"),
			fdisk_ask_get_type(ask));
		return -EINVAL;
	}
	return rc;
}

static int ui_end(void)
{
	if (!ui_enabled)
		return -EINVAL;

#if defined(HAVE_SLCURSES_H) || defined(HAVE_SLANG_SLCURSES_H)
	SLsmg_gotorc(ui_lines - 1, 0);
	SLsmg_refresh();
#else
	mvcur(0, ui_cols - 1, ui_lines-1, 0);
#endif
	curs_set(1);
	nl();
	endwin();
	printf("\n");
	ui_enabled = 0;
	return 0;
}

static void ui_vprint_center(size_t line, int attrs, const char *fmt, va_list ap)
{
	size_t width;
	char *buf = NULL;

	move(line, 0);
	clrtoeol();

	xvasprintf(&buf, fmt, ap);

	width = mbs_safe_width(buf);
	if (width > (size_t) ui_cols) {
		char *p = strrchr(buf + ui_cols, ' ');
		if (!p)
			p = buf + ui_cols;
		*p = '\0';
		if (line + 1 >= ui_lines)
			line--;
		attron(attrs);
		mvaddstr(line, 0, buf);
		mvaddstr(line + 1, 0, p+1);
		attroff(attrs);
	} else {
		attron(attrs);
		mvaddstr(line, (ui_cols - width) / 2, buf);
		attroff(attrs);
	}
	free(buf);
}

static void ui_center(size_t line, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ui_vprint_center(line, 0, fmt, ap);
	va_end(ap);
}

static void ui_warnx(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (ui_enabled)
		ui_vprint_center(INFO_LINE,
			colors_wanted() ? COLOR_PAIR(CFDISK_CL_WARNING) : 0,
			fmt, ap);
	else {
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
	}
	va_end(ap);
}

static void ui_warn(const char *fmt, ...)
{
	char *fmt_m;
	va_list ap;

	xasprintf(&fmt_m, "%s: %m", fmt);

	va_start(ap, fmt);
	if (ui_enabled)
		ui_vprint_center(INFO_LINE,
			colors_wanted() ? COLOR_PAIR(CFDISK_CL_WARNING) : 0,
			fmt_m, ap);
	else {
		vfprintf(stderr, fmt_m, ap);
		fputc('\n', stderr);
	}
	va_end(ap);
	free(fmt_m);
}

static int __attribute__((__noreturn__)) ui_errx(int rc, const char *fmt, ...)
		{
	va_list ap;
	ui_end();

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", program_invocation_short_name);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);

	exit(rc);
}

static void ui_info(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (ui_enabled)
		ui_vprint_center(INFO_LINE, A_BOLD, fmt, ap);
	else {
		vfprintf(stdout, fmt, ap);
		fputc('\n', stdout);
	}
	va_end(ap);
}

static void ui_clean_info(void)
{
	move(INFO_LINE, 0);
	clrtoeol();
}

static void ui_hint(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (ui_enabled)
		ui_vprint_center(HINT_LINE, A_BOLD, fmt, ap);
	else {
		vfprintf(stdout, fmt, ap);
		fputc('\n', stdout);
	}
	va_end(ap);
}

static void ui_clean_hint(void)
{
	move(HINT_LINE, 0);
	clrtoeol();
}

static void die_on_signal(int dummy __attribute__((__unused__)))
{
	DBG(FRONTEND, ul_debug("die on signal."));
	ui_end();
	exit(EXIT_FAILURE);
}

static void resize_on_signal(int dummy __attribute__((__unused__)))
{
	DBG(FRONTEND, ul_debug("resize on signal."));
	ui_resize = 1;
}

static void menu_refresh_size(struct cfdisk *cf)
{
	if (cf->menu && cf->menu->nitems)
		cf->menu->page_sz = (cf->menu->nitems / (ui_lines - 4)) ? ui_lines - 4 : 0;
}

static void menu_update_ignore(struct cfdisk *cf)
{
	char ignore[128] = { 0 };
	int i = 0;
	struct cfdisk_menu *m;
	struct cfdisk_menuitem *d, *org;
	size_t idx;

	assert(cf);
	assert(cf->menu);
	assert(cf->menu->ignore_cb);

	m = cf->menu;
	org = menu_get_menuitem(cf, m->idx);

	DBG(FRONTEND, ul_debug("menu: update menu ignored keys"));

	i = m->ignore_cb(cf, ignore, sizeof(ignore));
	ignore[i] = '\0';

	/* return if no change */
	if (   (!m->ignore && !*ignore)
	    || (m->ignore && *ignore && strcmp(m->ignore, ignore) == 0)) {
		    return;
	}

	free(m->ignore);
	m->ignore = xstrdup(ignore);
	m->nitems = 0;

	for (d = m->items; d->name; d++) {
		if (m->ignore && strchr(m->ignore, d->key))
			continue;
		m->nitems++;
	}

	/* refresh menu index to be at the same menuitem or go to the first */
	if (org && menu_get_menuitem_by_key(cf, org->key, &idx))
		m->idx = idx;
	else
		m->idx = 0;

	menu_refresh_size(cf);
}

static struct cfdisk_menu *menu_push(
			struct cfdisk *cf,
			struct cfdisk_menuitem *items)
{
	struct cfdisk_menu *m = xcalloc(1, sizeof(*m));
	struct cfdisk_menuitem *d;

	assert(cf);

	DBG(FRONTEND, ul_debug("menu: new menu"));

	m->prev = cf->menu;
	m->items = items;

	for (d = m->items; d->name; d++) {
		const char *name = _(d->name);
		size_t len = mbs_safe_width(name);
		if (len > m->width)
			m->width = len;
		m->nitems++;
	}

	cf->menu = m;
	menu_refresh_size(cf);
	return m;
}

static struct cfdisk_menu *menu_pop(struct cfdisk *cf)
{
	struct cfdisk_menu *m = NULL;

	assert(cf);

	DBG(FRONTEND, ul_debug("menu: rem menu"));

	if (cf->menu) {
		m = cf->menu->prev;
		free(cf->menu->ignore);
		free(cf->menu->title);
		free(cf->menu);
	}
	cf->menu = m;
	return cf->menu;
}

static void menu_set_title(struct cfdisk_menu *m, const char *title)
{
	char *str = NULL;

	if (title) {
		size_t len =  mbs_safe_width(title);
		if (len + 3 > m->width)
			m->width = len + 3;
		str = xstrdup(title);
	}
	m->title = str;
}


static int ui_init(struct cfdisk *cf __attribute__((__unused__)))
{
	struct sigaction sa;

	DBG(FRONTEND, ul_debug("ui: init"));

	/* setup SIGCHLD handler */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = die_on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	sa.sa_handler = resize_on_signal;
	sigaction(SIGWINCH, &sa, NULL);

	ui_enabled = 1;
	initscr();

#ifdef HAVE_USE_DEFAULT_COLORS
	if (colors_wanted() && has_colors()) {
		size_t i;

		start_color();
		use_default_colors();
		for (i = 1; i < ARRAY_SIZE(color_pairs); i++)		/* yeah, start from 1! */
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}
#else
	colors_off();
#endif

	cbreak();
	noecho();
	nonl();
	curs_set(0);
	keypad(stdscr, TRUE);

	return 0;
}

static size_t menuitem_get_line(struct cfdisk *cf, size_t idx)
{
	struct cfdisk_menu *m = cf->menu;

	if (m->vertical) {
		if (!m->page_sz)				/* small menu */
			return (ui_lines - (cf->menu->nitems + 1)) / 2 + idx;
		return (idx % m->page_sz) + 1;
	} else {
		size_t len = m->width + 4 + MENU_PADDING;	/* item width */
		size_t items = ui_cols / len;			/* items per line */

		if (items == 0)
			return 0;

		return MENU_START_LINE + ((idx / items));
	}
}

static int menuitem_get_column(struct cfdisk *cf, size_t idx)
{
	if (cf->menu->vertical) {
		size_t nc = cf->menu->width + MENU_PADDING;
		if ((size_t) ui_cols <= nc)
			return 0;
		return (ui_cols - nc) / 2;
	} else {
		size_t len = cf->menu->width + 4 + MENU_PADDING;	/* item width */
		size_t items = ui_cols / len;				/* items per line */
		size_t extra = items < cf->menu->nitems ?		/* extra space on line */
				ui_cols % len :				/* - multi-line menu */
				ui_cols - (cf->menu->nitems * len);	/* - one line menu */

		if (items == 0)
			return 0;					/* hmm... no space */

		extra += MENU_PADDING;		/* add padding after last item to extra */

		if (idx < items)
			return (idx * len) + (extra / 2);
		return ((idx % items) * len) + (extra / 2);
	}
}

static int menuitem_on_page(struct cfdisk *cf, size_t idx)
{
	struct cfdisk_menu *m = cf->menu;

	if (m->page_sz == 0 ||
	    m->idx / m->page_sz == idx / m->page_sz)
		return 1;
	return 0;
}

static struct cfdisk_menuitem *menu_get_menuitem(struct cfdisk *cf, size_t idx)
{
	struct cfdisk_menuitem *d;
	size_t i;

	for (i = 0, d = cf->menu->items; d->name; d++) {
		if (cf->menu->ignore && strchr(cf->menu->ignore, d->key))
			continue;
		if (i++ == idx)
			return d;
	}

	return NULL;
}

static struct cfdisk_menuitem *menu_get_menuitem_by_key(struct cfdisk *cf,
							int key, size_t *idx)
{
	struct cfdisk_menuitem *d;

	for (*idx = 0, d = cf->menu->items; d->name; d++) {
		if (cf->menu->ignore && strchr(cf->menu->ignore, d->key))
			continue;
		if (key == d->key)
			return d;
		(*idx)++;
	}

	return NULL;
}

static void ui_draw_menuitem(struct cfdisk *cf,
			     struct cfdisk_menuitem *d,
			     size_t idx)
{
	char buf[80 * MB_CUR_MAX];
	const char *name;
	size_t width = cf->menu->width + 2;	/* 2 = blank around string */
	int ln, cl, vert = cf->menu->vertical;

	if (!menuitem_on_page(cf, idx))
		return;		/* no visible item */
	ln = menuitem_get_line(cf, idx);
	cl = menuitem_get_column(cf, idx);

	name = _(d->name);
	mbsalign(name, buf, sizeof(buf), &width,
			vert ? MBS_ALIGN_LEFT : MBS_ALIGN_CENTER,
			0);

	DBG(FRONTEND, ul_debug("ui: menuitem: cl=%d, ln=%d, item='%s'",
			cl, ln, buf));

	if (vert) {
		mvaddch(ln, cl - 1, ACS_VLINE);
		mvaddch(ln, cl + cf->menu->width + 4, ACS_VLINE);
	}

	if (cf->menu->idx == idx) {
		standout();
		mvprintw(ln, cl, vert ? " %s " : "[%s]", buf);
		standend();
		if (d->desc)
			ui_hint(d->desc);
	} else
		mvprintw(ln, cl, vert ? " %s " : "[%s]", buf);
}

static void ui_clean_menu(struct cfdisk *cf)
{
	size_t i;
	size_t nlines;
	struct cfdisk_menu *m = cf->menu;
	size_t ln = menuitem_get_line(cf, 0);

	if (m->vertical)
		nlines = m->page_sz ? m->page_sz : m->nitems;
	else
		nlines = menuitem_get_line(cf, m->nitems);

	for (i = ln; i <= ln + nlines; i++) {
		move(i, 0);
		clrtoeol();
	}
	if (m->vertical) {
		move(ln - 1, 0);
		clrtoeol();
	}
	ui_clean_hint();
}

static void ui_draw_menu(struct cfdisk *cf)
{
	struct cfdisk_menuitem *d;
	struct cfdisk_menu *m;
	size_t i = 0;
	size_t ln = menuitem_get_line(cf, 0);
	size_t nlines;

	assert(cf);
	assert(cf->menu);

	DBG(FRONTEND, ul_debug("ui: menu: draw start"));

	ui_clean_menu(cf);
	m = cf->menu;

	if (m->vertical)
		nlines = m->page_sz ? m->page_sz : m->nitems;
	else
		nlines = menuitem_get_line(cf, m->nitems);

	if (m->ignore_cb)
		menu_update_ignore(cf);
	i = 0;
	while ((d = menu_get_menuitem(cf, i)))
		ui_draw_menuitem(cf, d, i++);

	if (m->vertical) {
		size_t cl = menuitem_get_column(cf, 0);
		size_t curpg = m->page_sz ? m->idx / m->page_sz : 0;

		/* corners and horizontal lines */
		mvaddch(ln - 1, cl - 1, ACS_ULCORNER);
		mvaddch(ln + nlines, cl - 1, ACS_LLCORNER);

		for (i = 0; i < m->width + 4; i++) {
			mvaddch(ln - 1, cl + i, ACS_HLINE);
			mvaddch(ln + nlines, cl + i, ACS_HLINE);
		}

		mvaddch(ln - 1, cl + i, ACS_URCORNER);
		mvaddch(ln + nlines, cl + i, ACS_LRCORNER);

		/* draw also lines around empty lines on last page */
		if (m->page_sz &&
		    m->nitems / m->page_sz == m->idx / m->page_sz) {
			for (i = m->nitems % m->page_sz + 1; i <= m->page_sz; i++) {
				mvaddch(i, cl - 1, ACS_VLINE);
				mvaddch(i, cl + cf->menu->width + 4, ACS_VLINE);
			}
		}
		if (m->title) {
			attron(A_BOLD);
			mvprintw(ln - 1, cl, " %s ", m->title);
			attroff(A_BOLD);
		}
		if (curpg != 0)
			mvaddch(ln - 1, cl + m->width + 3, ACS_UARROW);
		if (m->page_sz && curpg < m->nitems / m->page_sz)
			mvaddch(ln + nlines, cl + m->width + 3, ACS_DARROW);
	}

	DBG(FRONTEND, ul_debug("ui: menu: draw end."));
}

static void ui_menu_goto(struct cfdisk *cf, int where)
{
	struct cfdisk_menuitem *d;
	size_t old;

	/* stop and begin/end for vertical menus */
	if (cf->menu->vertical) {
		if (where < 0)
			where = 0;
		else if (where > (int) cf->menu->nitems - 1)
			where = cf->menu->nitems - 1;
	} else {
		/* continue from begin/end */
		if (where < 0)
			where = cf->menu->nitems - 1;
		else if ((size_t) where > cf->menu->nitems - 1)
			where = 0;
	}
	if ((size_t) where == cf->menu->idx)
		return;

	ui_clean_info();

	old = cf->menu->idx;
	cf->menu->idx = where;

	if (!menuitem_on_page(cf, old)) {
		ui_draw_menu(cf);
		return;
	}

	d = menu_get_menuitem(cf, old);
	ui_draw_menuitem(cf, d, old);

	d = menu_get_menuitem(cf, where);
	ui_draw_menuitem(cf, d, where);
}

static int ui_menu_move(struct cfdisk *cf, int key)
{
	struct cfdisk_menu *m;

	assert(cf);
	assert(cf->menu);

	if (key == ERR)
		return 0;	/* ignore errors */

	m = cf->menu;

	DBG(FRONTEND, ul_debug("ui: menu move key >%c<.", key));

	if (m->vertical)
	{
		switch (key) {
		case KEY_DOWN:
		case '\016':	/* ^N */
		case 'j':	/* Vi-like alternative */
			ui_menu_goto(cf, m->idx + 1);
			return 0;
		case KEY_UP:
		case '\020':	/* ^P */
		case 'k':	/* Vi-like alternative */
			ui_menu_goto(cf, (int) m->idx - 1);
			return 0;
		case KEY_PPAGE:
			if (m->page_sz) {
				ui_menu_goto(cf, (int) m->idx - m->page_sz);
				return 0;
			}
			/* fallthrough */
		case KEY_HOME:
			ui_menu_goto(cf, 0);
			return 0;
		case KEY_NPAGE:
			if (m->page_sz) {
				ui_menu_goto(cf, m->idx + m->page_sz);
				return 0;
			}
			/* fallthrough */
		case KEY_END:
			ui_menu_goto(cf, m->nitems);
			return 0;
		}
	} else {
		switch (key) {
		case KEY_RIGHT:
		case '\t':
			ui_menu_goto(cf, m->idx + 1);
			return 0;
		case KEY_LEFT:
#ifdef KEY_BTAB
		case KEY_BTAB:
#endif
			ui_menu_goto(cf, (int) m->idx - 1);
			return 0;
		}
	}

	return 1;	/* key irrelevant for menu move */
}

/* but don't call me from ui_run(), this is for pop-up menus only */
static void ui_menu_resize(struct cfdisk *cf)
{
	resize();
	ui_clean_menu(cf);
	menu_refresh_size(cf);
	ui_draw_menu(cf);
	refresh();
}

static int partition_on_page(struct cfdisk *cf, size_t i)
{
	if (cf->page_sz == 0 ||
	    cf->lines_idx / cf->page_sz == i / cf->page_sz)
		return 1;
	return 0;
}

static void ui_draw_partition(struct cfdisk *cf, size_t i)
{
	int ln = TABLE_START_LINE + 1 + i;	/* skip table header */
	int cl = ARROW_CURSOR_WIDTH;		/* we need extra space for cursor */
	int cur = cf->lines_idx == i;
	size_t curpg = 0;

	if (cf->page_sz) {
		if (!partition_on_page(cf, i))
			return;
		ln = TABLE_START_LINE + (i % cf->page_sz) + 1;
		curpg = cf->lines_idx / cf->page_sz;
	}

	DBG(FRONTEND, ul_debug(
			"ui: draw partition %zu [page_sz=%zu, "
			"line=%d, idx=%zu]",
			i, cf->page_sz, ln, cf->lines_idx));

	if (cur) {
		attron(A_REVERSE);
		mvaddstr(ln, 0, ARROW_CURSOR_STRING);
		mvaddstr(ln, cl, cf->lines[i + 1]);
		attroff(A_REVERSE);
	} else {
		int at = 0;

		if (colors_wanted() && is_freespace(cf, i)) {
			attron(COLOR_PAIR(CFDISK_CL_FREESPACE));
			at = 1;
		}
		mvaddstr(ln, 0, ARROW_CURSOR_DUMMY);
		mvaddstr(ln, cl, cf->lines[i + 1]);
		if (at)
                        attroff(COLOR_PAIR(CFDISK_CL_FREESPACE));
	}

	if ((size_t) ln == MENU_START_LINE - 1 &&
	    cf->page_sz && curpg < cf->nlines / cf->page_sz) {
		if (cur)
			attron(A_REVERSE);
		mvaddch(ln, ui_cols - 1, ACS_DARROW);
		mvaddch(ln, 0, ACS_DARROW);
		if (cur)
			attroff(A_REVERSE);
	}
}

static int ui_draw_table(struct cfdisk *cf)
{
	int cl = ARROW_CURSOR_WIDTH;
	size_t i, nparts = fdisk_table_get_nents(cf->table);
	size_t curpg = cf->page_sz ? cf->lines_idx / cf->page_sz : 0;

	DBG(FRONTEND, ul_debug("ui: draw table"));

	for (i = TABLE_START_LINE; i <= TABLE_START_LINE + cf->page_sz; i++) {
		move(i, 0);
		clrtoeol();
	}

	if ((size_t) cf->lines_idx > nparts - 1)
		cf->lines_idx = nparts ? nparts - 1 : 0;

	/* print header */
	attron(A_BOLD);
	mvaddstr(TABLE_START_LINE, cl, cf->lines[0]);
	attroff(A_BOLD);

	/* print partitions */
	for (i = 0; i < nparts; i++)
		ui_draw_partition(cf, i);

	if (curpg != 0) {
		mvaddch(TABLE_START_LINE, ui_cols - 1, ACS_UARROW);
		mvaddch(TABLE_START_LINE, 0, ACS_UARROW);
	}
	if (cf->page_sz && curpg < cf->nlines / cf->page_sz) {
		mvaddch(MENU_START_LINE - 1, ui_cols - 1, ACS_DARROW);
		mvaddch(MENU_START_LINE - 1, 0, ACS_DARROW);
	}
	return 0;
}

static int ui_table_goto(struct cfdisk *cf, int where)
{
	size_t old;
	size_t nparts = fdisk_table_get_nents(cf->table);

	DBG(FRONTEND, ul_debug("ui: goto table %d", where));

	if (where < 0)
		where = 0;
	else if ((size_t) where > nparts - 1)
		where = nparts - 1;

	if ((size_t) where == cf->lines_idx)
		return 0;

	old = cf->lines_idx;
	cf->lines_idx = where;

	if (!partition_on_page(cf, old) ||!partition_on_page(cf, where))
		ui_draw_table(cf);
	else {
		ui_draw_partition(cf, old);	/* cleanup old */
		ui_draw_partition(cf, where);	/* draw new */
	}
	ui_clean_info();
	ui_draw_menu(cf);
	refresh();
	return 0;
}

static int ui_refresh(struct cfdisk *cf)
{
	char *id = NULL;
        uint64_t bytes = cf->cxt->total_sectors * cf->cxt->sector_size;
	char *strsz = size_to_human_string(SIZE_SUFFIX_SPACE
				| SIZE_SUFFIX_3LETTER, bytes);
	erase();

	if (!ui_enabled)
		return -EINVAL;

	/* header */
	attron(A_BOLD);
	ui_center(0, _("Disk: %s"), cf->cxt->dev_path);
	attroff(A_BOLD);
	ui_center(1, _("Size: %s, %ju bytes, %ju sectors"),
			strsz, bytes, (uintmax_t) cf->cxt->total_sectors);
	if (fdisk_get_disklabel_id(cf->cxt, &id) == 0 && id)
		ui_center(2, _("Label: %s, identifier: %s"),
				cf->cxt->label->name, id);
	else
		ui_center(2, _("Label: %s"), cf->cxt->label->name);
	free(strsz);

	ui_draw_table(cf);
	ui_draw_menu(cf);
	refresh();
	return 0;
}

static ssize_t ui_get_string(struct cfdisk *cf, const char *prompt,
			     const char *hint, char *buf, size_t len)
{
	size_t cells = 0;
	ssize_t i = 0, rc = -1;
	int ln = MENU_START_LINE, cl = 1;

	assert(cf);
	assert(buf);
	assert(len);

	move(ln, 0);
	clrtoeol();

	if (prompt) {
		mvaddstr(ln, cl, (char *) prompt);
		cl += mbs_safe_width(prompt);
	}

	/* default value */
	if (*buf) {
		i = strlen(buf);
		cells = mbs_safe_width(buf);
		mvaddstr(ln, cl, buf);
	}

	if (hint)
		ui_hint(hint);
	else
		ui_clean_hint();

	move(ln, cl + cells);
	curs_set(1);
	refresh();

	while (1) {
#if !defined(HAVE_SLCURSES_H) && !defined(HAVE_SLANG_SLCURSES_H) && \
    defined(HAVE_LIBNCURSESW) && defined(HAVE_WIDECHAR)
		wint_t c;
		if (get_wch(&c) == ERR) {
#else
		int c;
		if ((c = getch()) == ERR) {
#endif
			if (ui_resize) {
				resize();
				continue;
			}
			if (!isatty(STDIN_FILENO))
				exit(2);
			else
				goto done;
		}
		if (c == '\r' || c == '\n' || c == KEY_ENTER)
			break;

		switch (c) {
		case KEY_ESC:
			rc = -CFDISK_ERR_ESC;
			goto done;
		case KEY_DELETE:
		case '\b':
		case KEY_BACKSPACE:
			if (i > 0) {
				cells--;
				i = mbs_truncate(buf, &cells);
				if (i < 0)
					goto done;
				mvaddch(ln, cl + cells, ' ');
				move(ln, cl + cells);
			} else
				beep();
			break;
		default:
#if defined(HAVE_LIBNCURSESW) && defined(HAVE_WIDECHAR)
			if (i + 1 < (ssize_t) len && iswprint(c)) {
				wchar_t wc = (wchar_t) c;
				char s[MB_CUR_MAX + 1];
				int sz = wctomb(s, wc);

				if (sz > 0 && sz + i < (ssize_t) len) {
					s[sz] = '\0';
					mvaddnstr(ln, cl + cells, s, sz);
					memcpy(buf + i, s, sz);
					i += sz;
					buf[i] = '\0';
					cells += wcwidth(wc);
				} else
					beep();
			}
#else
			if (i + 1 < (ssize_t) len && isprint(c)) {
				mvaddch(ln, cl + cells, c);
				buf[i++] = c;
				buf[i] = '\0';
				cells++;
			}
#endif
			else
				beep();
		}
		refresh();
	}

	rc = i;		/* success */
done:
	move(ln, 0);
	clrtoeol();
	curs_set(0);
	refresh();

	return rc;
}

/* @res is default value as well as result in bytes */
static int ui_get_size(struct cfdisk *cf, const char *prompt, uintmax_t *res,
		       uintmax_t low, uintmax_t up)
{
	char buf[128];
	uintmax_t user = 0;
	ssize_t rc;
	char *dflt = size_to_human_string(0, *res);

	DBG(FRONTEND, ul_debug("ui: get_size (default=%ju)", *res));

	ui_clean_info();

	do {
		int pwr = 0, insec = 0;

		snprintf(buf, sizeof(buf), "%s", dflt);
		rc = ui_get_string(cf, prompt,
				_("May be followed by {M,B,G,T}iB "
				  "(the \"iB\" is optional) or S for sectors."),
				buf, sizeof(buf));
		if (rc == 0) {
			ui_warnx(_("Please, specify size."));
			continue;			/* nothing specified */
		} else if (rc == -CFDISK_ERR_ESC)
			break;				/* cancel dialog */

		if (strcmp(buf, dflt) == 0)
			user = *res, rc = 0;		/* no change, use default */
		else {
			size_t len = strlen(buf);
			if (buf[len - 1] == 'S' || buf[len - 1] == 's') {
				insec = 1;
				buf[len - 1] = '\0';
			}
			rc = parse_size(buf, &user, &pwr);	/* parse */
		}

		if (rc == 0) {
			DBG(FRONTEND, ul_debug("ui: get_size user=%ju, power=%d, sectors=%s",
						user, pwr, insec ? "yes" : "no"));
			if (insec)
				user *= cf->cxt->sector_size;
			if (user < low) {
				ui_warnx(_("Minimal size is %ju"), low);
				rc = -ERANGE;
			}
			if (user > up && pwr && user < up + (1ULL << pwr * 10))
				/* ignore when the user specified size overflow
				 * with in range specified by suffix (e.g. MiB) */
				user = up;

			if (user > up) {
				ui_warnx(_("Maximal size is %ju bytes."), up);
				rc = -ERANGE;
			}
		} else
			ui_warnx(_("Failed to parse size."));
	} while (rc != 0);

	if (rc == 0)
		*res = user;
	free(dflt);

	DBG(FRONTEND, ul_debug("ui: get_size (result=%ju, rc=%zd)", *res, rc));
	return rc;
}

static struct fdisk_parttype *ui_get_parttype(struct cfdisk *cf,
					struct fdisk_parttype *cur)
{
	struct cfdisk_menuitem *d, *cm;
	size_t i = 0, nitems, idx = 0;
	struct fdisk_parttype *t = NULL;
	int has_typestr = 0;

	DBG(FRONTEND, ul_debug("ui: asking for parttype."));

	/* create cfdisk menu according to label types, note that the
	 * last cm[] item has to be empty -- so nitems + 1 */
	nitems = cf->cxt->label->nparttypes;
	if (!nitems)
		return NULL;
	cm = xcalloc(nitems + 1, sizeof(struct cfdisk_menuitem));
	if (!cm)
		return NULL;

	has_typestr = cf->cxt->label->parttypes[0].typestr &&
		      *cf->cxt->label->parttypes[0].typestr;

	for (i = 0; i < nitems; i++) {
		struct fdisk_parttype *x = &cf->cxt->label->parttypes[i];
		char *name;

		if (!x || !x->name)
			continue;
		cm[i].userdata = x;
		if (!has_typestr)
			xasprintf(&name, "%2x %s", x->type, _(x->name));
		else {
			name = (char *) _(x->name);
			cm[i].desc = x->typestr;
		}
		cm[i].name = name;
		if (x == cur)
			idx = i;
	}

	/* make the new menu active */
	menu_push(cf, cm);
	cf->menu->vertical = 1;
	cf->menu->idx = idx;
	menu_set_title(cf->menu, _("Select partition type"));
	ui_draw_menu(cf);
	refresh();

	do {
		int key = getch();

		if (ui_resize)
			ui_menu_resize(cf);
		if (ui_menu_move(cf, key) == 0)
			continue;

		switch (key) {
		case KEY_ENTER:
		case '\n':
		case '\r':
			d = menu_get_menuitem(cf, cf->menu->idx);
			if (d)
				t = (struct fdisk_parttype *) d->userdata;
			goto done;
		case KEY_ESC:
		case 'q':
		case 'Q':
			goto done;
		}
	} while (1);

done:
	menu_pop(cf);
	if (!has_typestr) {
		for (i = 0; i < nitems; i++)
			free((char *) cm[i].name);
	}
	free(cm);
	DBG(FRONTEND, ul_debug("ui: get parrtype done [type=%s] ", t ? t->name : NULL));
	return t;
}

/* prints menu with libfdisk labels and waits for users response */
static int ui_create_label(struct cfdisk *cf)
{
	struct cfdisk_menuitem *d, *cm;
	int rc = 1;
	size_t i = 0, nitems;
	struct fdisk_label *lb = NULL;

	assert(cf);

	DBG(FRONTEND, ul_debug("ui: asking for new disklabe."));

	/* create cfdisk menu according to libfdisk labels, note that the
	 * last cm[] item has to be empty -- so nitems + 1 */
	nitems = fdisk_context_get_nlabels(cf->cxt);
	cm = xcalloc(nitems + 1, sizeof(struct cfdisk_menuitem));

	while (fdisk_context_next_label(cf->cxt, &lb) == 0) {
		if (fdisk_label_is_disabled(lb) || strcmp(lb->name, "bsd") == 0)
			continue;
		cm[i++].name = lb->name;
	}

	erase();
	ui_center(ui_lines - 4,
		_("Device does not contain a recognized partition table."));
	ui_center(ui_lines - 3,
		_("Please, select a type to create a new disk label."));

	/* make the new menu active */
	menu_push(cf, cm);
	cf->menu->vertical = 1;
	menu_set_title(cf->menu, _("Select label type"));
	ui_draw_menu(cf);
	refresh();

	do {
		int key = getch();

		if (ui_resize)
			ui_menu_resize(cf);
		if (ui_menu_move(cf, key) == 0)
			continue;
		switch (key) {
		case KEY_ENTER:
		case '\n':
		case '\r':
			d = menu_get_menuitem(cf, cf->menu->idx);
			if (d)
				rc = fdisk_create_disklabel(cf->cxt, d->name);
			goto done;
		case KEY_ESC:
		case 'q':
		case 'Q':
			goto done;
		}
	} while (1);

done:
	menu_pop(cf);
	free(cm);
	DBG(FRONTEND, ul_debug("ui: create label done [rc=%d] ", rc));
	return rc;
}

static int ui_help(void)
{
	size_t i;
	static const char *help[] = {
		N_("Help Screen for cfdisk"),
		"",
		N_("This is cfdisk, a curses based disk partitioning program, which"),
		N_("allows you to create, delete and modify partitions on your hard"),
		N_("disk drive."),
		"",
		N_("Copyright (C) 2014 Karel Zak <kzak@redhat.com> "),
		N_("Based on the original cfdisk from Kevin E. Martin & aeb."),
		"",
		N_("Command      Meaning"),
		N_("-------      -------"),
		N_("  b          Toggle bootable flag of the current partition"),
		N_("  d          Delete the current partition"),
		N_("  h          Print this screen"),
		N_("  n          Create new partition from free space"),
		N_("  q          Quit program without writing partition table"),
		N_("  t          Change the partition type"),
		N_("  s          Fix partitions order (only when in disarray)"),
		N_("  W          Write partition table to disk (must enter upper case W)"),
		N_("             Since this might destroy data on the disk, you must"),
		N_("             either confirm or deny the write by entering `yes' or"),
		N_("             `no'"),
		N_("Up Arrow     Move cursor to the previous partition"),
		N_("Down Arrow   Move cursor to the next partition"),
		N_("Left Arrow   Move cursor to the previous menu item"),
		N_("Right Arrow  Move cursor to the next menu item"),

		"",
		N_("Note: All of the commands can be entered with either upper or lower"),
		N_("case letters (except for Writes)."),
		"",
		N_("Use lsblk(8) or partx(8) to see more details about the device.")
	};

	erase();
	for (i = 0; i < ARRAY_SIZE(help); i++)
		mvaddstr(i, 1, _(help[i]));

	ui_info(_("Press a key to continue."));

	getch();
	return 0;
}

/* TODO: use @sz, now 128bytes */
static int main_menu_ignore_keys(struct cfdisk *cf, char *ignore,
		size_t sz __attribute__((__unused__)))
{
	struct fdisk_partition *pa = get_current_partition(cf);
	size_t i = 0;

	if (!pa)
		return 0;
	if (fdisk_partition_is_freespace(pa)) {
		ignore[i++] = 'd';	/* delete */
		ignore[i++] = 't';	/* set type */
		ignore[i++] = 'b';      /* set bootable */
	} else {
		ignore[i++] = 'n';
		if (!fdisk_is_disklabel(cf->cxt, DOS) &&
		    !fdisk_is_disklabel(cf->cxt, SGI))
			ignore[i++] = 'b';
	}

	if (!cf->wrong_order)
		ignore[i++] = 's';
	if (fdisk_context_is_readonly(cf->cxt))
		ignore[i++] = 'W';
	return i;
}


/* returns: error: < 0, success: 0, quit: 1 */
static int main_menu_action(struct cfdisk *cf, int key)
{
	size_t n;
	int ref = 0, rc, org_order = cf->wrong_order;
	const char *info = NULL, *warn = NULL;
	struct fdisk_partition *pa;

	assert(cf);
	assert(cf->cxt);
	assert(cf->menu);

	if (key == 0) {
		struct cfdisk_menuitem *d = menu_get_menuitem(cf, cf->menu->idx);
		if (!d)
			return 0;
		key = d->key;

	} else if (key != 'w' && key != 'W')
		key = tolower(key);	/* case insensitive except 'W'rite */

	DBG(FRONTEND, ul_debug("ui: main menu action: key=%c", key));

	if (cf->menu->ignore && strchr(cf->menu->ignore, key)) {
		DBG(FRONTEND, ul_debug("  ignore '%c'", key));
		return 0;
	}

	pa = get_current_partition(cf);
	n = fdisk_partition_get_partno(pa);

	DBG(FRONTEND, ul_debug("menu action on %p", pa));
	ui_clean_hint();
	ui_clean_info();

	switch (key) {
	case 'b': /* Bootable flag */
	{
		int fl = fdisk_is_disklabel(cf->cxt, DOS) ? DOS_FLAG_ACTIVE :
			 fdisk_is_disklabel(cf->cxt, SGI) ? SGI_FLAG_BOOT : 0;

		if (fl && fdisk_partition_toggle_flag(cf->cxt, n, fl))
			warn = _("Could not toggle the flag.");
		else if (fl)
			ref = 1;
		break;
	}
#ifdef KEY_DC
	case KEY_DC:
#endif
	case 'd': /* Delete */
		if (fdisk_delete_partition(cf->cxt, n) != 0)
			warn = _("Could not delete partition %zu.");
		else
			info = _("Partition %zu has been deleted.");
		ref = 1;
		break;
	case 'h': /* help */
		ui_help();
		ref = 1;
		break;
	case 'n': /* New */
	{
		uint64_t start, size, dflt_size;
		struct fdisk_partition *npa;	/* the new partition */

		if (!pa || !fdisk_partition_is_freespace(pa))
			return -EINVAL;
		npa = fdisk_new_partition();
		if (!npa)
			return -ENOMEM;
		/* free space range */
		start = fdisk_partition_get_start(pa);
		size = dflt_size = fdisk_partition_get_size(pa) * cf->cxt->sector_size;

		if (ui_get_size(cf, _("Partition size: "), &size, 1, size)
				== -CFDISK_ERR_ESC)
			break;

		if (dflt_size == size)	/* default is to fillin all free space */
			fdisk_partition_end_follow_default(npa, 1);
		else /* set relative size of the partition */
			fdisk_partition_set_size(npa, size / cf->cxt->sector_size);

		fdisk_partition_set_start(npa, start);
				fdisk_partition_partno_follow_default(npa, 1);
		/* add to disk label -- libfdisk will ask for missing details */
		rc = fdisk_add_partition(cf->cxt, npa);
		fdisk_unref_partition(npa);
		if (rc == 0)
			ref = 1;
		break;
	}
	case 'q': /* Quit */
		return 1;
	case 't': /* Type */
	{
		struct fdisk_parttype *t;

		if (!pa || fdisk_partition_is_freespace(pa))
			return -EINVAL;
		t = (struct fdisk_parttype *) fdisk_partition_get_type(pa);
		t = ui_get_parttype(cf, t);
		ref = 1;

		if (t && fdisk_set_partition_type(cf->cxt, n, t) == 0)
			info = _("Changed type of partition %zu.");
		else
			info = _("The type of partition %zu is unchanged.");
		break;
	}
	case 's': /* fix order */
		if (cf->wrong_order) {
			fdisk_reorder_partitions(cf->cxt);
			ref = 1;
		}
		break;
	case 'W': /* Write */
	{
		char buf[64] = { 0 };
		int rc;

		if (fdisk_context_is_readonly(cf->cxt)) {
			warn = _("Device open in read-only mode");
			break;
		}

		rc = ui_get_string(cf,
			  _("Are you sure you want to write the partition "
			    "table to disk? "),
			  _("Type \"yes\" or \"no\", or press ESC to leave this dialog."),
			  buf, sizeof(buf));

		ref = 1;
		if (rc <= 0 || (strcasecmp(buf, "yes") != 0 &&
				strcasecmp(buf, _("yes")) != 0)) {
			info = _("Did not write partition table to disk");
			break;
		}
		rc = fdisk_write_disklabel(cf->cxt);
		if (rc)
			warn = _("Failed to write disklabel");
		else {
			fdisk_reread_partition_table(cf->cxt);
			info = _("The partition table has been altered.");
		}
		cf->nwrites++;
		break;
	}
	default:
		break;
	}

	if (ref) {
		lines_refresh(cf);
		ui_refresh(cf);
	}

	ui_clean_hint();
	if (warn)
		ui_warnx(warn, n + 1);
	else if (info)
		ui_info(info, n + 1);
	else if (key == 'n' && cf->wrong_order && org_order == 0)
		 ui_info(_("Note that partition table entries are not in disk order now."));

	return 0;
}

static void ui_resize_refresh(struct cfdisk *cf)
{
	resize();
	menu_refresh_size(cf);
	lines_refresh(cf);
	ui_refresh(cf);
}

static int ui_run(struct cfdisk *cf)
{
	int rc = 0;

	ui_lines = LINES;
	ui_cols = COLS;
	DBG(FRONTEND, ul_debug("start cols=%zu, lines=%zu", ui_cols, ui_lines));

	if (!fdisk_dev_has_disklabel(cf->cxt) || cf->zero_start) {
		rc = ui_create_label(cf);
		if (rc < 0)
			ui_errx(EXIT_FAILURE,
					_("failed to create a new disklabel"));
		if (rc)
			return rc;
	}

	cols_init(cf);
	rc = lines_refresh(cf);
	if (rc)
		ui_errx(EXIT_FAILURE, _("failed to read partitions"));

	menu_push(cf, main_menuitems);
	cf->menu->ignore_cb = main_menu_ignore_keys;

	rc = ui_refresh(cf);
	if (rc)
		return rc;

	if (fdisk_context_is_readonly(cf->cxt))
		ui_warnx(_("Device open in read-only mode."));

	do {
		int rc = 0, key = getch();

		if (ui_resize)
			/* Note that ncurses getch() returns ERR when interrupted
			 * by signal, but SLang does not interrupt at all. */
			ui_resize_refresh(cf);
		if (key == ERR)
			continue;
		if (ui_menu_move(cf, key) == 0)
			continue;

		DBG(FRONTEND, ul_debug("ui: main action key >%c<.", key));

		switch (key) {
		case KEY_DOWN:
		case '\016':	/* ^N */
		case 'j':	/* Vi-like alternative */
			ui_table_goto(cf, cf->lines_idx + 1);
			break;
		case KEY_UP:
		case '\020':	/* ^P */
		case 'k':	/* Vi-like alternative */
			ui_table_goto(cf, (int) cf->lines_idx - 1);
			break;
		case KEY_PPAGE:
			if (cf->page_sz) {
				ui_table_goto(cf, (int) cf->lines_idx - cf->page_sz);
				break;
			}
		case KEY_HOME:
			ui_table_goto(cf, 0);
			break;
		case KEY_NPAGE:
			if (cf->page_sz) {
				ui_table_goto(cf, cf->lines_idx + cf->page_sz);
				break;
			}
		case KEY_END:
			ui_table_goto(cf, (int) cf->nlines - 1);
			break;
		case KEY_ENTER:
		case '\n':
		case '\r':
			rc = main_menu_action(cf, 0);
			break;
		default:
			rc = main_menu_action(cf, key);
			if (rc < 0)
				beep();
			break;
		}

		if (rc == 1)
			break; /* quit */
	} while (1);

	menu_pop(cf);

	DBG(FRONTEND, ul_debug("ui: end"));

	return 0;
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] <disk>\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -L --color[=<when>]     colorize output (auto, always or never)\n"), out);
	fputs(_(" -z --zero               start with zeroed partition table\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("cfdisk(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	const char *diskpath;
	int rc, c, colormode = UL_COLORMODE_UNDEF;
	struct cfdisk _cf = { .lines_idx = 0 },
		      *cf = &_cf;

	static const struct option longopts[] = {
		{ "color",   optional_argument, NULL, 'L' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'V' },
		{ "zero",    no_argument,	NULL, 'z' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "L::hVz", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'L':
			colormode = UL_COLORMODE_AUTO;
			if (optarg)
				colormode = colormode_or_err(optarg,
						_("unsupported color mode"));
			break;
		case 'V':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		case 'z':
			cf->zero_start = 1;
			break;
		}
	}

	colors_init(colormode, "cfdisk");

	fdisk_init_debug(0);
	scols_init_debug(0);

	cf->cxt = fdisk_new_context();
	if (!cf->cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));

	fdisk_context_set_ask(cf->cxt, ask_callback, (void *) cf);

	if (optind == argc)
		diskpath = access(DEFAULT_DEVICE, F_OK) == 0 ?
					DEFAULT_DEVICE : ALTERNATE_DEVICE;
	else
		diskpath = argv[optind];

	rc = fdisk_context_assign_device(cf->cxt, diskpath, 0);
	if (rc == -EACCES)
		rc = fdisk_context_assign_device(cf->cxt, diskpath, 1);
	if (rc != 0)
		err(EXIT_FAILURE, _("cannot open %s"),
				optind == argc ? DEFAULT_DEVICE : diskpath);

	/* Don't use err(), warn() from this point */
	ui_init(cf);
	ui_run(cf);
	ui_end();

	free(cf->lines);
	free(cf->linesbuf);
	fdisk_unref_table(cf->table);

	rc = fdisk_context_deassign_device(cf->cxt, cf->nwrites == 0);
	fdisk_free_context(cf->cxt);
	DBG(FRONTEND, ul_debug("bye! [rc=%d]", rc));
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
