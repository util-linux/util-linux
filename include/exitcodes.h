#ifndef UTIL_LINUX_EXITCODES_H
#define UTIL_LINUX_EXITCODES_H
/*
 * BE CAREFUL
 *
 * These exit codes are part of the official interface for mount,
 * fsck, mkfs, etc. wrappers.
 */

/* Exit codes used by mkfs-type programs */
#define MKFS_EX_OK	0	/* No errors */
#define MKFS_EX_ERROR	8	/* Operational error */
#define MKFS_EX_USAGE	16	/* Usage or syntax error */

/* Exit codes used by fsck-type programs */
#define FSCK_EX_OK		0	/* No errors */
#define FSCK_EX_NONDESTRUCT	1	/* File system errors corrected */
#define FSCK_EX_REBOOT		2	/* System should be rebooted */
#define FSCK_EX_DESTRUCT	FSCK_EX_REBOOT	/* Alias */
#define FSCK_EX_UNCORRECTED	4	/* File system errors left uncorrected */
#define FSCK_EX_ERROR		8	/* Operational error */
#define FSCK_EX_USAGE		16	/* Usage or syntax error */
#define FSCK_EX_LIBRARY		128	/* Shared library error */

/* Exit codes used by mount-line programs */
#define MOUNT_EX_SUCCESS	0	/* No errors */
#define MOUNT_EX_USAGE		1	/* incorrect invocation or permission */
#define MOUNT_EX_SYSERR		2	/* out of memory, cannot fork, ... */
#define MOUNT_EX_SOFTWARE	4	/* internal mount bug or wrong version */
#define MOUNT_EX_USER		8	/* user interrupt */
#define MOUNT_EX_FILEIO		16	/* problems writing, locking, ... mtab/fstab */
#define MOUNT_EX_FAIL		32	/* mount failure */
#define MOUNT_EX_SOMEOK		64	/* some mount succeeded */

/* Exit codes use by the su program */
#define SU_EX_EXEC			126 /* faied to execute a program */
#define SU_EX_NOENT			127 /* program was not found */

/* General exit codes */
#define EX_EXEC				127 /* faied to execute a program */

#endif	/* UTIL_LINUX_EXITCODES_H */
