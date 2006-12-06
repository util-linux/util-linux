#include <stdio.h>
#include "partx.h"

struct partition {
        unsigned char boot_ind;         /* 0x80 - active */
        unsigned char bh, bs, bc;
        unsigned char sys_type;
        unsigned char eh, es, ec;
        unsigned int start_sect;
        unsigned int nr_sects;
};

static int
is_extended(int type) {
	return (type == 5 || type == 0xf || type == 0x85);
}

static int
read_extended_partition(int fd, struct partition *ep,
			struct slice *sp, int ns) {
	struct partition *p;
	unsigned long start, here, next;
	unsigned char *bp;
	int loopct = 0;
	int moretodo = 1;
	int i, n=0;

	here = start = ep->start_sect;

	while (moretodo) {
		moretodo = 0;
		if (++loopct > 100)
			return n;

		bp = getblock(fd, here);
		if (bp == NULL)
			return n;

		if (bp[510] != 0x55 || bp[511] != 0xaa)
			return n;

		p = (struct partition *) (bp + 0x1be);

		for (i=0; i<2; i++, p++) {
			if (p->nr_sects == 0 || is_extended(p->sys_type))
				continue;
			if (n < ns) {
				sp[n].start = here + p->start_sect;
				sp[n].size = p->nr_sects;
				n++;
			} else {
				fprintf(stderr,
				    "dos_extd_partition: too many slices\n");
				return n;
			}
			loopct = 0;
		}

		p -= 2;
		for (i=0; i<2; i++, p++) {
			if(p->nr_sects != 0 && is_extended(p->sys_type)) {
				here = start + p->start_sect;
				moretodo = 1;
				break;
			}
		}
	}
	return n;
}

int
read_dos_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct partition *p;
	unsigned long offset = all.start;
	int i, n=0;
	unsigned char *bp;

	bp = getblock(fd, offset);
	if (bp == NULL)
		return -1;

	if (bp[510] != 0x55 || bp[511] != 0xaa)
		return -1;

	p = (struct partition *) (bp + 0x1be);
	for (i=0; i<4; i++) {
		/* always add, even if zero length */
		if (n < ns) {
			sp[n].start = p->start_sect;
			sp[n].size = p->nr_sects;
			n++;
		} else {
			fprintf(stderr,
				"dos_partition: too many slices\n");
			break;
		}
		p++;
	}
	p = (struct partition *) (bp + 0x1be);
	for (i=0; i<4; i++) {
		if (is_extended(p->sys_type))
			n += read_extended_partition(fd, p, sp+n, ns-n);
		p++;
	}
	return n;
}
