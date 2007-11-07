#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "mount_paths.h"
#include "fsprobe.h"
#include "sundries.h"		/* for xstrdup */
#include "nls.h"
#include "realpath.h"

/* list of already tested filesystems by fsprobe_procfsloop_mount() */
static struct tried {
	struct tried *next;
	char *type;
} *tried = NULL;

static int
was_tested(const char *fstype) {
	struct tried *t;

	if (fsprobe_known_fstype(fstype))
		return 1;
	for (t = tried; t; t = t->next) {
		if (!strcmp(t->type, fstype))
			return 1;
	}
	return 0;
}

static void
set_tested(const char *fstype) {
	struct tried *t = xmalloc(sizeof(struct tried));

	t->next = tried;
	t->type = xstrdup(fstype);
	tried = t;
}

static void
free_tested(void) {
	struct tried *t, *tt;

	t = tried;
	while(t) {
		free(t->type);
		tt = t->next;
		free(t);
		t = tt;
	}
	tried = NULL;
}

static char *
procfsnext(FILE *procfs) {
   char line[100];
   char fsname[100];

   while (fgets(line, sizeof(line), procfs)) {
      if (sscanf (line, "nodev %[^\n]\n", fsname) == 1) continue;
      if (sscanf (line, " %[^ \n]\n", fsname) != 1) continue;
      return xstrdup(fsname);
   }
   return 0;
}

/* Only use /proc/filesystems here, this is meant to test what
   the kernel knows about, so /etc/filesystems is irrelevant.
   Return: 1: yes, 0: no, -1: cannot open procfs */
int
fsprobe_known_fstype_in_procfs(const char *type)
{
    FILE *procfs;
    char *fsname;
    int ret = -1;

    procfs = fopen(PROC_FILESYSTEMS, "r");
    if (procfs) {
	ret = 0;
	while ((fsname = procfsnext(procfs)) != NULL)
	    if (!strcmp(fsname, type)) {
		ret = 1;
		break;
	    }
	fclose(procfs);
	procfs = NULL;
    }
    return ret;
}

/* Try all types in FILESYSTEMS, except those in *types,
   in case *types starts with "no" */
/* return: 0: OK, -1: error in errno, 1: type not found */
/* when 0 or -1 is returned, *types contains the type used */
/* when 1 is returned, *types is NULL */
int
fsprobe_procfsloop_mount(int (*mount_fn)(struct mountargs *, int *, int *),
			 struct mountargs *args,
			 const char **types,
			 int *special, int *status)
{
	char *files[2] = { ETC_FILESYSTEMS, PROC_FILESYSTEMS };
	FILE *procfs;
	char *fsname;
	const char *notypes = NULL;
	int no = 0;
	int ret = 1;
	int errsv = 0;
	int i;

	if (*types && !strncmp(*types, "no", 2)) {
		no = 1;
		notypes = (*types) + 2;
	}
	*types = NULL;

	/* Use PROC_FILESYSTEMS only when ETC_FILESYSTEMS does not exist.
	   In some cases trying a filesystem that the kernel knows about
	   on the wrong data will crash the kernel; in such cases
	   ETC_FILESYSTEMS can be used to list the filesystems that we
	   are allowed to try, and in the order they should be tried.
	   End ETC_FILESYSTEMS with a line containing a single '*' only,
	   if PROC_FILESYSTEMS should be tried afterwards. */

	for (i=0; i<2; i++) {
		procfs = fopen(files[i], "r");
		if (!procfs)
			continue;
		while ((fsname = procfsnext(procfs)) != NULL) {
			if (!strcmp(fsname, "*")) {
				fclose(procfs);
				goto nexti;
			}
			if (was_tested (fsname))
				continue;
			if (no && matching_type(fsname, notypes))
				continue;
			set_tested (fsname);
			args->type = fsname;
			if (verbose)
				printf(_("Trying %s\n"), fsname);
			if ((*mount_fn) (args, special, status) == 0) {
				*types = fsname;
				ret = 0;
				break;
			} else if (errno != EINVAL &&
				   fsprobe_known_fstype_in_procfs(fsname) == 1) {
				*types = "guess";
				ret = -1;
				errsv = errno;
				break;
			}
		}
		free_tested();
		fclose(procfs);
		errno = errsv;
		return ret;
	nexti:;
	}
	return 1;
}

const char *
fsprobe_get_devname_for_mounting(const char *spec)
{
	char *name, *value;

	if (!spec)
		return NULL;

	if (parse_spec(spec, &name, &value) != 0)
		return NULL;				/* parse error */

	if (name) {
		const char *nspec = NULL;

		if (!strcmp(name,"LABEL"))
			nspec = fsprobe_get_devname_by_label(value);
		else if (!strcmp(name,"UUID"))
			nspec = fsprobe_get_devname_by_uuid(value);

		if (nspec && verbose > 1)
			printf(_("mount: going to mount %s by %s\n"), spec, name);

		free((void *) name);
		return nspec;
	}

	/* no LABEL, no UUID, .. probably a path */
	if (verbose > 1)
		printf(_("mount: no LABEL=, no UUID=, going to mount %s by path\n"), spec);

	return canonicalize(spec);
}

/* like fsprobe_get_devname_for_mounting(), but without verbose messages */
const char *
fsprobe_get_devname(const char *spec)
{
	char *name, *value;

	if (!spec)
		return NULL;

	if (parse_spec(spec, &name, &value) != 0)
		return NULL;				/* parse error */

	if (name) {
		const char *nspec = NULL;

		if (!strcmp(name,"LABEL"))
			nspec = fsprobe_get_devname_by_label(value);
		else if (!strcmp(name,"UUID"))
			nspec = fsprobe_get_devname_by_uuid(value);

		free((void *) name);
		return nspec;
	}

	return canonicalize(spec);
}

