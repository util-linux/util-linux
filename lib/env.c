/*
 * environ[] array cleanup code and getenv() wrappers
 *
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#else
#define PR_GET_DUMPABLE 3
#endif
#if (!defined(HAVE_PRCTL) && defined(linux))
#include <sys/syscall.h>
#endif
#include <unistd.h>
#include <sys/types.h>

#include "env.h"
#include "strv.h"
#include "all-io.h"

#ifndef HAVE_ENVIRON_DECL
extern char **environ;
#endif

static char * const forbid[] = {
	"BASH_ENV=",    /* GNU creeping featurism strikes again... */
	"ENV=",
	"HOME=",
	"IFS=",
	"KRB_CONF=",
	"LD_",	  /* anything with the LD_ prefix */
	"LIBPATH=",
	"MAIL=",
	"NLSPATH=",
	"PATH=",
	"SHELL=",
	"SHLIB_PATH=",
	(char *) 0
};

/* these are allowed, but with no slashes inside
   (to work around security problems in GNU gettext) */
static char * const noslash[] = {
	"LANG=",
	"LANGUAGE=",
	"LC_",	  /* anything with the LC_ prefix */
	(char *) 0
};


struct ul_env_list {
	char	*name;
	char	*value;

	struct ul_env_list *next;
};

/*
 * Saves to the list and returns a pointer to the new head of the list.
 */
static struct ul_env_list *env_list_add( struct ul_env_list *ls0,
					 const char *name, size_t namesz,
					 const char *value, size_t valsz)
{
	struct ul_env_list *ls;

	ls = calloc(1, sizeof(struct ul_env_list) + namesz + valsz + 2);
	if (!ls)
		return ls0;

	ls->name = ((char *) ls) + sizeof(struct ul_env_list);
	ls->value = ls->name + namesz + 1;

	memcpy(ls->name, name, namesz);
	memcpy(ls->value, value, valsz);

	ls->next = ls0;
	return ls;
}

/*
 * Saves the @str (with the name=value string) to the @ls and returns a pointer
 * to the new head of the list.
 */
static struct ul_env_list *env_list_add_from_string(struct ul_env_list *ls,
						    const char *str)
{
	size_t namesz = 0, valsz = 0;
	const char *val;

	if (!str || !*str)
		return ls;

	val = strchr(str, '=');
	if (!val)
		return ls;
	namesz = val - str;

	val++;
	valsz = strlen(val);

	return env_list_add(ls, str, namesz, val, valsz);
}

/*
 * Saves the @name and @value to @ls0 and returns a pointer to the new head of
 * the list.
 */
struct ul_env_list *env_list_add_variable(
				struct ul_env_list *ls,
				const char *name, const char *value)
{
	if (!name || !*name)
		return ls;

	return env_list_add(ls, name, strlen(name), value, value ? strlen(value) : 0);
}

/*
 * Calls getenv() and adds the result to the list.
 */
struct ul_env_list *env_list_add_getenv(struct ul_env_list *ls,
				const char *name, const char *dflt)
{
	const char *val;

	if (!name)
		return ls;

	val = getenv(name);
	if (!val)
		val= dflt;
	if (val)
		ls = env_list_add_variable(ls, name, val);
	return ls;
}

/*
 * Calls getenv() for each name (comma-separated) in @str and adds the results
 * to the list.
 */
struct ul_env_list *env_list_add_getenvs(struct ul_env_list *ls, const char *str)
{
	char **all, **name;

	if (!str)
		return ls;

	all = ul_strv_split(str, ",");
	if (!all)
		return ls;

	UL_STRV_FOREACH(name, all)
		ls = env_list_add_getenv(ls, *name, NULL);

	ul_strv_free(all);
	return ls;
}

/*
 * Use env_list_from_fd() to read environment from @fd.
 *
 * @fd must be /proc/<pid>/environ file.
*/
struct ul_env_list *env_list_from_fd(int fd)
{
	char *buf = NULL, *p, *end;
	ssize_t rc = 0;
	struct ul_env_list *ls = NULL;

	errno = 0;
	if ((rc = ul_read_all_alloc(fd, &buf)) < 1)
		return NULL;
	buf[rc] = '\0';
	end = buf + rc;

	for (p = buf; p < end; p += strlen(p) + 1)
		ls = env_list_add_from_string(ls, p);

	free(buf);
	return ls;
}

/*
 * Use setenv() for all stuff in @ls.
 */
int env_list_setenv(struct ul_env_list *ls, int overwrite)
{
	int rc = 0;

	while (ls && rc == 0) {
		if (ls->name && ls->value)
			rc = setenv(ls->name, ls->value, overwrite);
		ls = ls->next;
	}
	return rc;
}

void env_list_free(struct ul_env_list *ls)
{
	while (ls) {
		struct ul_env_list *x = ls;
		ls = ls->next;
		free(x);
	}
}

/*
 * Removes unwanted variables from environ[]. If @org is not NULL than stores
 * unwnated variables to the list.
 */
void __sanitize_env(struct ul_env_list **org)
{
	char **envp = environ;
	char * const *bad;
	char **cur;
	int last = 0;

	for (cur = envp; *cur; cur++)
		last++;

	for (cur = envp; *cur; cur++) {
		for (bad = forbid; *bad; bad++) {
			if (strncmp(*cur, *bad, strlen(*bad)) == 0) {
				if (org)
					*org = env_list_add_from_string(*org, *cur);
				last = ul_remove_entry(envp, cur - envp, last);
				cur--;
				break;
			}
		}
	}

	for (cur = envp; *cur; cur++) {
		for (bad = noslash; *bad; bad++) {
			if (strncmp(*cur, *bad, strlen(*bad)) != 0)
				continue;
			if (!strchr(*cur, '/'))
				continue;  /* OK */
			if (org)
				*org = env_list_add_from_string(*org, *cur);
			last = ul_remove_entry(envp, cur - envp, last);
			cur--;
			break;
		}
	}
}

void sanitize_env(void)
{
	__sanitize_env(NULL);
}

char *safe_getenv(const char *arg)
{
	if (is_privileged_execution())
		return NULL;
#ifdef HAVE_PRCTL
	if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#else
#if (defined(linux) && defined(SYS_prctl))
	if (syscall(SYS_prctl, PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#endif
#endif
#ifdef HAVE_SECURE_GETENV
return secure_getenv(arg);
#elif HAVE___SECURE_GETENV
	return __secure_getenv(arg);
#else
	return getenv(arg);
#endif
}

#ifdef TEST_PROGRAM_ENV
/*
 * Sizes of name=value strings (without the terminating zero) for
 * test_from_fd(); the totals are around the read buffer size.
 */
static const size_t from_fd_small[] = { 3, 7, 5 };
static const size_t from_fd_1k[] = { 600, 421 };
static const size_t from_fd_4k[] = { 3000, 1093 };
static const size_t from_fd_over_1k[] = { 4, 900, 118 };

/*
 * Read @n variables of the given sizes from a pipe by env_list_from_fd() and
 * check that every variable comes back exactly once and nothing else does.
 */
static int test_from_fd(const size_t *sizes, size_t n)
{
	struct ul_env_list *ls, *x;
	size_t i, total = 0, count = 0;
	char *buf, *p, *seen;
	int fds[2], rc = 0;

	for (i = 0; i < n; i++)
		total += sizes[i] + 1;

	p = buf = malloc(total);
	seen = calloc(n, 1);
	if (!buf || !seen || pipe(fds) != 0)
		err(EXIT_FAILURE, "test setup failed");

	for (i = 0; i < n; i++) {
		int len = sprintf(p, "V%zu=", i);

		memset(p + len, 'x', sizes[i] - len);
		p += sizes[i];
		*p++ = '\0';
	}
	if (ul_write_all(fds[1], buf, total) != 0)
		err(EXIT_FAILURE, "test setup failed");
	close(fds[1]);

	ls = env_list_from_fd(fds[0]);
	close(fds[0]);

	for (x = ls; x; x = x->next) {
		size_t idx;

		count++;
		if (sscanf(x->name, "V%zu", &idx) != 1 || idx >= n || seen[idx]
		    || strlen(x->name) + 1 + strlen(x->value) != sizes[idx]) {
			warnx("from_fd: unexpected variable '%s'", x->name);
			rc = 1;
			continue;
		}
		seen[idx] = 1;
	}
	if (count != n) {
		warnx("from_fd: got %zu variables, expected %zu", count, n);
		rc = 1;
	}

	env_list_free(ls);
	free(seen);
	free(buf);
	return rc;
}

int main(void)
{
	char *const *bad;
	char copy[32];
	char *p;
	int retval = EXIT_SUCCESS;
	struct ul_env_list *removed = NULL;

	/* define env. */
	for (bad = forbid; *bad; bad++) {
		strcpy(copy, *bad);
		p = strchr(copy, '=');
		if (p)
			*p = '\0';
		setenv(copy, "abc", 1);
	}

	/* removed */
	__sanitize_env(&removed);

	/* check removal */
	for (bad = forbid; *bad; bad++) {
		strcpy(copy, *bad);
		p = strchr(copy, '=');
		if (p)
			*p = '\0';
		p = getenv(copy);
		if (p) {
			warnx("%s was not removed", copy);
			retval = EXIT_FAILURE;
		}
	}

	/* restore removed */
	env_list_setenv(removed, 0);

	/* check restore */
	for (bad = forbid; *bad; bad++) {
		strcpy(copy, *bad);
		p = strchr(copy, '=');
		if (p)
			*p = '\0';
		p = getenv(copy);
		if (!p) {
			warnx("%s was not restored", copy);
			retval = EXIT_FAILURE;
		}
	}

	env_list_free(removed);

	/* read environment from fd */
	if (test_from_fd(from_fd_small, ARRAY_SIZE(from_fd_small))
	    || test_from_fd(from_fd_1k, ARRAY_SIZE(from_fd_1k))
	    || test_from_fd(from_fd_4k, ARRAY_SIZE(from_fd_4k))
	    || test_from_fd(from_fd_over_1k, ARRAY_SIZE(from_fd_over_1k)))
		retval = EXIT_FAILURE;

	return retval;
}
#endif
