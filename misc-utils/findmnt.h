#ifndef UTIL_LINUX_FINDMNT_H
#define UTIL_LINUX_FINDMNT_H

/* flags */
enum {
	FL_EVALUATE	= (1 << 1),
	FL_CANONICALIZE = (1 << 2),
	FL_FIRSTONLY	= (1 << 3),
	FL_INVERT	= (1 << 4),
	FL_NOSWAPMATCH	= (1 << 6),
	FL_NOFSROOT	= (1 << 7),
	FL_SUBMOUNTS	= (1 << 8),
	FL_POLL		= (1 << 9),
	FL_DF		= (1 << 10),
	FL_ALL		= (1 << 11),
	FL_UNIQ		= (1 << 12),
	FL_BYTES	= (1 << 13),
	FL_NOCACHE	= (1 << 14),
	FL_STRICTTARGET = (1 << 15),
	FL_VERBOSE	= (1 << 16),
	FL_PSEUDO	= (1 << 17),
	FL_REAL		= (1 << 18),
	FL_VFS_ALL	= (1 << 19),
	FL_SHADOWED	= (1 << 20),
	FL_DELETED      = (1 << 21),
	FL_SHELLVAR     = (1 << 22),
	FL_DF_INODES	= (1 << 23),

	/* basic table settings */
	FL_ASCII	= (1 << 25),
	FL_RAW		= (1 << 26),
	FL_NOHEADINGS	= (1 << 27),
	FL_EXPORT	= (1 << 28),
	FL_TREE		= (1 << 29),
	FL_JSON		= (1 << 30),
};

struct findmnt {
	struct libmnt_cache *cache;
	unsigned int flags;
	int parse_nerrors;
	char *uri;
	struct libscols_filter *filter;
};

extern int is_listall_mode(unsigned int flags);
extern struct libmnt_fs *get_next_fs(struct libmnt_table *tb, struct libmnt_iter *itr, struct findmnt *findmnt);
extern int verify_table(struct libmnt_table *tb, struct findmnt *findmnt);

#endif /* UTIL_LINUX_FINDMNT_H */
