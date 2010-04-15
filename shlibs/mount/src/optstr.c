/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: optstr
 * @title: Options string
 * @short_description: low-level API for work with mount options
 *
 * This is simple and low-level API to work with mount options that are stored
 * in string. This API is independent on the high-level options container and
 * option maps.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "nls.h"
#include "mountP.h"

/*
 * Parses the first option from @optstr. The @optstr pointer is set to begin of
 * the next option.
 *
 * Returns -1 on parse error, 1 at the end of optstr and 0 on success.
 */
static int mnt_optstr_parse_next(char **optstr,	 char **name, size_t *namesz,
					char **value, size_t *valsz)
{
	int open_quote = 0;
	char *start = NULL, *stop = NULL, *p, *sep = NULL;
	char *optstr0;

	assert(optstr);
	assert(*optstr);

	optstr0 = *optstr;

	if (name)
		*name = NULL;
	if (namesz)
		*namesz = 0;
	if (value)
		*value = NULL;
	if (valsz)
		*valsz = 0;

	for (p = optstr0; p && *p; p++) {
		if (!start)
			start = p;		/* begin of the option item */
		if (*p == '"')
			open_quote ^= 1;	/* reverse the status */
		if (open_quote)
			continue;		/* still in quoted block */
		if (!sep && *p == '=')
			sep = p;		/* name and value separator */
		if (*p == ',')
			stop = p;		/* terminate the option item */
		else if (*(p + 1) == '\0')
			stop = p + 1;		/* end of optstr */
		if (!start || !stop)
			continue;
		if (stop <= start)
			goto error;

		if (name)
			*name = start;
		if (namesz)
			*namesz = sep ? sep - start : stop - start;
		*optstr = *stop ? stop + 1 : stop;

		if (sep) {
			if (value)
				*value = sep + 1;
			if (valsz)
				*valsz = stop - sep - 1;
		}
		return 0;
	}

	return 1;				/* end of optstr */

error:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
			"libmount: parse error: \"%s\"\n", optstr0));
	return -1;
}

/*
 * Locates the first option that match with @name. The @end is set to
 * char behind the option (it means ',' or \0).
 *
 * Returns -1 on parse error, 1 when not found and 0 on success.
 */
static int mnt_optstr_locate_option(char *optstr, const char *name, char **begin,
					char **end, char **value, size_t *valsz)
{
	char *n;
	size_t namesz, nsz;
	int rc;

	assert(name);
	assert(optstr);

	namesz = strlen(name);

	do {
		rc = mnt_optstr_parse_next(&optstr, &n, &nsz, value, valsz);
		if (rc)
			break;

		if (namesz == nsz && strncmp(n, name, nsz) == 0) {
			if (begin)
				*begin = n;
			if (end)
				*end = *(optstr - 1) == ',' ?
					optstr - 1 : optstr;
			return 0;
		}
	} while(1);

	DBG(DEBUG_OPTIONS, fprintf(stderr,
			"libmount: can't found '%s' option\n", name));
	return rc;
}

/**
 * mnt_optstr_next_option:
 * @optstr: option string, returns position to next option
 * @name: returns option name
 * @namesz: returns option name length
 * @value: returns option value or NULL
 * @valuesz: returns option value length or zero
 *
 * Parses the first option in @optstr  or -1 in case of error.
 *
 * Returns: 0 on success, 1 at the end of @optstr or -1 in case of error.
 */
int mnt_optstr_next_option(char **optstr, char **name, size_t *namesz,
					char **value, size_t *valuesz)
{
	if (!optstr || !*optstr)
		return -1;
	return mnt_optstr_parse_next(optstr, name, namesz, value, valuesz);
}

/**
 * mnt_optstr_append_option:
 * @optstr: option string or NULL
 * @name: value name
 * @value: value
 *
 * Returns: reallocated (or newly allocated) @optstr with ,name=value
 */
int mnt_optstr_append_option(char **optstr, const char *name, const char *value)
{
	char *p;
	size_t sz, vsz, osz, nsz;

	if (!name)
		return -1;

	osz = *optstr ? strlen(*optstr) : 0;
	nsz = strlen(name);
	vsz = value ? strlen(value) : 0;

	sz = osz + nsz + 1;		/* 1: '\0' */
	if (osz)
		sz++;			/* ',' options separator */
	if (vsz)
		sz += vsz + 1;		/* 1: '=' */

	p = realloc(*optstr, sz);
	if (!p)
		return -1;
	*optstr = p;

	if (osz) {
		p += osz;
		*p++ = ',';
	}

	memcpy(p, name, nsz);
	p += nsz;

	if (vsz) {
		*p++ = '=';
		memcpy(p, value, vsz);
		p += vsz;
	}
	*p = '\0';

	return 0;
}

/**
 * mnt_optstr_get_option:
 * @optstr: string with comma separated list of options
 * @name: requested option name
 * @value: returns pointer to the begin of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of the value or 0
 *
 * Returns: 0 on success, 1 when not found the @name or -1 in case of error.
 */
int mnt_optstr_get_option(char *optstr, const char *name,
				char **value, size_t *valsz)
{
	return mnt_optstr_locate_option(optstr, name, NULL, NULL, value, valsz);
}

/* Removes substring located between @begin and @end from @str
 * -- result never starts or ends with comma or contains two commas
 *    (e.g. ",aaa,bbb" or "aaa,,bbb" or "aaa,")
 */
static void remove_substring(char *str, char *begin, char *end)
{
	size_t sz = strlen(end);

	if ((begin == str || *(begin - 1) == ',') && *end == ',')
		end++;

	memmove(begin, end, sz + 1);
	if (!*begin && *(begin - 1) == ',')
		*(begin - 1) = '\0';
}

/* insert '=substr' to @str on position @pos */
static int insert_substring(char **str, char *pos, const char *substr)
{
	char *p;
	size_t ssz = strlen(substr);	/* substring size */

	p = realloc(*str, strlen(*str) + 1 + ssz);
	if (!p)
		return -1;
	*str = p;

	memmove(pos + ssz + 1, pos, strlen(pos) + 1);
	*pos++ = '=';
	memcpy(pos, substr, ssz);
	return 0;
}

/**
 * mnt_optstr_set_option:
 * @optstr: string with comma separated list of options
 * @name: requested option
 * @value: new value or NULL
 *
 * Set or unset option @value.
 *
 * Returns: 0 on success, 1 when not found the @name or -1 in case of error.
 */
int mnt_optstr_set_option(char **optstr, const char *name, const char *value)
{
	char *val = NULL, *begin, *end, *nameend;
	size_t valsz;
	int rc = 1;

	if (!optstr)
		return -1;
	if (*optstr)
		rc = mnt_optstr_locate_option(*optstr, name,
					&begin, &end, &val, &valsz);
	if (rc == -1)
		/* parse error */
		return -1;
	if (rc == 1)
		/* not found */
		return mnt_optstr_append_option(optstr, name, value);

	nameend = begin + strlen(name);

	if (value == NULL && val && valsz)
		/* remove unwanted "=value" */
		remove_substring(*optstr, nameend, end);

	else if (value && val == NULL)
		/* insert "=value" */
		rc = insert_substring(optstr, nameend, value);

	else if (value && val && strlen(value) == valsz)
		/* simply replace =value */
		memcpy(val, value, valsz);

	else if (value && val) {
		remove_substring(*optstr, nameend, end);
		rc = insert_substring(optstr, nameend, value);
	}


	return 0;
}

/**
 * mnt_optstr_remove_option:
 * @optstr: string with comma separated list of options
 * @name: requested option name
 *
 * Returns: 0 on success, 1 when not found the @name or -1 in case of error.
 */
int mnt_optstr_remove_option(char **optstr, const char *name)
{
	char *begin, *end;
	int rc;

	rc = mnt_optstr_locate_option(*optstr, name,
				&begin, &end, NULL, NULL);
	if (rc != 0)
		return rc;

	remove_substring(*optstr, begin, end);
	return 0;
}

#ifdef TEST_PROGRAM

int test_append(struct mtest *ts, int argc, char *argv[])
{
	const char *value = NULL, *name;
	char *optstr;

	if (argc < 3)
		goto done;
	optstr = strdup(argv[1]);
	name = argv[2];

	if (argc == 4)
		value = argv[3];

	if (mnt_optstr_append_option(&optstr, name, value) == 0) {
		printf("result: >%s<\n", optstr);
		return 0;
	}
done:
	return -1;
}

int test_set(struct mtest *ts, int argc, char *argv[])
{
	const char *value = NULL, *name;
	char *optstr;

	if (argc < 3)
		goto done;
	optstr = strdup(argv[1]);
	name = argv[2];

	if (argc == 4)
		value = argv[3];

	if (mnt_optstr_set_option(&optstr, name, value) == 0) {
		printf("result: >%s<\n", optstr);
		return 0;
	}
done:
	return -1;
}

int test_get(struct mtest *ts, int argc, char *argv[])
{
	char *optstr;
	const char *name;
	char *val = NULL;
	size_t sz = 0;
	int rc;

	if (argc < 2)
		goto done;
	optstr = argv[1];
	name = argv[2];

	rc = mnt_optstr_get_option(optstr, name, &val, &sz);
	if (rc == 0) {
		printf("found; name: %s", name);
		if (sz) {
			printf(", argument: size=%zd data=", sz);
			if (fwrite(val, 1, sz, stdout) != sz)
				goto done;
		}
		printf("\n");
		return 0;
	} else if (rc == 1)
		printf("%s: not found\n", name);
	else
		printf("parse error: %s\n", optstr);
done:
	return -1;
}

int test_remove(struct mtest *ts, int argc, char *argv[])
{
	const char *name;
	char *optstr;

	if (argc < 3)
		goto done;
	optstr = strdup(argv[1]);
	name = argv[2];

	if (mnt_optstr_remove_option(&optstr, name) == 0) {
		printf("result: >%s<\n", optstr);
		return 0;
	}
done:
	return -1;
}


int main(int argc, char *argv[])
{
	struct mtest tss[] = {
		{ "--append", test_append, "<optstr> <name> [<value>]  append value to optstr" },
		{ "--set",    test_set,    "<optstr> <name> [<value>]  (un)set value" },
		{ "--get",    test_get,    "<optstr> <name>            search name in optstr" },
		{ "--remove", test_remove, "<optstr> <name>            remove name in optstr" },
		{ NULL }
	};
	return  mnt_run_test(tss, argc, argv);
}
#endif /* TEST_PROGRAM */
