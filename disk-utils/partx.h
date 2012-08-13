#ifndef UTIL_LINUX_PARTX_H
#define UTIL_LINUX_PARTX_H

#include <sys/ioctl.h>
#include <linux/blkpg.h>

#ifndef BLKPG_ADD_PARTITION
# define BLKPG_ADD_PARTITION	1
#endif

#ifndef BLKPG_DEL_PARTITION
# define BLKPG_DEL_PARTITION	2
#endif

#ifndef BLKPG_RESIZE_PARTITION
# define BLKPG_RESIZE_PARTITION	3		/* since Linux 3.6 */
#endif

static inline int partx_del_partition(int fd, unsigned int partno)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;

	p.pno = partno;
	p.start = 0;
	p.length = 0;
	p.devname[0] = 0;
	p.volname[0] = 0;
	a.op = BLKPG_DEL_PARTITION;
	a.flags = 0;
	a.datalen = sizeof(p);
	a.data = &p;

	return ioctl(fd, BLKPG, &a);
}

static inline int partx_add_partition(int fd, int partno,
			uint64_t start, uint64_t size)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;

	p.pno = partno;
	p.start = start << 9;
	p.length = size << 9;
	p.devname[0] = 0;
	p.volname[0] = 0;
	a.op = BLKPG_ADD_PARTITION;
	a.flags = 0;
	a.datalen = sizeof(p);
	a.data = &p;

	return ioctl(fd, BLKPG, &a);
}

static inline int partx_resize_partition(int fd, int partno,
			uint64_t start, uint64_t size)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;

	p.pno = partno;
	p.start = start << 9;
	p.length = size << 9;
	p.devname[0] = 0;
	p.volname[0] = 0;
	a.op = BLKPG_RESIZE_PARTITION;
	a.flags = 0;
	a.datalen = sizeof(p);
	a.data = &p;

	return ioctl(fd, BLKPG, &a);
}

#endif /*  UTIL_LINUX_PARTX_H */
