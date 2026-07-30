/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_AGETTY_ISSUEFILE_PARSER_H
#define UTIL_LINUX_AGETTY_ISSUEFILE_PARSER_H

#include <stdbool.h>
#include <stdio.h>

#include "list.h"

/* Escape code identifiers — ordered so related codes are adjacent
 * to allow range-based filtering in agetty_ifile_next_item(). */
enum {
	AGETTY_ESC_TEXT = 0,	/* literal text between escapes */

	/* uname */
	AGETTY_ESC_SYSNAME,	/* \s */
	AGETTY_ESC_NODENAME,	/* \n */
	AGETTY_ESC_RELEASE,	/* \r */
	AGETTY_ESC_VERSION,	/* \v */
	AGETTY_ESC_MACHINE,	/* \m */

	/* domain */
	AGETTY_ESC_NIS_DOMAIN,	/* \o */
	AGETTY_ESC_DNS_DOMAIN,	/* \O */

	/* time */
	AGETTY_ESC_DATE,	/* \d */
	AGETTY_ESC_TIME,	/* \t */

	/* terminal */
	AGETTY_ESC_TTYNAME,	/* \l */
	AGETTY_ESC_BAUDRATE,	/* \b */

	/* os-release */
	AGETTY_ESC_OSRELEASE,	/* \S or \S{VAR} */

	/* users */
	AGETTY_ESC_USERS,	/* \u */
	AGETTY_ESC_USERS_TEXT,	/* \U */

	/* escape character */
	AGETTY_ESC_ESCAPE,	/* \e or \e{color} */

	/* network — keep together for range filtering */
	AGETTY_ESC_IPV4,	/* \4 or \4{iface} */
	AGETTY_ESC_IPV6,	/* \6 or \6{iface} */
	AGETTY_ESC_NET_GOOD,	/* \a */
	AGETTY_ESC_NET_ALL,	/* \A */

	__AGETTY_ESC_COUNT
};

/* Item definition — static description of a supported escape code */
struct agetty_idef {
	const char		code;	/* escape character, e.g. 's', '4' */
	const char * const	*args;	/* NULL-terminated named arg list, or NULL */
};

/* Parsed item — one element from the issue file */
struct agetty_iitem {
	int			id;	/* AGETTY_ESC_* */
	char			*data;	/* text content (TEXT) or handler data */
	char			*arg;	/* argument from \x{arg}, or NULL */
	struct list_head	items;	/* linked list member */
};

/* Parsed issue file — container for items */
struct agetty_ifile {
	struct list_head	items;	/* list of struct agetty_iitem */
};

/* Iterator for traversing parsed items */
struct agetty_iiter {
	struct list_head	*p;	/* current position */
	struct list_head	*head;	/* list head (sentinel) */
	int			direction;
};

#define AGETTY_ITER_FORWARD	0
#define AGETTY_ITER_BACKWARD	1

/* list operations */
extern void agetty_ifile_init(struct agetty_ifile *ls);
extern void agetty_ifile_free(struct agetty_ifile *ls);
extern bool agetty_ifile_has_item(struct agetty_ifile *ls, int id);

/* iterator */
extern void agetty_iiter_init(struct agetty_iiter *itr, int direction);
extern int  agetty_ifile_next_item(struct agetty_ifile *ls,
				   struct agetty_iiter *itr,
				   struct agetty_iitem **item,
				   int filter_low, int filter_high);

/* item accessors */
extern int         agetty_iitem_get_id(struct agetty_iitem *item);
extern const char *agetty_iitem_get_data(struct agetty_iitem *item);
extern const char *agetty_iitem_get_arg(struct agetty_iitem *item);

/* definition lookups */
extern const struct agetty_idef *agetty_idef_by_code(int c);
extern const char *agetty_idef_get_code(int id);

/* parser */
extern int agetty_ifile_parse_stream(struct agetty_ifile *ls, FILE *f);
extern int agetty_ifile_parse_file(struct agetty_ifile *ls, const char *filename);

#endif /* UTIL_LINUX_AGETTY_ISSUEFILE_PARSER_H */
