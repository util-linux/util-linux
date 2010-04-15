/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: optmap
 * @title: Option maps
 * @short_description: description for mount options
 *
 * The mount(2) linux syscall uses two arguments for mount options:
 *
 *	@mountflags: (see MS_* macros in linux/fs.h)
 *
 *	@mountdata: (usully a comma separated string of options)
 *
 * The libmount uses options-map(s) to describe mount options. The number of
 * maps is unlimited. The libmount options parser could be easily extended
 * (e.g. by mnt_optls_add_map()) to work with new options.
 *
 * The option description (map entry) includes:
 *
 *	@name: and argument type (e.g. "loop[=%s]")
 *
 *	@id: (in the map unique identifier or a mountflags, e.g MS_RDONLY)
 *
 *	@mask: (MNT_INVERT, MNT_MDATA, MNT_MFLAG, MNT_NOMTAB)
 *
 * The option argument type is defined by:
 *
 *	"=type"   -- required argument
 *
 *	"[=type]" -- optional argument
 *
 * where the 'type' is sscanf() format string or
 *
 *     {item0,item1,...}  -- enum (mnt_option_get_number() converts the value
 *                           to 0..N number)
 *
 * The options argument format is used for parsing only. The library internally
 * stores the option argument as a string. The conversion to the data type is
 * on-demant by mnt_option_get_value_*() functions.
 *
 * The library checks options argument according to 'type' format for simple
 * formats only:
 *
 *	%s, %d, %ld, %lld, %u, %lu, %llu, %x, %o and {enum}
 *
 * Example:
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
 *   </programlisting>
 * </informalexample>
 *
 * The libmount defines two basic built-in options maps:
 *
 *	@MNT_LINUX_MAP: fs-independent kernel mount options (usually MS_* flags)
 *
 *	@MNT_USERSPACE_MAP: userspace specific mount options (e.g. "user", "loop")
 *
 * For more details about option map struct see "struct mnt_optmap" in
 * mount/mount.h.
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "nls.h"
#include "mountP.h"

/*
 * fs-independent mount flags (built-in MNT_LINUX_MAP)
 */
static const struct mnt_optmap linux_flags_map[] =
{
   { "ro",       MS_RDONLY, MNT_MFLAG },               /* read-only */
   { "rw",       MS_RDONLY, MNT_MFLAG | MNT_INVERT },  /* read-write */
   { "exec",     MS_NOEXEC, MNT_MFLAG | MNT_INVERT },  /* permit execution of binaries */
   { "noexec",   MS_NOEXEC, MNT_MFLAG },               /* don't execute binaries */
   { "suid",     MS_NOSUID, MNT_MFLAG | MNT_INVERT },  /* honor suid executables */
   { "nosuid",   MS_NOSUID, MNT_MFLAG },               /* don't honor suid executables */
   { "dev",      MS_NODEV,  MNT_MFLAG | MNT_INVERT },  /* interpret device files  */
   { "nodev",    MS_NODEV,  MNT_MFLAG },               /* don't interpret devices */

   { "sync",     MS_SYNCHRONOUS, MNT_MFLAG },          /* synchronous I/O */
   { "async",    MS_SYNCHRONOUS, MNT_MFLAG | MNT_INVERT }, /* asynchronous I/O */

   { "dirsync",  MS_DIRSYNC, MNT_MFLAG },              /* synchronous directory modifications */
   { "remount",  MS_REMOUNT, MNT_MFLAG },              /* Alter flags of mounted FS */
   { "bind",     MS_BIND,    MNT_MFLAG },              /* Remount part of tree elsewhere */
   { "rbind",    MS_BIND|MS_REC, MNT_MFLAG },          /* Idem, plus mounted subtrees */
#ifdef MS_NOSUB
   { "sub",      MS_NOSUB,  MNT_MFLAG | MNT_INVERT },  /* allow submounts */
   { "nosub",    MS_NOSUB,  MNT_MFLAG },               /* don't allow submounts */
#endif
#ifdef MS_SILENT
   { "quiet",	 MS_SILENT, MNT_MFLAG },               /* be quiet  */
   { "loud",     MS_SILENT, MNT_MFLAG | MNT_INVERT },  /* print out messages. */
#endif
#ifdef MS_MANDLOCK
   { "mand",     MS_MANDLOCK, MNT_MFLAG },             /* Allow mandatory locks on this FS */
   { "nomand",   MS_MANDLOCK, MNT_MFLAG | MNT_INVERT },/* Forbid mandatory locks on this FS */
#endif
#ifdef MS_NOATIME
   { "atime",    MS_NOATIME, MNT_MFLAG | MNT_INVERT }, /* Update access time */
   { "noatime",	 MS_NOATIME, MNT_MFLAG },              /* Do not update access time */
#endif
#ifdef MS_I_VERSION
   { "iversion", MS_I_VERSION,   MNT_MFLAG },          /* Update inode I_version time */
   { "noiversion", MS_I_VERSION, MNT_MFLAG | MNT_INVERT}, /* Don't update inode I_version time */
#endif
#ifdef MS_NODIRATIME
   { "diratime", MS_NODIRATIME,   MNT_MFLAG | MNT_INVERT }, /* Update dir access times */
   { "nodiratime", MS_NODIRATIME, MNT_MFLAG },         /* Do not update dir access times */
#endif
#ifdef MS_RELATIME
   { "relatime", MS_RELATIME,   MNT_MFLAG },           /* Update access times relative to mtime/ctime */
   { "norelatime", MS_RELATIME, MNT_MFLAG | MNT_INVERT }, /* Update access time without regard to mtime/ctime */
#endif
#ifdef MS_STRICTATIME
   { "strictatime", MS_STRICTATIME, MNT_MFLAG },       /* Strict atime semantics */
   { "nostrictatime", MS_STRICTATIME, MNT_MFLAG | MNT_INVERT }, /* kernel default atime */
#endif
   { NULL, 0, 0 }
};

/*
 * userspace mount option (built-in MNT_USERSPACE_MAP)
 */
static const struct mnt_optmap userspace_opts_map[] =
{
   { "defaults", MNT_MS_DFLTS, MNT_NOMTAB },               /* default options */

   { "auto",    MNT_MS_NOAUTO, MNT_INVERT | MNT_NOMTAB },  /* Can be mounted using -a */
   { "noauto",  MNT_MS_NOAUTO, MNT_NOMTAB },               /* Can  only be mounted explicitly */

   { "user[=%s]", MNT_MS_USER },                           /* Allow ordinary user to mount (mtab) */
   { "nouser",  MNT_MS_USER, MNT_INVERT | MNT_NOMTAB },    /* Forbid ordinary user to mount */

   { "users",   MNT_MS_USERS, MNT_NOMTAB },                /* Allow ordinary users to mount */
   { "nousers", MNT_MS_USERS, MNT_INVERT | MNT_NOMTAB },   /* Forbid ordinary users to mount */

   { "owner",   MNT_MS_OWNER, MNT_NOMTAB },                /* Let the owner of the device mount */
   { "noowner", MNT_MS_OWNER, MNT_INVERT | MNT_NOMTAB },   /* Device owner has no special privs */

   { "group",   MNT_MS_GROUP, MNT_NOMTAB },                /* Let the group of the device mount */
   { "nogroup", MNT_MS_GROUP, MNT_INVERT | MNT_NOMTAB },   /* Device group has no special privs */

   { "_netdev", MNT_MS_NETDEV },                           /* Device requires network */

   { "comment=%s", MNT_MS_COMMENT, MNT_NOMTAB },           /* fstab comment only */

   { "loop[=%s]", MNT_MS_LOOP },                           /* use the loop device */

   { "nofail",  MNT_MS_NOFAIL, MNT_NOMTAB },               /* Do not fail if ENOENT on dev */

   { NULL, 0, 0 }
};

/**
 * mnt_get_builtin_map:
 * @id: map id -- MNT_LINUX_MAP or MNT_USERSPACE_MAP
 *
 * MNT_LINUX_MAP - Linux kernel fs-independent mount options
 *                 (usually MS_* flags, see linux/fs.h)
 *
 * MNT_USERSPACE_MAP - userpace mount(8) specific mount options
 *                     (e.g user=, _netdev, ...)
 *
 * Returns: static built-in libmount map.
 */
const struct mnt_optmap *mnt_get_builtin_optmap(int id)
{
	assert(id);

	if (id == MNT_LINUX_MAP)
		return linux_flags_map;
	else if (id == MNT_USERSPACE_MAP)
		return userspace_opts_map;
	return NULL;
}

/*
 * Lookups for the @name in @maps and returns a map and in @mapent
 * returns the map entry
 */
const struct mnt_optmap *mnt_optmap_get_entry(
				struct mnt_optmap const **maps,
				int nmaps,
				const char *name,
				size_t namelen,
				const struct mnt_optmap **mapent)
{
	int i;

	assert(maps);
	assert(nmaps);
	assert(name);
	assert(namelen);
	assert(mapent);

	*mapent = NULL;

	for (i = 0; i < nmaps; i++) {
		const struct mnt_optmap *map = maps[i];
		const struct mnt_optmap *ent;
		const char *p;

		for (ent = map; ent && ent->name; ent++) {
			if (strncmp(ent->name, name, namelen))
				continue;
			p = ent->name + namelen;
			if (*p == '\0' || *p == '=' || *p == '[') {
				*mapent = ent;
				return map;
			}
		}
	}
	return NULL;
}


/*
 * Converts @rawdata to number according to enum definition in the @mapent.
 */
int mnt_optmap_enum_to_number(const struct mnt_optmap *mapent,
			const char *rawdata, size_t len)
{
	const char *p, *end = NULL, *begin = NULL;
	int n = -1;

	if (!rawdata || !*rawdata || !mapent || !len)
		return -1;

	p = strrchr(mapent->name, '=');
	if (!p || *(p + 1) == '{')
		return -1;	/* value unexpected or not "enum" */
	p += 2;
	if (!*p || *(p + 1) == '}')
		return -1;	/* hmm... option <type> is "={" or "={}" */

	/* we cannot use strstr(), @rawdata is not terminated */
	for (; p && *p; p++) {
		if (!begin)
			begin = p;		/* begin of the item */
		if (*p == ',')
			end = p;		/* terminate the item */
		if (*(p + 1) == '}')
			end = p + 1;		/* end of enum definition */
		if (!begin || !end)
			continue;
		if (end <= begin)
			return -1;
		n++;
		if (len == end - begin && strncasecmp(begin, rawdata, len) == 0)
			return n;
		p = end;
	}

	return -1;
}

/*
 * Returns data type defined in the @mapent.
 */
const char *mnt_optmap_get_type(const struct mnt_optmap *mapent)
{
	char *type;

	assert(mapent);
	assert(mapent->name);

	type = strrchr(mapent->name, '=');
	if (!type)
		return NULL;			/* value is unexpected */
	if (type == mapent->name)
		return NULL;			/* wrong format of type definition */
	type++;
	if (*type != '%' && *type != '{')
		return NULL;			/* wrong format of type definition */
	return type ? : NULL;
}

/*
 * Does the option (that is described by @mntent) require any value? (e.g.
 * uid=<foo>)
 */
int mnt_optmap_require_value(const struct mnt_optmap *mapent)
{
	char *type;

	assert(mapent);
	assert(mapent->name);

	type = strchr(mapent->name, '=');
	if (!type)
		return 0;			/* value is unexpected */
	if (type == mapent->name)
		return 0;			/* wrong format of type definition */
	if (*(type - 1) == '[')
		return 0;			/* optional */
	return 1;
}
