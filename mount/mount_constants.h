#define MS_RDONLY	 1	/* Mount read-only */
#define MS_NOSUID	 2	/* Ignore suid and sgid bits */
#define MS_NODEV	 4	/* Disallow access to device special files */
#define MS_NOEXEC	 8	/* Disallow program execution */
#define MS_SYNCHRONOUS	16	/* Writes are synced at once */
#define MS_REMOUNT	32	/* Alter flags of a mounted FS */
#define MS_MANDLOCK	64	/* Allow mandatory locks on an FS */
#define MS_NOATIME	1024	/* Do not update access times. */
#define MS_NODIRATIME   2048    /* Do not update directory access times */
/*
 * Magic mount flag number. Has to be or-ed to the flag values.
 */
#ifndef MS_MGC_VAL
#define MS_MGC_VAL 0xC0ED0000	/* magic flag number to indicate "new" flags */
#endif
#define MS_MGC_MSK 0xffff0000	/* magic flag number mask */
