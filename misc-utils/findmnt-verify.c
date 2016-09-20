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
	if (!vfy->target_printed)
		fprintf(stdout, "%s\n", mnt_fs_get_target(vfy->fs));

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
	verify_mesg(vfy, '+', fmt, ap);
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
		return verify_err(vfy, _("undefined target (mountpoint)"));

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
			verify_err(vfy, _("on boot required target unaccessible: %m"));
		else
			verify_warn(vfy, _("target unaccessible: %m"));

	} else if (!S_ISDIR(sb.st_mode)
		 && mnt_fs_get_option(vfy->fs, "bind", NULL, NULL) == 1) {
		verify_err(vfy, _("target is not a directory"));
	} else
		verify_ok(vfy, _("target accessible"));

	return 0;
}

static int verify_filesystem(struct verify_context *vfy)
{
	int rc = 0;

	if (!mnt_fs_is_swaparea(vfy->fs))
		rc = verify_target(vfy);
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
