#ifndef DOS_H_INCLUDED
#define DOS_H_INCLUDED

struct partition {
	unsigned char boot_ind;	/* 0x80 - active */
	unsigned char bh, bs, bc;
	unsigned char sys_type;
	unsigned char eh, es, ec;
	unsigned char start_sect[4];
	unsigned char nr_sects[4];
};

#endif				/* DOS_H_INCLUDED */
