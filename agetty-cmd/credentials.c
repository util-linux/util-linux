/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "agetty.h"
#include "env.h"
#include "fileutils.h"
#include "nls.h"
#include "path.h"
#include "strutils.h"

static int cred_read_str(struct path_cxt *pc, const char *name,
			 struct agetty_options *op, size_t offset)
{
	char *str = NULL, **dest;

	if (ul_path_read_string(pc, &str, name) < 0)
		return -1;

	dest = (char **) ((char *) op + offset);
	free(*dest);
	*dest = str;
	return 0;
}

static int cred_read_num(struct path_cxt *pc, const char *name,
			 struct agetty_options *op, size_t offset, int type)
{
	char *str = NULL;
	int rc;

	if (ul_path_read_string(pc, &str, name) < 0)
		return -1;

	switch (type) {
	case 'u':
	{
		uint32_t num;
		rc = ul_strtou32(str, &num, 10);
		if (rc == 0)
			*((unsigned int *) ((char *) op + offset)) = num;
		break;
	}
	case 'd':
	{
		int32_t num;
		rc = ul_strtos32(str, &num, 10);
		if (rc == 0)
			*((int *) ((char *) op + offset)) = num;
		break;
	}
	default:
		rc = -EINVAL;
		break;
	}

	if (rc)
		agetty_log_warn(_("invalid '%s' credential value"), name);
	free(str);
	return rc;
}

static int cred_read_bool(struct path_cxt *pc, const char *name,
			  int *flags, int flag, int invert)
{
	char *str = NULL;
	bool res;
	int rc;

	if (ul_path_read_string(pc, &str, name) < 0)
		return -1;

	rc = ul_strtobool(str, &res);
	if (rc)
		agetty_log_warn(_("invalid '%s' credential value"), name);
	else if (res != invert)
		*flags |= flag;
	else
		*flags &= ~flag;

	free(str);
	return rc;
}

void agetty_load_credentials(struct agetty_options *op)
{
	char *env;
	DIR *dir;
	struct dirent *d;
	struct path_cxt *pc;

	env = safe_getenv("CREDENTIALS_DIRECTORY");
	if (!env)
		return;

	pc = ul_new_path("%s", env);
	if (!pc) {
		agetty_log_warn(_("failed to initialize path context"));
		return;
	}

	dir = ul_path_opendir(pc, NULL);
	if (!dir) {
		agetty_log_warn(_("failed to open credentials directory"));
		return;
	}

	while ((d = xreaddir(dir))) {
		if (strcmp(d->d_name, "agetty.autologin") == 0)
			cred_read_str(pc, d->d_name, op,
				      offsetof(struct agetty_options, autolog));
		else if (strcmp(d->d_name, "agetty.delay") == 0)
			cred_read_num(pc, d->d_name, op,
				      offsetof(struct agetty_options, delay), 'u');
		else if (strcmp(d->d_name, "agetty.nice") == 0)
			cred_read_num(pc, d->d_name, op,
				      offsetof(struct agetty_options, nice), 'd');
		else if (strcmp(d->d_name, "agetty.hangup") == 0)
			cred_read_bool(pc, d->d_name,
				       &op->flags, F_HANGUP, 0);
		else if (strcmp(d->d_name, "agetty.noclear") == 0)
			cred_read_bool(pc, d->d_name,
				       &op->flags, F_NOCLEAR, 0);
		else if (strcmp(d->d_name, "agetty.nohints") == 0)
			cred_read_bool(pc, d->d_name,
				       &op->flags, F_NOHINTS, 0);
		else if (strcmp(d->d_name, "agetty.nohostname") == 0)
			cred_read_bool(pc, d->d_name,
				       &op->flags, F_NOHOSTNAME, 0);
		else if (strcmp(d->d_name, "agetty.noissue") == 0)
			cred_read_bool(pc, d->d_name,
				       &op->flags, F_ISSUE, 1);
	}
	closedir(dir);
}
