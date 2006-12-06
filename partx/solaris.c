#include <stdio.h>
#include <time.h>		/* time_t */
#include "partx.h"

#define SOLARIS_X86_NUMSLICE	8
#define SOLARIS_X86_VTOC_SANE	(0x600DDEEEUL)

typedef int daddr_t;		/* or long - check */

struct solaris_x86_slice {
	unsigned short	s_tag;		/* ID tag of partition */
	unsigned short	s_flag;		/* permision flags */
	daddr_t 	s_start;	/* start sector no of partition */
	long		s_size;		/* # of blocks in partition */
};

struct solaris_x86_vtoc {
	unsigned long v_bootinfo[3];	/* info for mboot */
	unsigned long v_sanity;		/* to verify vtoc sanity */
	unsigned long v_version;	/* layout version */
	char	v_volume[8];		/* volume name */
	unsigned short	v_sectorsz;	/* sector size in bytes */
	unsigned short	v_nparts;	/* number of partitions */
	unsigned long v_reserved[10];	/* free space */
	struct solaris_x86_slice
		v_slice[SOLARIS_X86_NUMSLICE];   /* slice headers */
	time_t	timestamp[SOLARIS_X86_NUMSLICE]; /* timestamp */
	char	v_asciilabel[128];	/* for compatibility */
};

int
read_solaris_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct solaris_x86_vtoc *v;
	struct solaris_x86_slice *s;
	unsigned int offset = all.start;
	int i, n;
	char *bp;

	bp = getblock(fd, offset+1); 	/* 1 sector suffices */
	if (bp == NULL)
		return -1;

	v = (struct solaris_x86_vtoc *) bp;
	if(v->v_sanity != SOLARIS_X86_VTOC_SANE)
		return -1;

	if(v->v_version != 1) {
		fprintf(stderr, "Cannot handle solaris version %ld vtoc\n",
		       v->v_version);
		return 0;
	}

	for(i=0, n=0; i<SOLARIS_X86_NUMSLICE; i++) {
		s = &v->v_slice[i];

		if (s->s_size == 0)
			continue;
		if (n < ns) {
			sp[n].start = offset + s->s_start;
			sp[n].size = s->s_size;
			n++;
		} else {
			fprintf(stderr,
				"solaris_x86_partition: too many slices\n");
			break;
		}
	}
	return n;
}

