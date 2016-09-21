#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libmount.h>
#include <blkid.h>

#include "nls.h"
#include "c.h"
#include "strutils.h"
#include "xalloc.h"

#include "findmnt.h"

struct verify_context {
	struct libmnt_fs	*fs;
	struct libmnt_table	*tb;

	int	nwarnings;
	int	nerrors;

	unsigned int	target_printed : 1;
};

static void verify_mesg(struct verify_context *vfy, char type, const char *fmt, va_list ap)
{
	if (!vfy->target_printed) {
		fprintf(stdout, "%s\n", mnt_fs_get_target(vfy->fs));
		vfy->target_printed = 1;
	}

	fprintf(stdout, "   [%c] ", type);
	vfprintf(stdout, fmt, ap);
	fputc('\n', stdout);
}

static int verify_warn(struct verify_context *vfy, const char *fmt, ...)
{
	va_list ap;
	vfy->nwarnings++;
	va_start(ap, fmt);
	verify_mesg(vfy, 'W', fmt, ap);
	va_end(ap);
	return 0;
}

static int verify_err(struct verify_context *vfy, const char *fmt, ...)
{
	va_list ap;
	vfy->nerrors++;
	va_start(ap, fmt);
	verify_mesg(vfy, 'E', fmt, ap);
	va_end(ap);
	return 0;
}

static int verify_ok(struct verify_context *vfy __attribute__((unused)),
		      const char *fmt, ...)
{
	va_list ap;

	if (!(flags & FL_VERBOSE))
		return 0;

	va_start(ap, fmt);
	verify_mesg(vfy, ' ', fmt, ap);
	va_end(ap);
	return 0;
}

static int verify_order(struct verify_context *vfy)
{
	struct libmnt_iter *itr = NULL;
	struct libmnt_fs *next;
	const char *tgt;

	tgt = mnt_fs_get_target(vfy->fs);
	if (tgt && !(flags & FL_NOCACHE))
		tgt  = mnt_resolve_target(tgt, cache);
	else if (!tgt)
		return 0;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		goto done;
	}

	/* set iterator position to 'fs' */
	mnt_table_set_iter(vfy->tb, itr, vfy->fs);
	mnt_table_next_fs(vfy->tb, itr, &next);

	/* scan all next filesystems */
	while (mnt_table_next_fs(vfy->tb, itr, &next) == 0) {
		const char *n_tgt;
		size_t len;

		n_tgt = mnt_fs_get_target(next);
		if (n_tgt && !(flags & FL_NOCACHE))
			n_tgt  = mnt_resolve_target(n_tgt, cache);
		else if (!n_tgt)
			continue;
		len = strlen(n_tgt);

		if (strncmp(n_tgt, tgt, len) == 0) {
			if (*(tgt + len) == '\0')
				verify_warn(vfy, _("target specified more than once"));
			else if (*(tgt + len) == '/')
				verify_err(vfy, _("wrong order: %s specified before %s"), tgt, n_tgt);
		}
	}
done:
	mnt_free_iter(itr);
	return 0;
}

static int verify_target(struct verify_context *vfy)
{
	const char *tgt = mnt_fs_get_target(vfy->fs);
	const char *cn = tgt;
	struct stat sb;

	if (!tgt)
		return verify_err(vfy, _("undefined target (fs_file)"));

	if (!(flags & FL_NOCACHE)) {
		cn = mnt_resolve_target(tgt, cache);
		if (!cn)
			return -ENOMEM;
		if (strcmp(cn, tgt) != 0)
			verify_warn(vfy, _("non-canonical target path (real: %s)"), cn);
		tgt = cn;
	}
	if (stat(tgt, &sb) != 0) {
		if (mnt_fs_get_option(vfy->fs, "noauto", NULL, NULL) == 1)
			verify_err(vfy, _("unreachable on boot required target: %m"));
		else
			verify_warn(vfy, _("unreachable target: %m"));

	} else if (!S_ISDIR(sb.st_mode)
		 && mnt_fs_get_option(vfy->fs, "bind", NULL, NULL) == 1) {
		verify_err(vfy, _("target is not a directory"));
	} else
		verify_ok(vfy, _("target exists"));

	return 0;
}

static char *verify_tag(struct verify_context *vfy, const char *name,
		      const char *value)
{
	char *src = mnt_resolve_tag(name, value, cache);

	if (!src) {
		if (mnt_fs_get_option(vfy->fs, "noauto", NULL, NULL) == 1)
			verify_err(vfy, _("unreachable on boot required source: %s=%s"), name, value);
		else
			verify_warn(vfy, _("unreachable: %s=%s"), name, value);
	} else
		verify_ok(vfy, _("%s=%s translated to %s"), name, value, src);

	return src;
}

/* Note that mount source is very FS specific and we should not
 * interpret unreachable source as error. The exception is only
 * NAME=value, this has to be convertible to device name.
 */
static int verify_source(struct verify_context *vfy)
{
	const char *src = mnt_fs_get_srcpath(vfy->fs);
	char *t = NULL, *v = NULL;
	struct stat sb;
	int isbind;

	/* source is NAME=value tag */
	if (!src) {
		const char *tag = NULL, *val = NULL;

		if (mnt_fs_get_tag(vfy->fs, &tag, &val) != 0)
			return verify_err(vfy, _("undefined source (fs_spec)"));

		src = verify_tag(vfy, tag, val);
		if (!src)
			goto done;

	/* blkid is able to parse it, but libmount does not see it as a tag --
	 * it means unsupported tag */
	} else if (blkid_parse_tag_string(src, &t, &v) == 0 && stat(src, &sb) != 0)
		return verify_err(vfy, _("unsupported source tag: %s"), src);

	isbind = mnt_fs_get_option(vfy->fs, "bind", NULL, NULL) == 0;

	/* source is path */
	if (mnt_fs_is_pseudofs(vfy->fs) || mnt_fs_is_netfs(vfy->fs))
		verify_ok(vfy, _("do not check %s (pseudo/net filesystem)"), src);

	else if (stat(src, &sb) != 0)
		verify_warn(vfy, _("unreachable source: %s: %m"), src);

	else if ((S_ISDIR(sb.st_mode) || S_ISREG(sb.st_mode)) && !isbind)
		verify_warn(vfy, _("non-bind mount source %s is a directory or regular file"), src);

	else if (!S_ISBLK(sb.st_mode) && !isbind)
		verify_warn(vfy, _("source %s is not a block device"), src);
	else
		verify_ok(vfy, _("source %s exists"), src);
done:
	free(t);
	free(v);
	return 0;
}

static int verify_filesystem(struct verify_context *vfy)
{
	int rc = 0;

	if (!mnt_fs_is_swaparea(vfy->fs))
		rc = verify_target(vfy);
	if (!rc)
		verify_source(vfy);

	return rc;
}

int verify_table(struct libmnt_table *tb)
{
	struct verify_context vfy = { .nerrors = 0 };
	struct libmnt_iter *itr = NULL;
	int rc = 0;		/* overall return code (alloc errors, etc.) */
	int check_order = is_listall_mode();

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		goto done;
	}

	vfy.tb = tb;

	while (rc == 0 && (vfy.fs = get_next_fs(tb, itr))) {
		vfy.target_printed = 0;
		if (check_order)
			rc = verify_order(&vfy);
		if (!rc)
			rc = verify_filesystem(&vfy);

		if (flags & FL_FIRSTONLY)
			break;
		flags |= FL_NOSWAPMATCH;
	}

done:
	mnt_free_iter(itr);

	/* summary */
	if (vfy.nerrors || parse_nerrors || vfy.nwarnings) {
		fputc('\n', stderr);
		fprintf(stderr, P_("%d parse error", "%d parse errors", parse_nerrors), parse_nerrors);
		fprintf(stderr, P_(", %d error",     ", %d errors", vfy.nerrors), vfy.nerrors);
		fprintf(stderr, P_(", %d warning",   ", %d warnings", vfy.nwarnings), vfy.nwarnings);
		fputc('\n', stderr);
	} else
		fprintf(stdout, _("\nSuccess, no errors or warnings detected\n"));

	return rc != 0 ? rc : vfy.nerrors + parse_nerrors;
}
