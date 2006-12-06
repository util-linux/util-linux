#ifndef PARTX_H_INCLUDED
#define PARTX_H_INCLUDED

/*
 * For each partition type there is a routine that takes
 * a block device and a range, and returns the list of
 * slices found there in the supplied array SP that can
 * hold NS entries. The return value is the number of
 * entries stored, or -1 if the appropriate type is not
 * present.
 */


/* units: 512 byte sectors */
struct slice {
	unsigned int start;
	unsigned int size;
};

typedef int (ptreader)(int fd, struct slice all, struct slice *sp, int ns);

extern ptreader read_dos_pt, read_bsd_pt, read_solaris_pt, read_unixware_pt, read_gpt_pt;

char *getblock(int fd, unsigned int secnr);

static inline int
four2int(unsigned char *p) {
	return p[0] + (p[1]<<8) + (p[2]<<16) + (p[3]<<24);
}

#endif /* PARTX_H_INCLUDED */
