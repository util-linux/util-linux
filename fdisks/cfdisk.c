#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

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


struct cfdisk_menudesc {
	int		key;		/* keyboard shortcut */
	const char	*name;		/* item name */
	const char	*desc;		/* item description */
};

struct cfdisk_menu {
	struct cfdisk_menudesc	*desc;
	char			*ignore;
	size_t			id;
	size_t			width;
	size_t			nitems;
	struct cfdisk_menu	*prev;
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
	CFDISK_MENU_MAIN	= 0,
};

static struct cfdisk_menudesc *menus[] = {
	[CFDISK_MENU_MAIN] = menu_main
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

	unsigned int	ui_enabled : 1;
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
	struct fdisk_partition *pa;
	const struct fdisk_column *col;
	struct fdisk_label *lb;
	struct fdisk_iter *itr = NULL;
	struct tt *tt = NULL;
	char *res = NULL;
	size_t i;

	DBG(FRONTEND, dbgprint("table: convert to string"));

	assert(cf);
	assert(cf->cxt);
	assert(cf->cols);
	assert(tb);

	lb = fdisk_context_get_label(cf->cxt, NULL);
	assert(lb);

	tt = tt_new_table(TT_FL_FREEDATA | TT_FL_MAX);
	if (!tt)
		goto done;
	itr = fdisk_new_iter(FDISK_ITER_FORWARD);
	if (!itr)
		goto done;

	/* headers */
	for (i = 0; i < cf->ncols; i++) {
		col = fdisk_label_get_column(lb, cf->cols[i]);
		if (col)
			tt_define_column(tt, col->name,
					     col->width,
					     col->tt_flags);
	}

	/* data */
	while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
		struct tt_line *ln = tt_add_line(tt, NULL);
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
	}

	if (!tt_is_empty(tt)) {
		tt_set_termreduce(tt, ARROW_CURSOR_WIDTH);
		tt_print_table_to_string(tt, &res);
	}
done:
	tt_free_table(tt);
	fdisk_free_iter(itr);

	return res;
}


static int lines_refresh_buffer(struct cfdisk *cf)
{
	int rc;
	char *p;
	size_t i;

	assert(cf);

	DBG(FRONTEND, dbgprint("refresing buffer"));

	free(cf->linesbuf);
	free(cf->lines);
	cf->linesbuf = NULL;
	cf->linesbufsz = 0;
	cf->lines = NULL;
	cf->nlines = 0;

	fdisk_unref_table(cf->table);
	fdisk_context_enable_freespace(cf->cxt, 1);

	rc = fdisk_get_table(cf->cxt, &cf->table);
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

static int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)))
{
	int rc = 0;

	assert(cxt);
	assert(ask);

	switch(fdisk_ask_get_type(ask)) {
	case FDISK_ASKTYPE_INFO:
		fputs(fdisk_ask_print_get_mesg(ask), stdout);
		fputc('\n', stdout);
		break;
	case FDISK_ASKTYPE_WARNX:
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		fputc('\n', stderr);
		break;
	case FDISK_ASKTYPE_WARN:
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		errno = fdisk_ask_print_get_errno(ask);
		fprintf(stderr, ": %m\n");
		break;
	default:
		warnx(_("internal error: unsupported dialog type %d"), fdisk_ask_get_type(ask));
		return -EINVAL;
	}
	return rc;
}


static int ui_end(struct cfdisk *cf)
{
	if (cf && !cf->ui_enabled)
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

static void ui_print_center(int line, const char *fmt, ...)
{
	size_t width;
	va_list ap;
	char *buf = NULL;

	move(line, 0);
	clrtoeol();

	va_start(ap, fmt);
	xvasprintf(&buf, fmt, ap);
	va_end(ap);

	width = strlen(buf);			/* TODO: count cells! */
	mvaddstr(line, (COLS - width) / 2, buf);
	free(buf);
}


static void die_on_signal(int dummy __attribute__((__unused__)))
{
	ui_end(NULL);
	exit(EXIT_FAILURE);
}

static void menu_update_ignore(struct cfdisk *cf)
{
	char *ignore = NULL;
	struct cfdisk_menu *m;
	struct cfdisk_menudesc *d;

	assert(cf);

	m = cf->menu;
	DBG(FRONTEND, dbgprint("menu: update menu ignored keys"));

	switch (m->id) {
	case CFDISK_MENU_MAIN:
		break;
	}

	/* return if no change */
	if (   (!m->ignore && (!ignore || !*ignore))
	    || (m->ignore && ignore && strcmp(m->ignore, ignore) == 0)) {
		    free(ignore);
		    return;
	}

	free(m->ignore);
	m->ignore = ignore;
	m->nitems = 0;

	for (d = m->desc; d->name; d++) {
		if (m->ignore && strchr(m->ignore, d->key))
			m->nitems++;
	}
}

static struct cfdisk_menu *menu_push(struct cfdisk *cf, size_t id)
{
	struct cfdisk_menu *m = xcalloc(1, sizeof(*m));
	struct cfdisk_menudesc *d;

	assert(cf);
	assert(id < ARRAY_SIZE(menus));

	DBG(FRONTEND, dbgprint("menu: new menu"));

	m->prev = cf->menu;
	m->id = id;
	m->desc = menus[id];

	for (d = m->desc; d->name; d++) {
		const char *name = _(d->name);
		size_t len = strlen(name);	/* TODO: we care about cells! */
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
		free(cf->menu);
	}
	cf->menu = m;
	return cf->menu;
}


static int ui_init(struct cfdisk *cf)
{
	struct sigaction sa;

	DBG(FRONTEND, dbgprint("ui: init"));

	/* setup SIGCHLD handler */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = die_on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	cf->ui_enabled = 1;
	initscr();

	cbreak();
	noecho();
	nonl();
	curs_set(0);
	keypad(stdscr, TRUE);

	return 0;
}

static size_t menuitem_get_line(struct cfdisk *cf, size_t idx)
{
	size_t len = cf->menu->width + 4 + MENU_PADDING;	/* item width */
	size_t items = COLS / len;				/* items per line */

	return MENU_START_LINE + ((idx / items));
}

static int menuitem_get_column(struct cfdisk *cf, size_t idx)
{
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

static void ui_draw_menuitem(struct cfdisk *cf,
			     struct cfdisk_menudesc *d,
			     size_t idx)
{
	char buf[80 * MB_CUR_MAX];
	const char *name;
	size_t width = cf->menu->width + 2;	/* 2 = blank around string */
	int ln, cl;

	name = _(d->name);
	mbsalign(name, buf, sizeof(buf), &width, MBS_ALIGN_CENTER, 0);

	ln = menuitem_get_line(cf, idx);
	cl = menuitem_get_column(cf, idx);

	DBG(FRONTEND, dbgprint("ui: menuitem: cl=%d, ln=%d, item='%s'",
			cl, ln, buf));

	if (cf->menu_idx == idx) {
		standout();
		mvprintw(ln, cl, "[%s]", buf);
		standend();
		if (d->desc)
			ui_print_center(LINES - 1, d->desc);
	} else
		mvprintw(ln, cl, "[%s]", buf);
}

static void ui_draw_menu(struct cfdisk *cf)
{
	struct cfdisk_menudesc *d;
	size_t i = 0;

	assert(cf);
	assert(cf->menu);

	DBG(FRONTEND, dbgprint("ui: menu: draw start"));

	menu_update_ignore(cf);

	while ((d = menu_get_menuitem(cf, i)))
		ui_draw_menuitem(cf, d, i++);

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

	old = cf->menu_idx;
	cf->menu_idx = where;

	d = menu_get_menuitem(cf, old);
	ui_draw_menuitem(cf, d, old);

	d = menu_get_menuitem(cf, where);
	ui_draw_menuitem(cf, d, where);
}

static int ui_menu_action(struct cfdisk *cf, int key)
{
	return 0;
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
		mvaddstr(ln, 0, ARROW_CURSOR_DUMMY);
		mvaddstr(ln, cl, cf->lines[i + 1]);
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

	if (!cf->ui_enabled)
		return -EINVAL;

	/* header */
	attron(A_BOLD);
	ui_print_center(0, _("Disk: %s"), cf->cxt->dev_path);
	attroff(A_BOLD);
	ui_print_center(1, _("Size: %s, %ju bytes, %ju sectors"),
			strsz, bytes, (uintmax_t) cf->cxt->total_sectors);
	if (fdisk_get_disklabel_id(cf->cxt, &id) == 0 && id)
		ui_print_center(2, _("Label: %s, identifier: %s"),
				cf->cxt->label->name, id);
	else
		ui_print_center(2, _("Label: %s"));
	free(strsz);

	ui_draw_table(cf);
	ui_draw_menu(cf);
	refresh();
	return 0;
}

static int ui_run(struct cfdisk *cf)
{
	int rc;

	DBG(FRONTEND, dbgprint("ui: start COLS=%d, LINES=%d", COLS, LINES));

	menu_push(cf, CFDISK_MENU_MAIN);

	rc = ui_refresh(cf);
	if (rc)
		return rc;

	do {
		int key = getch();

		if (key == 'q')
			break;

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
		case KEY_LEFT:
#ifdef KEY_BTAB
		case KEY_BTAB:
#endif
			ui_menu_goto(cf, cf->menu_idx - 1);
			break;
		case KEY_RIGHT:
		case '\t':
			ui_menu_goto(cf, cf->menu_idx + 1);
			break;
		case KEY_ENTER:
		case '\n':
		case '\r':
			ui_menu_action(cf, 0);
			break;
		default:
			if (ui_menu_action(cf, key) != 0)
				beep();
			break;
		}
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
	fdisk_context_enable_freespace(cf->cxt, 1);

	if (argc != 2)
		err(EXIT_FAILURE, "usage: %s <device>", argv[0]);

	if (fdisk_context_assign_device(cf->cxt, argv[optind], 0) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), argv[optind]);

	cols_init(cf);

	if (lines_refresh_buffer(cf))
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
