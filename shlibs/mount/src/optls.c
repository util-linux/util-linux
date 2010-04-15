/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: optls
 * @title: Options container
 * @short_description: high-level API for work with parsed mount options
 *
 * The optls container allows to work with parsed mount options and generate
 * arguments for mount(2) syscall, output to mtab or analyze userspace specific
 * options.
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "nls.h"
#include "mountP.h"

/**
 * mnt_new_optls:
 *
 * Returns: newly allocated and initialized optls instance. The library
 * uses this object as a container for mount options.
 */
mnt_optls *mnt_new_optls(void)
{
	mnt_optls *ls = calloc(1, sizeof(struct _mnt_optls));
	if (!ls)
		return NULL;
	INIT_LIST_HEAD(&ls->opts);
	return ls;
}

/**
 * mnt_free_optls:
 * @ls: pointer to mnt_optls instance.
 *
 * Deallocates mnt_optls and all stored options.
 */
void mnt_free_optls(mnt_optls *ls)
{
	if (!ls)
		return;
	while (!list_empty(&ls->opts)) {
		mnt_optent *o = list_entry(ls->opts.next, mnt_optent, opts);
		mnt_free_optent(o);
	}

	free(ls->maps);
	free(ls);
}

/**
 * mnt_optls_add_map:
 * @ls: pointer to mnt_optls instance
 * @map: pointer to the custom map
 *
 * Stores pointer to the custom options map (options description). The map has
 * to be accessible all time when the libmount works with options. (The map is
 * usually a static array.)
 *
 * All already stored unknown mount options are reverified against the new map.
 * Note, it's recommented to add all maps to the @optls container before options
 * parsing.
 *
 * Example (add new options "foo" and "bar=data"):
 *
 * <informalexample>
 *   <programlisting>
 *     #define MY_MS_FOO   (1 << 1)
 *     #define MY_MS_BAR   (1 << 2)
 *
 *     mnt_optmap myoptions[] = {
 *       { "foo",   MY_MS_FOO, MNT_MFLAG },
 *       { "nofoo", MY_MS_FOO, MNT_MFLAG | MNT_INVERT },
 *       { "bar=%s",MY_MS_BAR, MNT_MDATA },
 *       { NULL }
 *     };
 *
 *     mnt_optls_add_map(ls, myoptions);
 *   </programlisting>
 * </informalexample>
 *
 * Returns: 0 on success, 1 on failed verification, or -1 in case of error.
 */
int mnt_optls_add_map(mnt_optls *ls, const struct mnt_optmap *map)
{
	mnt_optent *op;
	mnt_iter itr;

	assert(ls);
	assert(map || ls->maps == NULL);

	ls->maps = realloc(ls->maps,
			sizeof(struct mnt_optmap *) * (ls->nmaps + 1));
	if (!ls->maps)
		return -1;

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: optls %p: add map[%zd]", ls, ls->nmaps));
	ls->maps[ls->nmaps] = map;
	ls->nmaps++;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while(mnt_optls_next_option(ls, &itr, NULL, &op) == 0) {
		if (!mnt_optent_is_unknown(op))
			continue;
		if (mnt_optent_assign_map(op, &map, 1) == -1)
			return 1;
	}
	return 0;
}

/**
 * mnt_optls_add_builtin_map:
 * @ls: pointer to mnt_optls instance
 * @id: built-in map id (see mnt_get_builtin_map())
 *
 * Same as mnt_optls_add_map(), but works with libmount built in maps.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_optls_add_builtin_map(mnt_optls *ls, int id)
{
	const struct mnt_optmap *m = mnt_get_builtin_optmap(id);

	assert(ls);
	assert(id);

	return m ? mnt_optls_add_map(ls, m) : -1;
}


/*
 * Append the option to "ls" container.
 */
static void mnt_optls_add_optent(mnt_optls *ls, mnt_optent *op)
{
	assert(ls);
	assert(op);

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: add option %s",
		ls, mnt_optent_get_name(op)));

	list_add_tail(&op->opts, &ls->opts);
}

/**
 * mnt_optls_add_option:
 * @ls: pointer to mnt_optls instance
 * @name: option name
 * @value: option value
 *
 * Returns: new option or NULL in case of error.
 */
mnt_optent *mnt_optls_add_option(mnt_optls *ls,
			const char *name, const char *value)
{
	mnt_optent *op;

	if (!ls || !name)
		return NULL;

	op = mnt_new_optent(name, strlen(name),
				value, value ? strlen(value) : 0,
                                ls->maps, ls->nmaps);
	if (op)
		mnt_optls_add_optent(ls, op);
	return op;
}

/**
 * mnt_optls_parse_optstr:
 * @ls: pointer to mnt_optls instance.
 * @optstr: zero terminated string with mount options (comma separaed list)
 *
 * Parses @optstr and all options from @optstr are added to the @optls. It's
 * possible to call this function more than once. The new options from @optstr
 * will be appended to the container.
 *
 * The options are accessible by mnt_optls_next_option().
 *
 * If the @optls container is associated with any options map(s), all new
 * options are verified according to the descriptions from the map(s).
 *
 * For example:
 *
 *	mnt_optls_parse_optstr(ls, "user=snake,noexec");
 *
 * is same like:
 *
 *      mnt_optls_add_option(ls, "user", "snake");
 *      mnt_optls_add_option(ls, "noexec", NULL);
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_optls_parse_optstr(mnt_optls *ls, const char *optstr)
{
	char *p = (char *) optstr;

	assert(ls);
	assert(optstr);

	if (!ls || !optstr)
		return -1;

	while(p && *p) {
		mnt_optent *op = mnt_new_optent_from_optstr(&p,
					ls->maps, ls->nmaps);
		if (!op)
			return -1;
		mnt_optls_add_optent(ls, op);
	}
	return 0;
}

/**
 * mnt_optls_remove_option:
 * @ls: pointer to mnt_optls instance
 * @name: option name
 *
 * Returns: 0 on success, 1 if @name not found and -1 in case of error.
 */
int mnt_optls_remove_option(mnt_optls *ls, const char *name)
{
	struct list_head *p, *pnext;

	if (!ls || !name)
		return -1;

	list_for_each_safe(p, pnext, &ls->opts) {
		mnt_optent *op;
		const char *n;

		if (!p)
			break;
		op = list_entry(p, mnt_optent, opts);
		n = mnt_optent_get_name(op);
		if (n && strcmp(name, n) == 0) {
			mnt_free_optent(op);
			return 0;
		}
	}
	return 1;
}


/**
 * mnt_optls_remove_option_by_flags:
 * @ls: pointer to mnt_optls instance
 * @map: pointer to the map with wanted options or NULL for all options
 * @flags: option flags
 *
 * Removes options which match with @flags. The set of options could
 * be restricted by @map. For exmaple:
 *
 *	mnt_optls_remove_option_by_flags(ls, NULL, MS_NOEXEC);
 *
 * removes "noexec" option from "ls".
 *
 * Note that this function is useles for options with MNT_INVERT mask (e.g.
 * "exec" is inverting MS_NOEXEC flag).
 *
 * See also mnt_optent_get_flag() and mnt_optls_remove_option_by_iflags().
 *
 * Returns: number of removed options or -1 in case of error.
 */
int mnt_optls_remove_option_by_flags(mnt_optls *ls,
		const struct mnt_optmap *map, const int flags)
{
	struct list_head *p, *pnext;
	int ct = 0;

	if (!ls)
		return -1;

	list_for_each_safe(p, pnext, &ls->opts) {
		mnt_optent *op;
		int fl = 0;

		if (!p)
			break;
		op = list_entry(p, mnt_optent, opts);

		if (!map || mnt_optent_get_map(op) == map) {
			mnt_optent_get_flag(op, &fl);
			if (fl & flags) {
				mnt_free_optent(op);
				ct++;
			}
		}
	}
	return ct;
}

/**
 * mnt_optls_remove_option_by_iflags:
 * @ls: pointer to mnt_optls instance
 * @map: pointer to the map with wanted options or NULL for all options
 * @flags: option flags
 *
 * Removes options which inverting any id from @flags. The set of options could
 * be restricted by @map. For exmaple:
 *
 *	mnt_optls_remove_option_by_iflags(ls, NULL, MS_NOEXEC);
 *
 * removes "exec" option from "ls".
 *
 * Note that this function is useles for options without MNT_INVERT mask (e.g.
 * "noexec").
 *
 * See also mnt_optent_get_flag() and mnt_optls_remove_option_by_flags().
 *
 * Returns: number of removed options or -1 in case of error.
 */
int mnt_optls_remove_option_by_iflags(mnt_optls *ls,
		const struct mnt_optmap *map, const int flags)
{
	struct list_head *p, *pnext;
	int ct = 0;

	if (!ls)
		return -1;

	list_for_each_safe(p, pnext, &ls->opts) {
		mnt_optent *op;
		int fl = flags;

		if (!p)
			break;
		op = list_entry(p, mnt_optent, opts);

		if (!map || mnt_optent_get_map(op) == map) {
			int id = mnt_optent_get_id(op);

			if (!(id & fl))
				continue;

			mnt_optent_get_flag(op, &fl);

			if (!(id & fl)) {
				mnt_free_optent(op);
				ct++;
			}
		}
	}
	return ct;
}

/**
 * mnt_optls_next_option:
 * @ls: pointer to mnt_optls instance
 * @itr: iterator
 * @map: pointer to the map of wanted options or NULL for all options
 * @option: returns pointer to the option object
 *
 * Example (print all options):
 * <informalexample>
 *   <programlisting>*
 *     mnt_optent *option;
 *     mnt_optls *ls = mnt_optls_new();
 *
 *     mnt_optls_parse_optstr(ls, "noexec,nodev");
 *
 *     while(mnt_optls_next_option(ls, itr, NULL, &option))
 *         printf("%s\n", mnt_optent_get_name(option)));
 *   </programlisting>
 * </informalexample>
 *
 * Returns: 0 on succes, -1 in case of error or 1 at end of list.
 */
int mnt_optls_next_option(mnt_optls *ls, mnt_iter *itr,
		const struct mnt_optmap *map, mnt_optent **option)
{
	assert(itr);
	assert(ls);
	assert(option);

	if (!itr || !ls || !option)
		return -1;
	if (!itr->head)
		MNT_ITER_INIT(itr, &ls->opts);
	while (itr->p != itr->head) {
		MNT_ITER_ITERATE(itr, *option, struct _mnt_optent, opts);
		if (map == NULL || (*option)->map == map)
			return 0;
	}

	return 1;
}

/**
 * mnt_optls_get_option:
 * @ls: pointer to mnt_optls instance
 * @name: options name
 *
 * Returns: the option or NULL.
 */
mnt_optent *mnt_optls_get_option(mnt_optls *ls, const char *name)
{
	mnt_optent *op;
	mnt_iter itr;

	assert(ls);
	assert(name);

	if (!ls || !name)
		return NULL;
	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while(mnt_optls_next_option(ls, &itr, NULL, &op) == 0) {
		const char *n = mnt_optent_get_name(op);

		if (n && !strcmp(n, name))
			return op;
	}
	return NULL;
}

/**
 * mnt_optls_get_ids:
 * @ls: pointer to mnt_optls instance
 * @map: pointer to the map of wanted options or NULL for all options
 *
 * Note that ID has to be unique in all maps when the @map is NULL.
 *
 * Note also that this function works with ALL options -- see also
 * mnt_optls_create_mountflags() that returns MNT_MFLAG options
 * (mount(2) flags) only.
 *
 * Returns: IDs from all options.
 */
int mnt_optls_get_ids(mnt_optls *ls, const struct mnt_optmap *map)
{
	int flags = 0;
	mnt_iter itr;
	mnt_optent *op;

	assert(ls);
	if (!ls)
		return 0;
	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while(mnt_optls_next_option(ls, &itr, map, &op) == 0)
		mnt_optent_get_flag(op, &flags);

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: generated IDs 0x%08x", ls, flags));
	return flags;
}

/**
 * mnt_optls_create_mountflags:
 * @ls: pointer to mnt_optls instance
 *
 * The mountflags are IDs from all MNT_MFLAG options. See "struct mnt_optmap".
 * For more details about mountflags see mount(2) syscall.
 *
 * Returns: mount flags or 0.
 */
int mnt_optls_create_mountflags(mnt_optls *ls)
{
	int flags = 0;
	mnt_iter itr;
	mnt_optent *op;

	assert(ls);
	if (!ls)
		return 0;
	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while(mnt_optls_next_option(ls, &itr, NULL, &op) == 0) {
		if (!(op->mask & MNT_MFLAG))
			continue;
		mnt_optent_get_flag(op, &flags);
	}

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: generated mountflags 0x%08x", ls, flags));
	return flags;
}

/**
 * mnt_optls_create_mountdata:
 * @ls: pointer to mnt_optls instance
 *
 * For more details about mountdata see mount(2) syscall.
 *
 * Returns: newly allocated string with mount options or NULL in case of error.
 */
char *mnt_optls_create_mountdata(mnt_optls *ls)
{
	mnt_iter itr;
	mnt_optent *op;
	char *optstr = NULL;

	assert(ls);
	if (!ls)
		return NULL;
	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while(mnt_optls_next_option(ls, &itr, NULL, &op) == 0) {
		if (!(op->mask & MNT_MDATA) && !mnt_optent_is_unknown(op))
			continue;
		if (mnt_optstr_append_option(&optstr,
				mnt_optent_get_name(op),
				mnt_optent_get_value(op)))
			goto err;
	}

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: generated mountdata: %s", ls, optstr));
	return optstr;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: ls %p: generate mountdata failed", ls));
	free(optstr);
	return NULL;
}

/**
 * mnt_optls_create_mtab_optstr:
 * @ls: pointer to mnt_optls instance
 *
 * Returns: newly allocated string with mount options for mtab.
 */
char *mnt_optls_create_mtab_optstr(mnt_optls *ls)
{
	mnt_iter itr;
	mnt_optent *op;
	char *optstr = NULL;

	assert(ls);
	if (!ls)
		return NULL;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while(mnt_optls_next_option(ls, &itr, NULL, &op) == 0) {
		if (op->mask & MNT_NOMTAB)
			continue;
		if (mnt_optstr_append_option(&optstr,
				mnt_optent_get_name(op),
				mnt_optent_get_value(op)))
			goto err;
	}

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: generated mtab options: %s", ls, optstr));
	return optstr;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: generate mtab optstr failed", ls));
	free(optstr);
	return NULL;
}

/**
 * mnt_optls_create_userspace_optstr:
 * @ls: pointer to mnt_optls instance
 *
 * Returns: newly allocated string with mount options that are
 * userspace specific (e.g. uhelper=,loop=).
 */
char *mnt_optls_create_userspace_optstr(mnt_optls *ls)
{
	mnt_iter itr;
	mnt_optent *op;
	char *optstr = NULL;

	assert(ls);
	if (!ls)
		return NULL;
	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while(mnt_optls_next_option(ls, &itr, NULL, &op) == 0) {
		if (mnt_optent_is_unknown(op))
			continue;
		if (op->mask & (MNT_MDATA | MNT_MFLAG | MNT_NOMTAB))
			continue;
		if (mnt_optstr_append_option(&optstr,
				mnt_optent_get_name(op),
				mnt_optent_get_value(op)))
			goto err;
	}

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: generated userspace-only options: %s",
		ls, optstr));
	return optstr;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: opts %p: generate userspace optstr failed", ls));
	free(optstr);
	return NULL;
}

/**
 * mnt_optls_print_debug:
 * @file: output
 * @ls: pointer to mnt_optls instance
 *
 * Prints details about options container.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_optls_print_debug(mnt_optls *ls, FILE *file)
{
	mnt_iter itr;
	mnt_optent *op;

	if (!ls)
		return -1;
	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	fprintf(file, "--- opts: %p\n", ls);
	while(mnt_optls_next_option(ls, &itr, NULL, &op) == 0)
		mnt_optent_print_debug(op, file);

	return 0;
}

#ifdef TEST_PROGRAM
mnt_optls *mk_optls(const char *optstr)
{
	mnt_optls *ls = mnt_new_optls();
	if (!ls)
		goto err;

	mnt_optls_add_builtin_map(ls, MNT_LINUX_MAP);
	mnt_optls_add_builtin_map(ls, MNT_USERSPACE_MAP);

	if (mnt_optls_parse_optstr(ls, optstr) != 0) {
		fprintf(stderr, "\tfailed to parse: %s\n", optstr);
		goto err;
	}
	return ls;
err:
	mnt_free_optls(ls);
	return NULL;
}

int test_parse(struct mtest *ts, int argc, char *argv[])
{
	mnt_optls *ls = NULL;
	int rc = -1;

	if (argc < 1)
		goto done;
	ls = mk_optls(argv[1]);
	if (!ls)
		goto done;

	mnt_optls_print_debug(ls, stdout);
	rc = 0;
done:
	mnt_free_optls(ls);
	return rc;
}

int test_flags(struct mtest *ts, int argc, char *argv[])
{
	mnt_optls *ls = NULL;
	int rc = -1;
	int flags;
	const struct mnt_optmap *map;

	if (argc < 1)
		goto done;
	ls = mk_optls(argv[1]);
	if (!ls)
		goto done;

	flags = mnt_optls_create_mountflags(ls);
	printf("\tmount(2) flags:        0x%08x\n", flags);

	map = mnt_get_builtin_optmap(MNT_LINUX_MAP);
	flags = mnt_optls_get_ids(ls, map);
	printf("\tMNT_MAP_LINUX IDs:     0x%08x  (map %p)\n", flags, map);

	map = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);
	flags = mnt_optls_get_ids(ls, map);
	printf("\tMNT_USERSPACE_MAP IDs: 0x%08x  (map %p)\n", flags, map);

	rc = 0;
done:
	mnt_free_optls(ls);
	return rc;
}

int test_data(struct mtest *ts, int argc, char *argv[])
{
	mnt_optls *ls = NULL;
	char *optstr;
	int rc = -1;

	if (argc < 1)
		goto done;
	ls = mk_optls(argv[1]);
	if (!ls)
		goto done;

	optstr = mnt_optls_create_mountdata(ls);
	printf("\tmount(2) data: '%s'\n", optstr);
	free(optstr);
	rc = 0;
done:
	mnt_free_optls(ls);
	return rc;
}

int test_mtabstr(struct mtest *ts, int argc, char *argv[])
{
	mnt_optls *ls = NULL;
	char *optstr;
	int rc = -1;

	if (argc < 1)
		goto done;
	ls = mk_optls(argv[1]);
	if (!ls)
		goto done;

	optstr = mnt_optls_create_mtab_optstr(ls);
	printf("\tmtab options: '%s'\n", optstr);
	free(optstr);
	rc = 0;
done:
	mnt_free_optls(ls);
	return rc;
}

int test_reparse(struct mtest *ts, int argc, char *argv[])
{
	const struct mnt_optmap *map;
	mnt_optls *ls = NULL;
	char *optstr;
	int rc = -1;

	if (argc < 1)
		goto done;
	optstr = argv[1];
	ls = mnt_new_optls();
	if (!ls)
		goto done;

	/* add description for kernel options */
	mnt_optls_add_builtin_map(ls, MNT_LINUX_MAP);

	if (mnt_optls_parse_optstr(ls, optstr) != 0) {
		fprintf(stderr, "\tfailed to parse: %s\n", optstr);
		goto done;
	}

	fprintf(stdout, "------ parse\n");
	mnt_optls_print_debug(ls, stdout);

	/* add description for userspace options */
	map = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);
	mnt_optls_add_map(ls, map);

	fprintf(stdout, "------ re-parse\n");
	mnt_optls_print_debug(ls, stdout);

	rc = 0;
done:
	mnt_free_optls(ls);
	return rc;
}

int main(int argc, char *argv[])
{
	struct mtest tss[] = {
		{ "--parse",    test_parse, "<optstr>  parse mount options string" },
		{ "--ls-data",	test_data,  "<optstr>  parse and generate mountdata" },
		{ "--ls-flags", test_flags, "<optstr>  parse and generate mountflags" },
		{ "--ls-mtabstr",test_mtabstr,"<optstr>  parse and generate mtab options" },
		{ "--reparse",  test_reparse, "<optstr>  test extra options reparsing" },
		{ NULL }
	};
	return  mnt_run_test(tss, argc, argv);
}
#endif /* TEST_PROGRAM */
