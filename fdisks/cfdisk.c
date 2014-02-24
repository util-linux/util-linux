#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#ifdef HAVE_SLANG_H
#include <slang.h>
#elif defined(HAVE_SLANG_SLANG_H)
#include <slang/slang.h>
#endif

#ifdef HAVE_SLCURSES_H
#include <slcurses.h>
#elif defined(HAVE_SLANG_SLCURSES_H)
#include <slang/slcurses.h>
#elif defined(HAVE_NCURSESW_NCURSES_H) && defined(HAVE_WIDECHAR)
#include <ncursesw/ncurses.h>
#elif defined(HAVE_NCURSES_H)
#include <ncurses.h>
#elif defined(HAVE_NCURSES_NCURSES_H)
#include <ncurses/ncurses.h>
#endif

#ifdef HAVE_WIDECHAR
#include <wctype.h>
#endif

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "mbsalign.h"

#include "fdiskP.h"

#define ARROW_CURSOR_STRING	">>> "
#define ARROW_CURSOR_DUMMY	"    "
#define ARROW_CURSOR_WIDTH	(sizeof(ARROW_CURSOR_STRING) - 1)

#define MENU_PADDING		2
#define TABLE_START_LINE	4
#define MENU_START_LINE		(LINES - 5)
#define INFO_LINE		(LINES - 2)
#define HINT_LINE		(LINES - 1)

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
typedef int (menu_callback_t)(struct cfdisk *, int);

static int menu_cb_main(struct cfdisk *cf, int key);
static struct cfdisk_menudesc *menu_get_menuitem(struct cfdisk *cf, size_t idx);
static struct cfdisk_menudesc *menu_get_menuitem_by_key(struct cfdisk *cf, int key, size_t *idx);
static struct cfdisk_menu *menu_push(struct cfdisk *cf, size_t id, struct cfdisk_menudesc *desc);
static struct cfdisk_menu *menu_pop(struct cfdisk *cf);

static int ui_refresh(struct cfdisk *cf);
static void ui_warnx(const char *fmt, ...);
static void ui_warn(const char *fmt, ...);
static void ui_info(const char *fmt, ...);
static void ui_draw_menu(struct cfdisk *cf);
static int ui_menu_move(struct cfdisk *cf, int key);

static int ui_get_size(struct cfdisk *cf, const char *prompt, uintmax_t *res,
		       uintmax_t low, uintmax_t up);

static int ui_enabled;

struct cfdisk_menudesc {
	int		key;		/* keyboard shortcut */
	const char	*name;		/* item name */
	const char	*desc;		/* item description */
};

struct cfdisk_menu {
	char			*title;
	struct cfdisk_menudesc	*desc;
	char			*ignore;
	size_t			id;
	size_t			width;
	size_t			nitems;

	menu_callback_t		*callback;

	struct cfdisk_menu	*prev;

	unsigned int		vertical : 1;
};

static struct cfdisk_menudesc menu_main[] = {
	{ 'b', N_("Bootable"), N_("Toggle bootable flag of the current partition") },
	{ 'd', N_("Delete"), N_("Delete the current partition") },
//	{ 'g', N_("Geometry"), N_("Change disk geometry (experts only)") },
//	{ 'h', N_("Help"), N_("Print help screen") },
//	{ 'm', N_("Maximize"), N_("Maximize disk usage of the current partition (experts only)") },
	{ 'n', N_("New"), N_("Create new partition from free space") },
//	{ 'p', N_("Print"), N_("Print partition table to the screen or to a file") },
	{ 'q', N_("Quit"), N_("Quit program without writing partition table") },
	{ 't', N_("Type"), N_("Change the partition type") },
//	{ 'u', N_("Units"), N_("Change units of the partition size display (MB, sect, cyl)") },
	{ 'W', N_("Write"), N_("Write partition table to disk (this might destroy data)") },
	{ 0, NULL, NULL }
};

enum {
	CFDISK_MENU_GENERATED	= -1,	/* used in libfdisk callback */

	/* built-in menus */
	CFDISK_MENU_MAIN	= 0,

};

static struct cfdisk_menudesc *menus[] = {
	[CFDISK_MENU_MAIN] = menu_main
};

static menu_callback_t *menu_callbacks[] = {
	[CFDISK_MENU_MAIN] = menu_cb_main
};


struct cfdisk {
	struct fdisk_context	*cxt;	/* libfdisk context */
	struct fdisk_table	*table;	/* partition table */

	struct cfdisk_menu	*menu;	/* the current menu */ 
	size_t			menu_idx;

	int	*cols;		/* output columns */
	size_t	ncols;		/* number of columns */

	char	*linesbuf;	/* table as string */
	size_t	linesbufsz;	/* size of the tb_buf */

	char	**lines;	/* array with lines */
	size_t	nlines;		/* number of lines */
	size_t	lines_idx;		/* current line <0..N>, exclude header */
};

static int cols_init(struct cfdisk *cf)
{
	assert(cf);

	free(cf->cols);
	cf->cols = NULL;
	cf->ncols = 0;

	return fdisk_get_columns(cf->cxt, 0, &cf->cols, &cf->ncols);
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
	struct tt *tt = NULL;
	char *res = NULL;
	size_t i;
	int tree = 0;
	struct tt_line *ln, *ln_cont = NULL;

	DBG(FRONTEND, dbgprint("table: convert to string"));

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
			DBG(FRONTEND, dbgprint("table: nested detected, using tree"));
			tree = TT_FL_TREE;
			break;
		}
	}

	tt = tt_new_table(TT_FL_FREEDATA | TT_FL_MAX | tree);
	if (!tt)
		goto done;

	/* headers */
	for (i = 0; i < cf->ncols; i++) {
		col = fdisk_label_get_column(lb, cf->cols[i]);
		if (col) {
			int fl = col->tt_flags;
			if (tree && col->id == FDISK_COL_DEVICE)
				fl |= TT_FL_TREE;
			tt_define_column(tt, col->name, col->width, fl);
		}
	}

	/* data */
	fdisk_reset_iter(itr, FDISK_ITER_FORWARD);

	while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
		struct tt_line *parent = fdisk_partition_is_nested(pa) ? ln_cont : NULL;

		ln = tt_add_line(tt, parent);
		if (!ln)
			goto done;
		for (i = 0; i < cf->ncols; i++) {
			char *cdata = NULL;
			col = fdisk_label_get_column(lb, cf->cols[i]);
			if (!col)
				continue;
			if (fdisk_partition_to_string(pa, cf->cxt, col->id, &cdata))
				continue;
			tt_line_set_data(ln, i, cdata);
		}
		if (tree && fdisk_partition_is_container(pa))
			ln_cont = ln;

		tt_line_set_userdata(ln, (void *) pa);
		fdisk_ref_partition(pa);
	}

	if (tt_is_empty(tt))
		goto done;

	tt_set_termreduce(tt, ARROW_CURSOR_WIDTH);
	tt_print_table_to_string(tt, &res);

	/* tt_* code might to reorder lines, let's reorder @tb according to the
	 * final output (it's no problem because partitions are addressed by
	 * parno stored within struct fdisk_partition)  */

	/* remove all */
	fdisk_reset_iter(itr, FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(tb, itr, &pa) == 0)
		fdisk_table_remove_partition(tb, pa);

	/* add all in the right order */
	i = 0;
	while (tt_get_output_line(tt, i++, &ln) == 0) {
		struct fdisk_partition *pa = tt_line_get_userdata(ln);

		fdisk_table_add_partition(tb, pa);
		fdisk_unref_partition(pa);
	}
done:
	tt_free_table(tt);
	fdisk_free_iter(itr);

	return res;
}

static int lines_refresh(struct cfdisk *cf)
{
	int rc;
	char *p;
	size_t i;

	assert(cf);

	DBG(FRONTEND, dbgprint("refreshing buffer"));

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

	cf->lines = calloc(cf->nlines, sizeof(char *));
	if (!cf->lines)
		return -ENOMEM;

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
	struct cfdisk_menudesc *d, *cm;
	int key;
	size_t i = 0, nitems;
	const char *name, *desc;

	assert(ask);
	assert(cf);

	/* create cfdisk menu according to libfdisk ask-menu, note that the
	 * last cm[] item has to be empty -- so nitems + 1 */
	nitems = fdisk_ask_menu_get_nitems(ask);
	cm = calloc(nitems + 1, sizeof(struct cfdisk_menudesc));
	if (!cm)
		return -ENOMEM;

	for (i = 0; i < nitems; i++) {
		if (fdisk_ask_menu_get_item(ask, i, &key, &name, &desc))
			break;
		cm[i].key = key;
		cm[i].desc = desc;
		cm[i].name = name;
	}

	/* make the new menu active */
	menu_push(cf, CFDISK_MENU_GENERATED, cm);
	ui_draw_menu(cf);
	refresh();

	/* wait for keys */
	do {
		int key = getch();

		if (ui_menu_move(cf, key) == 0)
			continue;

		switch (key) {
		case KEY_ENTER:
		case '\n':
		case '\r':
			d = menu_get_menuitem(cf, cf->menu_idx);
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

static int ui_end(struct cfdisk *cf)
{
	if (cf && !ui_enabled)
		return -EINVAL;

#if defined(HAVE_SLCURSES_H) || defined(HAVE_SLANG_SLCURSES_H)
	SLsmg_gotorc(LINES - 1, 0);
	SLsmg_refresh();
#else
	mvcur(0, COLS - 1, LINES-1, 0);
#endif
	nl();
	endwin();
	printf("\n");
	return 0;
}

static void ui_vprint_center(int line, int attrs, const char *fmt, va_list ap)
{
	size_t width;
	char *buf = NULL;

	move(line, 0);
	clrtoeol();

	xvasprintf(&buf, fmt, ap);

	width = mbs_safe_width(buf);
	if (width > (size_t) COLS) {
		char *p = strrchr(buf + COLS, ' ');
		if (!p)
			p = buf + COLS;
		*p = '\0';
		if (line + 1 >= LINES)
			line--;
		attron(attrs);
		mvaddstr(line, 0, buf);
		mvaddstr(line + 1, 0, p+1);
		attroff(attrs);
	} else {
		attron(attrs);
		mvaddstr(line, (COLS - width) / 2, buf);
		attroff(attrs);
	}
	free(buf);
}

static void ui_center(int line, const char *fmt, ...)
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
		ui_vprint_center(INFO_LINE, COLOR_PAIR(CFDISK_CL_WARNING), fmt, ap);
	else
		vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static void ui_warn(const char *fmt, ...)
{
	char *fmt_m;
	va_list ap;

	xasprintf(&fmt_m, "%s: %m", fmt);

	va_start(ap, fmt);
	if (ui_enabled)
		ui_vprint_center(INFO_LINE, COLOR_PAIR(CFDISK_CL_WARNING), fmt_m, ap);
	else
		vfprintf(stderr, fmt_m, ap);
	va_end(ap);
	free(fmt_m);
}

static void ui_info(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (ui_enabled)
		ui_vprint_center(INFO_LINE, A_BOLD, fmt, ap);
	else
		vfprintf(stdout, fmt, ap);
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
	else
		vfprintf(stdout, fmt, ap);
	va_end(ap);
}

static void ui_clean_hint(void)
{
	move(HINT_LINE, 0);
	clrtoeol();
}

static void die_on_signal(int dummy __attribute__((__unused__)))
{
	ui_end(NULL);
	exit(EXIT_FAILURE);
}

static void menu_update_ignore(struct cfdisk *cf)
{
	char ignore[128] = { 0 };
	int i = 0;
	struct fdisk_partition *pa;
	struct cfdisk_menu *m;
	struct cfdisk_menudesc *d, *org;
	size_t idx;

	assert(cf);

	m = cf->menu;
	org = menu_get_menuitem(cf, cf->menu_idx);

	DBG(FRONTEND, dbgprint("menu: update menu ignored keys"));

	switch (m->id) {
	case CFDISK_MENU_MAIN:
		pa = get_current_partition(cf);
		if (!pa)
			break;
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

		break;
	}

	ignore[i] = '\0';

	/* return if no change */
	if (   (!m->ignore && !*ignore)
	    || (m->ignore && *ignore && strcmp(m->ignore, ignore) == 0)) {
		    return;
	}

	free(m->ignore);
	m->ignore = xstrdup(ignore);
	m->nitems = 0;

	for (d = m->desc; d->name; d++) {
		if (m->ignore && strchr(m->ignore, d->key))
			continue;
		m->nitems++;
	}

	/* refresh menu index to be at the same menuitem or go to the first */
	if (org && menu_get_menuitem_by_key(cf, org->key, &idx))
		cf->menu_idx = idx;
	else
		cf->menu_idx = 0;
}

static struct cfdisk_menu *menu_push(
			struct cfdisk *cf,
			size_t id,
			struct cfdisk_menudesc *desc)
{
	struct cfdisk_menu *m = xcalloc(1, sizeof(*m));
	struct cfdisk_menudesc *d;

	assert(cf);

	DBG(FRONTEND, dbgprint("menu: new menu"));

	m->prev = cf->menu;
	m->id = id;
	m->desc = desc ? desc : menus[id];
	m->callback = menu_callbacks[id];

	for (d = m->desc; d->name; d++) {
		const char *name = _(d->name);
		size_t len = mbs_safe_width(name);
		if (len > m->width)
			m->width = len;
		m->nitems++;
	}

	cf->menu = m;
	return m;
}

static struct cfdisk_menu *menu_pop(struct cfdisk *cf)
{
	struct cfdisk_menu *m = NULL;

	assert(cf);

	DBG(FRONTEND, dbgprint("menu: rem menu"));

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


/* returns: error: < 0, success: 0, quit: 1 */
static int menu_cb_main(struct cfdisk *cf, int key)
{
	size_t n;
	int ref = 0, rc;
	const char *info = NULL, *warn = NULL;
	struct fdisk_partition *pa;

	assert(cf);
	assert(cf->cxt);
	assert(key);

	pa = get_current_partition(cf);
	n = fdisk_partition_get_partno(pa);

	DBG(FRONTEND, dbgprint("menu action on %p", pa));

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
	case 'd': /* Delete */
		if (fdisk_delete_partition(cf->cxt, n) != 0)
			warn = _("Could not delete partition %zu.");
		else
			info = _("Partition %zu has been deleted.");
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
		break;
	case 'W': /* Write */
		break;
	}

	if (ref) {
		lines_refresh(cf);
		ui_refresh(cf);
	}
	if (warn)
		ui_warnx(warn, n);
	else if (info)
		ui_info(info, n);

	return 0;
}

static int ui_init(struct cfdisk *cf __attribute__((__unused__)))
{
	struct sigaction sa;

	DBG(FRONTEND, dbgprint("ui: init"));

	/* setup SIGCHLD handler */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = die_on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	ui_enabled = 1;
	initscr();

	if (has_colors()) {
		size_t i;

		start_color();
		use_default_colors();

		for (i = 1; i < ARRAY_SIZE(color_pairs); i++)		/* yeah, start from 1! */
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}

	cbreak();
	noecho();
	nonl();
	curs_set(0);
	keypad(stdscr, TRUE);

	return 0;
}

static size_t menuitem_get_line(struct cfdisk *cf, size_t idx)
{
	if (cf->menu->vertical) {
		size_t ni = (cf->menu->nitems + 1) / 2;
		size_t l = LINES / 2;

		ni = ni > l ? l - 1 : ni;
		return l - ni + idx;
	} else {
		size_t len = cf->menu->width + 4 + MENU_PADDING;	/* item width */
		size_t items = COLS / len;				/* items per line */

		return MENU_START_LINE + ((idx / items));
	}
}

static int menuitem_get_column(struct cfdisk *cf, size_t idx)
{
	if (cf->menu->vertical) {
		size_t nc = cf->menu->width + MENU_PADDING;
		size_t c = COLS / 2;

		nc = nc > c ? c - 1 : nc;
		return c - nc;
	} else {
		size_t len = cf->menu->width + 4 + MENU_PADDING;	/* item width */
		size_t items = COLS / len;				/* items per line */
		size_t extra = items < cf->menu->nitems ?		/* extra space on line */
				COLS % len :				/* - multi-line menu */
				COLS - (cf->menu->nitems * len);	/* - one line menu */

		extra += MENU_PADDING;		/* add padding after last item to extra */

		if (idx < items)
			return (idx * len) + (extra / 2);
		return ((idx % items) * len) + (extra / 2);
	}
}

static struct cfdisk_menudesc *menu_get_menuitem(struct cfdisk *cf, size_t idx)
{
	struct cfdisk_menudesc *d;
	size_t i;

	for (i = 0, d = cf->menu->desc; d->name; d++) {
		if (cf->menu->ignore && strchr(cf->menu->ignore, d->key))
			continue;
		if (i++ == idx)
			return d;
	}

	return NULL;
}

static struct cfdisk_menudesc *menu_get_menuitem_by_key(struct cfdisk *cf,
							int key, size_t *idx)
{
	struct cfdisk_menudesc *d;

	for (*idx = 0, d = cf->menu->desc; d->name; d++) {
		if (cf->menu->ignore && strchr(cf->menu->ignore, d->key))
			continue;
		if (key == d->key)
			return d;
		(*idx)++;
	}

	return NULL;
}

static void ui_draw_menuitem(struct cfdisk *cf,
			     struct cfdisk_menudesc *d,
			     size_t idx)
{
	char buf[80 * MB_CUR_MAX];
	const char *name;
	size_t width = cf->menu->width + 2;	/* 2 = blank around string */
	int ln, cl, vert = cf->menu->vertical;

	name = _(d->name);
	mbsalign(name, buf, sizeof(buf), &width,
			vert ? MBS_ALIGN_LEFT : MBS_ALIGN_CENTER,
			0);

	ln = menuitem_get_line(cf, idx);
	cl = menuitem_get_column(cf, idx);

	DBG(FRONTEND, dbgprint("ui: menuitem: cl=%d, ln=%d, item='%s'",
			cl, ln, buf));

	if (vert) {
		mvaddch(ln, cl - 1, ACS_VLINE);
		mvaddch(ln, cl + cf->menu->width + 4, ACS_VLINE);
	}

	if (cf->menu_idx == idx) {
		standout();
		mvprintw(ln, cl, vert ? " %s " : "[%s]", buf);
		standend();
		if (d->desc)
			ui_hint(d->desc);
	} else
		mvprintw(ln, cl, vert ? " %s " : "[%s]", buf);
}

static void ui_draw_menu(struct cfdisk *cf)
{
	struct cfdisk_menudesc *d;
	struct cfdisk_menu *m;
	size_t i = 0;

	assert(cf);
	assert(cf->menu);

	DBG(FRONTEND, dbgprint("ui: menu: draw start"));

	m = cf->menu;

	for (i = MENU_START_LINE; i < (size_t) LINES - 1; i++) {
		move(i, 0);
		clrtoeol();
	}

	menu_update_ignore(cf);

	i = 0;
	while ((d = menu_get_menuitem(cf, i)))
		ui_draw_menuitem(cf, d, i++);

	if (m->vertical) {
		size_t ln = menuitem_get_line(cf, 0);
		size_t cl = menuitem_get_column(cf, 0);
		size_t nlines = i;

		mvaddch(ln - 1, cl - 1, ACS_ULCORNER);
		mvaddch(ln + nlines, cl - 1, ACS_LLCORNER);

		for (i = 0; i < m->width + 4; i++) {
			mvaddch(ln - 1, cl + i, ACS_HLINE);
			mvaddch(ln + nlines, cl + i, ACS_HLINE);
		}
		mvaddch(ln - 1, cl + i, ACS_URCORNER);
		mvaddch(ln + nlines, cl + i, ACS_LRCORNER);

		if (m->title) {
			attron(A_BOLD);
			mvprintw(ln - 1, cl, " %s ", m->title);
			attroff(A_BOLD);
		}

	}

	DBG(FRONTEND, dbgprint("ui: menu: draw end."));
}

static void ui_menu_goto(struct cfdisk *cf, int where)
{
	struct cfdisk_menudesc *d;
	size_t old;

	if (where < 0)
		where = cf->menu->nitems - 1;
	else if ((size_t) where > cf->menu->nitems - 1)
		where = 0;
	if ((size_t) where == cf->menu_idx)
		return;

	ui_clean_info();

	old = cf->menu_idx;
	cf->menu_idx = where;

	d = menu_get_menuitem(cf, old);
	ui_draw_menuitem(cf, d, old);

	d = menu_get_menuitem(cf, where);
	ui_draw_menuitem(cf, d, where);
}

static int ui_menu_move(struct cfdisk *cf, int key)
{
	assert(cf);
	assert(cf->menu);

	if (cf->menu->vertical)
	{
		switch (key) {
		case KEY_DOWN:
		case '\016':	/* ^N */
		case 'j':	/* Vi-like alternative */
			ui_menu_goto(cf, cf->menu_idx + 1);
			return 0;
		case KEY_UP:
		case '\020':	/* ^P */
		case 'k':	/* Vi-like alternative */
			ui_menu_goto(cf, cf->menu_idx - 1);
			return 0;
		case KEY_HOME:
			ui_menu_goto(cf, 0);
			return 0;
		case KEY_END:
			ui_menu_goto(cf, cf->menu->nitems);
			return 0;
		}
	} else {
		switch (key) {
		case KEY_RIGHT:
		case '\t':
			ui_menu_goto(cf, cf->menu_idx + 1);
			return 0;
		case KEY_LEFT:
#ifdef KEY_BTAB
		case KEY_BTAB:
#endif
			ui_menu_goto(cf, cf->menu_idx - 1);
			return 0;
		}
	}

	return 1;	/* key irrelevant for menu move */
}

/* returns: error: < 0, success: 0, quit: 1 */
static int ui_menu_action(struct cfdisk *cf, int key)
{
	assert(cf);
	assert(cf->menu);
	assert(cf->menu->callback);

	if (key == 0) {
		struct cfdisk_menudesc *d = menu_get_menuitem(cf, cf->menu_idx);
		if (!d)
			return 0;
		key = d->key;

	} else if (key != 'w' && key != 'W')
		key = tolower(key);	/* case insensitive except 'W'rite */

	DBG(FRONTEND, dbgprint("ui: menu action: key=%c", key));

	if (cf->menu->ignore && strchr(cf->menu->ignore, key)) {
		DBG(FRONTEND, dbgprint("  ignore '%c'", key));
		return 0;
	}

	return cf->menu->callback(cf, key);
}

static void ui_draw_partition(struct cfdisk *cf, size_t i)
{
	int ln = TABLE_START_LINE + 1 + i;	/* skip table header */
	int cl = ARROW_CURSOR_WIDTH;		/* we need extra space for cursor */

	DBG(FRONTEND, dbgprint("ui: draw partition %zu", i));

	if (cf->lines_idx == i) {
		standout();
		mvaddstr(ln, 0, ARROW_CURSOR_STRING);
		mvaddstr(ln, cl, cf->lines[i + 1]);
		standend();
	} else {
		int at = 0;

		if (is_freespace(cf, i)) {
			attron(COLOR_PAIR(CFDISK_CL_FREESPACE));
			at = 1;
		}
		mvaddstr(ln, 0, ARROW_CURSOR_DUMMY);
		mvaddstr(ln, cl, cf->lines[i + 1]);
		if (at)
                        attroff(COLOR_PAIR(CFDISK_CL_FREESPACE));
	}

}

static int ui_draw_table(struct cfdisk *cf)
{
	int cl = ARROW_CURSOR_WIDTH;
	size_t i, nparts = fdisk_table_get_nents(cf->table);

	DBG(FRONTEND, dbgprint("ui: draw table"));

	if (cf->nlines - 2 < cf->lines_idx)
		cf->lines_idx = cf->nlines - 2;	/* don't count header */

	/* print header */
	attron(A_BOLD);
	mvaddstr(TABLE_START_LINE, cl, cf->lines[0]);
	attroff(A_BOLD);

	/* print partitions */
	for (i = 0; i < nparts; i++)
		ui_draw_partition(cf, i);

	return 0;
}

static int ui_table_goto(struct cfdisk *cf, int where)
{
	size_t old;
	size_t nparts = fdisk_table_get_nents(cf->table);

	DBG(FRONTEND, dbgprint("ui: goto table %d", where));

	if (where < 0)
		where = 0;
	else if ((size_t) where > nparts - 1)
		where = nparts - 1;

	if ((size_t) where == cf->lines_idx)
		return 0;

	old = cf->lines_idx;
	cf->lines_idx = where;

	ui_draw_partition(cf, old);	/* cleanup old */
	ui_draw_partition(cf, where);	/* draw new */
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
		ui_center(2, _("Label: %s"));
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
	wint_t c;
	int ln = MENU_START_LINE, cl = 1;

	assert(cf);
	assert(buf);
	assert(len);

	move(ln, 0);
	clrtoeol();

	if (prompt) {
		mvaddstr(ln, cl, prompt);
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
		if (get_wch(&c) == ERR) {
#else
		if ((c = getch()) == ERR) {
#endif
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
				str[i++] = c;
				str[i] = '\0';
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

	DBG(FRONTEND, dbgprint("ui: get_size (default=%ju)", *res));

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
			if (buf[len - 1] == 'S') {
				insec = 1;
				buf[len - 1] = '\0';
			}
			rc = parse_size(buf, &user, &pwr);	/* parse */
		}

		if (rc == 0) {
			DBG(FRONTEND, dbgprint("ui: get_size user=%ju, power=%d, sectors=%s",
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

	DBG(FRONTEND, dbgprint("ui: get_size (result=%ju, rc=%zd)", *res, rc));
	return rc;
}

static int ui_run(struct cfdisk *cf)
{
	int rc;

	DBG(FRONTEND, dbgprint("ui: start COLS=%d, LINES=%d", COLS, LINES));

	menu_push(cf, CFDISK_MENU_MAIN, NULL);

	rc = ui_refresh(cf);
	if (rc)
		return rc;

	do {
		int rc = 0, key = getch();

		if (ui_menu_move(cf, key) == 0)
			continue;

		switch (key) {
		case KEY_DOWN:
		case '\016':	/* ^N */
		case 'j':	/* Vi-like alternative */
			ui_table_goto(cf, cf->lines_idx + 1);
			break;
		case KEY_UP:
		case '\020':	/* ^P */
		case 'k':	/* Vi-like alternative */
			ui_table_goto(cf, cf->lines_idx - 1);
			break;
		case KEY_HOME:
			ui_table_goto(cf, 0);
			break;
		case KEY_END:
			ui_table_goto(cf, cf->nlines - 1);
			break;
			ui_menu_action(cf, 0);
			break;
		case KEY_ENTER:
		case '\n':
		case '\r':
			rc = ui_menu_action(cf, 0);
			break;
		default:
			rc = ui_menu_action(cf, key);
			if (rc < 0)
				beep();
			break;
		}

		if (rc == 1)
			break; /* quit */
	} while (1);

	menu_pop(cf);

	DBG(FRONTEND, dbgprint("ui: end"));

	return 0;
}

int main(int argc, char *argv[])
{
	struct cfdisk _cf = { .lines_idx = 0 },
		      *cf = &_cf;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	fdisk_init_debug(0);
	cf->cxt = fdisk_new_context();
	if (!cf->cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));

	fdisk_context_set_ask(cf->cxt, ask_callback, (void *) cf);

	if (argc != 2)
		err(EXIT_FAILURE, "usage: %s <device>", argv[0]);

	if (fdisk_context_assign_device(cf->cxt, argv[optind], 0) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), argv[optind]);

	cols_init(cf);

	if (lines_refresh(cf))
		errx(EXIT_FAILURE, _("failed to read partitions"));

	/* Don't use err(), warn() from this point */
	ui_init(cf);
	ui_run(cf);
	ui_end(cf);

	free(cf->lines);
	free(cf->linesbuf);
	fdisk_unref_table(cf->table);
	fdisk_free_context(cf->cxt);
	return EXIT_SUCCESS;
}
