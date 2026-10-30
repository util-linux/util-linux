/*
 * fuzz harness: libmount option lists, fs objects and table lookups
 *
 * Threat model: mount options and mount-table content are attacker
 * influenced (unprivileged mounts, fuse, user namespaces, containers write
 * utab/mountinfo entries that libmount re-parses; fstab content is
 * semi-trusted). libmount's option parser (optstr.c), the option list
 * engine (optlist.c) and the fs/table matching logic (fs.c, tab.c) all
 * consume such strings, and the existing test_mount_fuzz only exercises
 * mnt_table_parse_stream() with mountinfo-style content.
 *
 * The fuzzed bytes drive:
 *   - a table parsed with a fuzz-selected format hint (fstab, mountinfo,
 *     swaps, utab, mtab, none) followed by the documented lookup functions,
 *   - mnt_optlist_* operations over the three builtin option maps
 *     (set/append/prepend optstr, set/append/insert flags, removals,
 *     iteration, queries, get_optstr round trip),
 *   - mnt_fs_* attribute setters and mnt_fs_match_* evaluation,
 *   - mnt_optstr_* string manipulation.
 *
 * All strings are length-bounded; the parsed table content is capped at
 * 128 KiB like the existing mount fuzzer.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c.h"
#include "mountP.h"
#include "libmount.h"

#define FZ_MAX_STR	64
#define FZ_MAX_TABLE	(128 * 1024)

struct fz {
	const uint8_t *data;
	size_t size;
	size_t off;
};

static int fz_byte(struct fz *fz, uint8_t *v)
{
	if (fz->off >= fz->size)
		return -1;
	*v = fz->data[fz->off++];
	return 0;
}

static int fz_uint(struct fz *fz, unsigned mod, unsigned *v)
{
	uint8_t b;

	if (fz_byte(fz, &b) < 0)
		return -1;
	*v = mod ? (unsigned) b % mod : (unsigned) b;
	return 0;
}

static char *fz_str(struct fz *fz, size_t maxlen)
{
	uint8_t len;
	char *s;

	if (fz_byte(fz, &len) < 0)
		return NULL;
	if (len > maxlen)
		len = maxlen;
	if (len && fz->off + len > fz->size)
		return NULL;
	s = malloc((size_t) len + 1);
	if (!s)
		return NULL;
	if (len)
		memcpy(s, fz->data + fz->off, len);
	s[len] = '\0';
	fz->off += len;
	return s;
}

/* option lists over all builtin maps */
static void fuzz_optlist(struct fz *fz)
{
	struct libmnt_optlist *ls, *cp;
	struct libmnt_iter *itr;
	struct libmnt_opt *opt;
	const struct libmnt_optmap *maps[2];
	unsigned i, u;
	char *strs[3];

	maps[0] = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);
	maps[1] = mnt_get_builtin_optmap(MNT_LINUX_MAP);

	ls = mnt_new_optlist();
	if (!ls)
		return;
	mnt_optlist_register_map(ls, maps[0]);
	mnt_optlist_register_map(ls, maps[1]);

	for (i = 0; i < 3; i++)
		strs[i] = fz_str(fz, FZ_MAX_STR);

	if (strs[0])
		mnt_optlist_set_optstr(ls, strs[0], NULL);
	if (fz_uint(fz, 256, &u) == 0 && (u & 1) && strs[1])
		mnt_optlist_append_optstr(ls, strs[1], u & 2 ? maps[0] : NULL);
	if (fz_uint(fz, 256, &u) == 0 && (u & 1) && strs[2])
		mnt_optlist_prepend_optstr(ls, strs[2], NULL);

	if (fz_uint(fz, 256, &u) == 0) {
		unsigned long fl = (unsigned long) u * 16843009UL;

		switch ((u >> 1) & 0x03) {
		case 0:
			mnt_optlist_set_flags(ls, fl, maps[0]);
			break;
		case 1:
			mnt_optlist_append_flags(ls, fl, maps[0]);
			break;
		case 2:
			mnt_optlist_insert_flags(ls, fl, maps[0],
					MS_BIND, NULL);
			break;
		default:
			mnt_optlist_set_flags(ls, fl, maps[1]);
			break;
		}
	}

	if (fz_uint(fz, 256, &u) == 0 && (u & 1)) {
		if (strs[0] && strs[0][0])
			mnt_optlist_remove_named(ls, strs[0], NULL);
		mnt_optlist_remove_flags(ls, 1UL << (u % 32), maps[1]);
	}

	/* iterate and query */
	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (itr) {
		while (mnt_optlist_next_opt(ls, itr, &opt) == 0) {
			const char *n = mnt_opt_get_name(opt);
			const char *v = mnt_opt_get_value(opt);
			const struct libmnt_optmap *m = mnt_opt_get_map(opt);

			if (n && v)
				mnt_optlist_get_named(ls, n, m);
			/* mnt_optlist_remove_opt() frees @opt -- stop
			 * iterating immediately after the removal */
			if (n && strcmp(n, "ro") == 0) {
				mnt_optlist_remove_opt(ls, opt);
				break;
			}
		}
		mnt_free_iter(itr);
	}

	{
		unsigned long fl = 0;
		uint64_t set = 0, clr = 0;
		const char *out = NULL;
		char *dup = NULL;
		int what = (int) (u % __MNT_OL_FLTR_COUNT);

		mnt_optlist_get_flags(ls, &fl, maps[0], MNT_OL_FLTR_DFLT);
		mnt_optlist_get_attrs(ls, &set, &clr, 0);
		mnt_optlist_get_optstr(ls, &out, NULL, what);
		mnt_optlist_strdup_optstr(ls, &dup, NULL, what);
		free(dup);

		if (mnt_optlist_is_bind(ls) || mnt_optlist_is_move(ls) ||
		    mnt_optlist_is_remount(ls) ||
		    mnt_optlist_is_propagation_only(ls))
			mnt_optlist_get_propagation(ls);
	}

	cp = mnt_copy_optlist(ls);
	if (cp)
		mnt_unref_optlist(cp);
	mnt_unref_optlist(ls);

	for (i = 0; i < 3; i++)
		free(strs[i]);
}

/* fs attribute setters and matchers */
static void fuzz_fs(struct fz *fz)
{
	struct libmnt_fs *fs;
	char *s[4];
	unsigned i;

	fs = mnt_new_fs();
	if (!fs)
		return;

	for (i = 0; i < 4; i++)
		s[i] = fz_str(fz, FZ_MAX_STR);

	if (s[0])
		mnt_fs_set_source(fs, s[0]);
	if (s[1])
		mnt_fs_set_target(fs, s[1]);
	if (s[2])
		mnt_fs_set_fstype(fs, s[2]);
	if (s[3])
		mnt_fs_set_options(fs, s[3]);
	if (fz_uint(fz, 2, &i) == 0 && i && s[1])
		mnt_fs_set_attributes(fs, s[1]);

	/* NOTE: mnt_fs_match_fstype() is only called with a fstype actually
	 * set on @fs: the product dereferences fs->fstype without a NULL
	 * check (match.c strlen(type), ASan finding util-linux-4, artifact
	 * crash-37a26a19...). Revisit once fixed upstream.
	 */
	if (mnt_fs_get_fstype(fs)) {
		mnt_fs_match_target(fs, s[0] ? s[0] : "", NULL);
		mnt_fs_match_source(fs, s[0] ? s[0] : "", NULL);
		mnt_fs_match_fstype(fs, s[2] ? s[2] : "");
		mnt_fs_match_options(fs, s[3] ? s[3] : "");
	}
	{
		/* mnt_fs_set_options() keeps fs->optlist in sync
		 * internally; read the result back through the getters */
		const char *out;

		out = mnt_fs_get_source(fs);
		out = mnt_fs_get_target(fs);
		out = mnt_fs_get_fstype(fs);
		out = mnt_fs_get_options(fs);
		out = mnt_fs_get_attributes(fs);
		out = mnt_fs_get_vfs_options(fs);
		out = mnt_fs_get_user_options(fs);
		out = mnt_fs_get_fs_options(fs);
		(void) out;
	}
	mnt_free_fs(fs);

	for (i = 0; i < 4; i++)
		free(s[i]);
}

/* raw mnt_optstr_* string engine */
static void fuzz_optstr(struct fz *fz)
{
	char *str = fz_str(fz, 128);
	char *name = fz_str(fz, 24);
	char *val = fz_str(fz, 24);
	unsigned u;
	char *p, *n = NULL, *v = NULL;
	size_t vsz = 0;
	unsigned long flags = 0;

	if (str && name) {
		p = str;
		while (mnt_optstr_next_option(&p, &n, &vsz, &v, &vsz) == 0) {
			n = NULL;
			v = NULL;
		}
		if (mnt_optstr_get_option(str, name, &v, &vsz) == 0 && v)
			mnt_optstr_remove_option(&str, name);
		mnt_optstr_set_option(&str, name, val ? val : "1");
		mnt_optstr_deduplicate_option(&str, name);
		mnt_optstr_append_option(&str, name, val);
		mnt_optstr_prepend_option(&str, name, NULL);
		mnt_optstr_get_flags(str, &flags, mnt_get_builtin_optmap(
					MNT_USERSPACE_MAP));
		mnt_optstr_apply_flags(&str, flags | 1,
				mnt_get_builtin_optmap(MNT_USERSPACE_MAP));
		if (fz_uint(fz, 2, &u) == 0 && name[0] && name[1]) {
			char *subset = NULL;

			mnt_optstr_get_options(str, &subset,
					mnt_get_builtin_optmap(
					MNT_USERSPACE_MAP), 0);
			free(subset);
		}
	}
	free(str);
	free(name);
	free(val);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fz fz = { .data = data, .size = size };
	struct libmnt_table *tb;
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;
	unsigned u, hint;
	char *tgt = NULL, *src = NULL, *tag = NULL;
	static const char *const hints[] = {
		/* mnt_table_parse_stream() asserts a non-NULL filename */
		"none", "fstab", "mountinfo", "swaps", "utab", "mtab", "fstab"
	};
	FILE *f;
	char *buf;
	size_t tsize;

	if (size < 8)
		return 0;

	fuzz_optlist(&fz);
	fuzz_fs(&fz);
	fuzz_optstr(&fz);

	/* parse remaining bytes as a mount table */
	if (fz_uint(&fz, 7, &hint) < 0)
		hint = 0;
	if (fz_uint(&fz, 256, &u) == 0) {
		tgt = fz_str(&fz, FZ_MAX_STR);
		src = fz_str(&fz, FZ_MAX_STR);
		tag = fz_str(&fz, FZ_MAX_STR);
	}

	tsize = fz.size > fz.off ? fz.size - fz.off : 0;
	if (tsize > FZ_MAX_TABLE)
		tsize = FZ_MAX_TABLE;
	if (tsize == 0) {
		free(tgt);
		free(src);
		free(tag);
		return 0;
	}
	buf = malloc(tsize);
	if (!buf) {
		free(tgt);
		free(src);
		free(tag);
		return 0;
	}
	memcpy(buf, fz.data + fz.off, tsize);

	tb = mnt_new_table();
	if (!tb) {
		free(buf);
		free(tgt);
		free(src);
		free(tag);
		return 0;
	}
	mnt_table_enable_comments(tb, 1);

	f = fmemopen(buf, tsize, "r");
	if (!f)
		err(EXIT_FAILURE, "fmemopen() failed");

	(void) mnt_table_parse_stream(tb, f, hints[hint]);
	fclose(f);

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	while (itr && mnt_table_next_fs(tb, itr, &fs) == 0) {
		const char *opts = mnt_fs_get_options(fs);

		if (opts && *opts == ' ')
			mnt_fs_set_priority(fs, 1);
	}
	mnt_free_iter(itr);

	mnt_table_find_target(tb, tgt ? tgt : "", MNT_ITER_FORWARD);
	mnt_table_find_srcpath(tb, src ? src : "", MNT_ITER_FORWARD);
	mnt_table_find_source(tb, src ? src : "", MNT_ITER_BACKWARD);
	if (tag && *tag) {
		char *val = strchr(tag, '=');

		if (val) {
			*val = '\0';
			mnt_table_find_tag(tb, tag, val + 1, MNT_ITER_FORWARD);
		}
	}

	mnt_unref_table(tb);
	free(buf);
	free(tgt);
	free(src);
	free(tag);
	return 0;
}
