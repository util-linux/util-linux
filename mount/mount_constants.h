#ifndef MS_RDONLY
#define MS_RDONLY	 1	/* Mount read-only */
#endif
#ifndef MS_NOSUID
#define MS_NOSUID	 2	/* Ignore suid and sgid bits */
#endif
#ifndef MS_NODEV
#define MS_NODEV	 4	/* Disallow access to device special files */
#endif
#ifndef MS_NOEXEC
#define MS_NOEXEC	 8	/* Disallow program execution */
#endif
#ifndef MS_SYNCHRONOUS
#define MS_SYNCHRONOUS	16	/* Writes are synced at once */
#endif
#ifndef MS_REMOUNT
#define MS_REMOUNT	32	/* Alter flags of a mounted FS */
#endif
#ifndef MS_MANDLOCK
#define MS_MANDLOCK	64	/* Allow mandatory locks on an FS */
#endif
#ifndef MS_DIRSYNC
#define MS_DIRSYNC	128	/* Directory modifications are synchronous */
#endif

#ifndef MS_ACTION_MASK
#define	MS_ACTION_MASK	0x380
/* Remount, but new filesystem may be different from old. Atomic
   (i.e. there is no interval when nothing is mounted at the mountpoint).
   If new fs differs from the old one and old is busy - -EBUSY. */
#define	MS_REPLACE	0x080	/* 128 */
/* After, Before: as soon as we get unions these will add a new member
   in the end or beginning of the chain. Fail if there is a stack
   on the mountpoint. */
#define	MS_AFTER	0x100	/* 256 */
#define	MS_BEFORE	0x180
/* Over: if nothing mounted on a mountpoint - same as if none of these
flags had been set; if we have a union with more than one element - fail;
if we have a stack or plain mount - mount atop of it, forming a stack. */
#define	MS_OVER		0x200	/* 512 */
#endif
#ifndef MS_NOATIME
#define MS_NOATIME	0x400	/* 1024: Do not update access times. */
#endif
#ifndef MS_NODIRATIME
#define MS_NODIRATIME   0x800	/* 2048: Don't update directory access times */
#endif
#ifndef MS_BIND
#define	MS_BIND		0x1000	/* 4096: Mount existing tree also elsewhere */
#endif
#ifndef MS_MOVE
#define MS_MOVE		0x2000	/* 8192: Atomically move tree */
#endif
#ifndef MS_REC
#define MS_REC		0x4000	/* 16384: Recursive loopback */
#endif
#ifndef MS_VERBOSE
#define MS_VERBOSE	0x8000	/* 32768 */
#endif
/*
 * Magic mount flag number. Had to be or-ed to the flag values.
 */
#ifndef MS_MGC_VAL
#define MS_MGC_VAL 0xC0ED0000	/* magic flag number to indicate "new" flags */
#endif
#ifndef MS_MGC_MSK
#define MS_MGC_MSK 0xffff0000	/* magic flag number mask */
#endif
