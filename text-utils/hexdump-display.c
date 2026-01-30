/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <linux/fiemap.h>
#include <linux/fs.h>
#endif
#include "hexdump.h"
#include "xalloc.h"
#include "c.h"
#include "nls.h"
#include "colors.h"

static void doskip(const char *, int, struct hexdump *);
static u_char *get(struct hexdump *);

#ifdef __linux__
#define FIEMAP_EXTENTS_BATCH 256

static void free_fiemap(struct hexdump *hex)
{
	if (!hex->fiemap)
		return;
	free(hex->fiemap);
	hex->fiemap = NULL;
}

/*
 * Use FIEMAP ioctl to get file extent map for sparse file optimization.
 * This allows us to skip holes without reading them.
 */
static void init_fiemap(struct hexdump *hex, int fd)
{
	struct stat st;
	struct fiemap *fm;
	size_t fm_size;

	/* Free previous fiemap (when processing multiple files) */
	free_fiemap(hex);
	hex->current_extent = 0;
	hex->file_size = 0;
	hex->in_sparse_hole = 0;
	hex->region_end = 0;

	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))
		return;

	hex->file_size = st.st_size;

	fm_size = sizeof(struct fiemap) + sizeof(struct fiemap_extent) * FIEMAP_EXTENTS_BATCH;
	fm = xcalloc(1, fm_size);

	fm->fm_start = 0;
	fm->fm_length = st.st_size;
	fm->fm_flags = 0;
	fm->fm_extent_count = FIEMAP_EXTENTS_BATCH;

	if (ioctl(fd, FS_IOC_FIEMAP, fm) < 0) {
		free(fm);
		return;
	}

	/* If no extents, the entire file is a hole - keep fiemap to indicate this */
	if (fm->fm_mapped_extents == 0) {
		hex->fiemap = fm;
		return;
	}

	/* Check if we got all extents or need more */
	if (fm->fm_mapped_extents == FIEMAP_EXTENTS_BATCH &&
	    !(fm->fm_extents[fm->fm_mapped_extents - 1].fe_flags & FIEMAP_EXTENT_LAST)) {
		unsigned int count = FIEMAP_EXTENTS_BATCH * 16;
		free(fm);
		fm_size = sizeof(struct fiemap) + sizeof(struct fiemap_extent) * count;
		fm = xcalloc(1, fm_size);
		fm->fm_start = 0;
		fm->fm_length = st.st_size;
		fm->fm_flags = 0;
		fm->fm_extent_count = count;

		if (ioctl(fd, FS_IOC_FIEMAP, fm) < 0) {
			free(fm);
			return;
		}
	}

	hex->fiemap = fm;
}

/*
 * Check if position is in a hole.
 *
 * Returns: 1 if in hole, 0 if in data.
 */
static int check_hole(struct hexdump *hex, off_t pos)
{
	struct fiemap *fm = hex->fiemap;
	unsigned int i;
	struct fiemap_extent *last_ext;

	if (!fm) {
		hex->in_sparse_hole = 0;
		hex->region_end = 0;
		return 0;
	}

	/* If no extents, entire file is a hole */
	if (fm->fm_mapped_extents == 0) {
		hex->in_sparse_hole = 1;
		hex->region_end = hex->file_size;
		return 1;
	}

	/* Start search from current_extent for efficiency */
	for (i = hex->current_extent; i < fm->fm_mapped_extents; i++) {
		struct fiemap_extent *ext = &fm->fm_extents[i];
		off_t ext_end = ext->fe_logical + ext->fe_length;

		if (pos < (off_t)ext->fe_logical) {
			/* pos is before this extent - it's in a hole */
			hex->current_extent = i;
			hex->in_sparse_hole = 1;
			hex->region_end = ext->fe_logical;
			return 1;
		}
		if (pos < ext_end) {
			/* pos is within this extent - it's in data */
			hex->current_extent = i;
			hex->in_sparse_hole = 0;
			hex->region_end = ext_end;
			return 0;
		}
	}

	last_ext = &fm->fm_extents[fm->fm_mapped_extents - 1];
	if (last_ext->fe_flags & FIEMAP_EXTENT_LAST) {
		hex->in_sparse_hole = 1;
		hex->region_end = hex->file_size;
		return 1;
	}

	/* Incomplete extent map - disable optimization for safety */
	hex->in_sparse_hole = 0;
	hex->region_end = hex->file_size;
	return 0;
}
#endif /* __linux__ */

enum _vflag vflag = FIRST;

static off_t address;			/* address/offset in stream */
static off_t eaddress;			/* end address */

static const char *color_cond(struct hexdump_pr *pr, unsigned char *bp, int bcnt)
{
	register struct list_head *p;
	register struct hexdump_clr *clr;
	off_t offt;
	int match;

	list_for_each(p, pr->colorlist) {
		clr = list_entry(p, struct hexdump_clr, colorlist);
		offt = clr->offt;
		match = 0;

		/* no offset or offset outside this print unit */
		if (offt < 0)
			offt = address;
		if (offt < address || offt + clr->range > address + bcnt)
			continue;

		/* match a string */
		if (clr->str) {
			if (pr->flags == F_ADDRESS) {
				/* TODO */
			}
			else if (!strncmp(clr->str, (char *)bp + offt
				- address, clr->range))
				match = 1;
		/* match a value */
		} else if (clr->val != -1) {
			int val = 0;
			/* addresses are not part of the input, so we can't
			 * compare with the contents of bp */
			if (pr->flags == F_ADDRESS) {
				if (clr->val == address)
					match = 1;
			} else {
				memcpy(&val, bp + offt - address, clr->range);
				if (val == clr->val)
					match = 1;
			}
		/* no conditions, only a color was specified */
		} else
			return clr->fmt;

		/* return the format string or check for another */
		if (match ^ clr->invert)
			return clr->fmt;
	}

	/* no match */
	return NULL;
}

static inline void
print(struct hexdump_pr *pr, unsigned char *bp) {

	const char *color = NULL;

	if (pr->colorlist && (color = color_cond(pr, bp, pr->bcnt)))
		color_enable(color);

	switch(pr->flags) {
	case F_ADDRESS:
		printf(pr->fmt, address);
		break;
	case F_BPAD:
		printf(pr->fmt, "");
		break;
	case F_C:
		conv_c(pr, bp);
		break;
	case F_CHAR:
		printf(pr->fmt, *bp);
		break;
	case F_DBL:
	    {
		double dval;
		float fval;
		switch(pr->bcnt) {
		case 4:
			memmove(&fval, bp, sizeof(fval));
			printf(pr->fmt, fval);
			break;
		case 8:
			memmove(&dval, bp, sizeof(dval));
			printf(pr->fmt, dval);
			break;
		}
		break;
	    }
	case F_INT:
	    {
		char cval;	/* int8_t */
		short sval;	/* int16_t */
		int ival;	/* int32_t */
		long long Lval;	/* int64_t, int64_t */

		switch(pr->bcnt) {
		case 1:
			memmove(&cval, bp, sizeof(cval));
			printf(pr->fmt, (unsigned long long) cval);
			break;
		case 2:
			memmove(&sval, bp, sizeof(sval));
			printf(pr->fmt, (unsigned long long) sval);
			break;
		case 4:
			memmove(&ival, bp, sizeof(ival));
			printf(pr->fmt, (unsigned long long) ival);
			break;
		case 8:
			memmove(&Lval, bp, sizeof(Lval));
			printf(pr->fmt, Lval);
			break;
		}
		break;
	    }
	case F_P:
		printf(pr->fmt, isprint(*bp) ? *bp : '.');
		break;
	case F_STR:
		printf(pr->fmt, (char *)bp);
		break;
	case F_TEXT:
		printf("%s", pr->fmt);
		break;
	case F_U:
		conv_u(pr, bp);
		break;
	case F_UINT:
	    {
		unsigned short sval;	/* u_int16_t */
		unsigned int ival;	/* u_int32_t */
		unsigned long long Lval;/* u_int64_t, u_int64_t */

		switch(pr->bcnt) {
		case 1:
			printf(pr->fmt, (unsigned long long) *bp);
			break;
		case 2:
			memmove(&sval, bp, sizeof(sval));
			printf(pr->fmt, (unsigned long long) sval);
			break;
		case 4:
			memmove(&ival, bp, sizeof(ival));
			printf(pr->fmt, (unsigned long long) ival);
			break;
		case 8:
			memmove(&Lval, bp, sizeof(Lval));
			printf(pr->fmt, Lval);
			break;
		}
		break;
	    }
	}
	if (color) /* did we colorize something? */
		color_disable();
}

static void bpad(struct hexdump_pr *pr)
{
	static const char *spec = " -0+#";
	char *p1, *p2;

	/*
	 * remove all conversion flags; '-' is the only one valid
	 * with %s, and it's not useful here.
	 */
	pr->flags = F_BPAD;
	pr->cchar[0] = 's';
	pr->cchar[1] = 0;

	p1 = pr->fmt;
	while (*p1 != '%')
		++p1;

	p2 = ++p1;
	while (*p1 && strchr(spec, *p1))
		++p1;

	while ((*p2++ = *p1++))
		;
}

void display(struct hexdump *hex)
{
	register struct list_head *fs;
	register struct hexdump_fs *fss;
	register struct hexdump_fu *fu;
	register struct hexdump_pr *pr;
	register int cnt;
	register unsigned char *bp;
	off_t saveaddress;
	unsigned char savech = 0, *savebp;
	struct list_head *p, *q, *r;

	while ((bp = get(hex)) != NULL) {
		ssize_t rem = hex->blocksize;

		fs = &hex->fshead; savebp = bp; saveaddress = address;

		list_for_each(p, fs) {
			fss = list_entry(p, struct hexdump_fs, fslist);

			list_for_each(q, &fss->fulist) {
				fu = list_entry(q, struct hexdump_fu, fulist);

				if (fu->flags&F_IGNORE)
					break;

				cnt = fu->reps;

				while (cnt && rem >= 0) {
					list_for_each(r, &fu->prlist) {
						pr = list_entry(r, struct hexdump_pr, prlist);

						if (eaddress && address >= eaddress
						    && !(pr->flags&(F_TEXT|F_BPAD)))
							bpad(pr);

						if (cnt == 1 && pr->nospace) {
							savech = *pr->nospace;
							*pr->nospace = '\0';
							print(pr, bp);
							*pr->nospace = savech;
						} else
							print(pr, bp);

						address += pr->bcnt;

						rem -= pr->bcnt;
						if (rem < 0)
							break;

						bp += pr->bcnt;
					}
					--cnt;
				}
			}
			bp = savebp;
			rem = hex->blocksize;
			address = saveaddress;
		}
	}
	if (endfu) {
		/*
		 * if eaddress not set, error or file size was multiple of
		 * blocksize, and no partial block ever found.
		 */
		if (!eaddress) {
			if (!address)
				return;
			eaddress = address;
		}
		list_for_each (p, &endfu->prlist) {
			const char *color = NULL;

			pr = list_entry(p, struct hexdump_pr, prlist);
			if (colors_wanted() && pr->colorlist
			    && (color = color_cond(pr, bp, pr->bcnt))) {
				color_enable(color);
			}

			switch(pr->flags) {
			case F_ADDRESS:
				printf(pr->fmt, eaddress);
				break;
			case F_TEXT:
				printf("%s", pr->fmt);
				break;
			}
			if (color) /* did we highlight something? */
				color_disable();
		}
	}
}

static char **_argv;

static u_char *
get(struct hexdump *hex)
{
	static int ateof = 1;
	static u_char *curp, *savp;
	ssize_t n, need, nread;
	u_char *tmpp;

	if (!curp) {
		curp = xcalloc(1, hex->blocksize);
		savp = xcalloc(1, hex->blocksize);
	} else {
		tmpp = curp;
		curp = savp;
		savp = tmpp;
		address += hex->blocksize;
	}
	need = hex->blocksize, nread = 0;
	while (TRUE) {
		/*
		 * if read the right number of bytes, or at EOF for one file,
		 * and no other files are available, zero-pad the rest of the
		 * block and set the end flag.
		 */
		if (!hex->length || (ateof && !next(NULL, hex))) {
			if (need == hex->blocksize)
				goto retnul;
			if (!need && vflag != ALL &&
			    !memcmp(curp, savp, nread)) {
				if (vflag != DUP)
					printf("*\n");
				goto retnul;
			}
			if (need > 0)
				memset((char *)curp + nread, 0, need);
			eaddress = address + nread;
			return(curp);
		}
		if (fileno(stdin) == -1) {
			warnx(_("all input file arguments failed"));
			goto retnul;
		}

#ifdef __linux__
		/*
		 * FIEMAP-based sparse file optimization:
		 */
		if (hex->fiemap) {
			off_t curpos = address + nread;

			if (curpos >= hex->region_end)
				check_hole(hex, curpos);

			if (hex->in_sparse_hole && vflag == DUP) {
				int savp_is_zero = 1;
				ssize_t j;
				for (j = 0; j < hex->blocksize; j++) {
					if (savp[j] != 0) {
						savp_is_zero = 0;
						break;
					}
				}

				// Only if savp is all zeros, we can skip the hole.
				if (savp_is_zero) {
					off_t next_data = hex->region_end;
					off_t aligned_pos = (next_data / hex->blocksize) * hex->blocksize;
					if (aligned_pos > curpos) {
						off_t skip = aligned_pos - curpos;
						if (fseeko(stdin, aligned_pos, SEEK_SET) == 0) {
							address = aligned_pos;
							if (hex->length != -1)
								hex->length -= skip;
							memset(curp, 0, hex->blocksize);
							need = hex->blocksize;
							nread = 0;
							hex->region_end = 0;
							continue;
						}
					}
				}
			}
		}
#endif

		n = fread((char *)curp + nread, sizeof(unsigned char),
		    hex->length == -1 ? need : min(hex->length, need), stdin);
		if (!n) {
			if (ferror(stdin))
				warn("%s", _argv[-1]);
			ateof = 1;
			continue;
		}
		ateof = 0;
		if (hex->length != -1)
			hex->length -= n;
		if (!(need -= n)) {
			if (vflag == ALL || vflag == FIRST ||
			    memcmp(curp, savp, hex->blocksize) != 0) {
				if (vflag == DUP || vflag == FIRST)
					vflag = WAIT;
				return(curp);
			}
			if (vflag == WAIT)
				printf("*\n");
			vflag = DUP;
			address += hex->blocksize;
			need = hex->blocksize;
			nread = 0;
		}
		else
			nread += n;
	}
retnul:
	free (curp);
	free (savp);
#ifdef __linux__
	free_fiemap(hex);
#endif
	return NULL;
}

int next(char **argv, struct hexdump *hex)
{
	static int done;
	int statok;

	if (argv) {
		_argv = argv;
		return(1);
	}
	while (TRUE) {
		if (*_argv) {
			if (!(freopen(*_argv, "r", stdin))) {
				warn("%s", *_argv);
				hex->exitval = EXIT_FAILURE;
				++_argv;
				continue;
			}
			statok = done = 1;
		} else {
			if (done++)
				return(0);
			statok = 0;
		}
#ifdef __linux__
		init_fiemap(hex, fileno(stdin));
#endif
		if (hex->skip)
			doskip(statok ? *_argv : "stdin", statok, hex);
		if (*_argv)
			++_argv;
		if (!hex->skip)
			return(1);
	}
	/* NOTREACHED */
}

static void
doskip(const char *fname, int statok, struct hexdump *hex)
{
	struct stat sbuf;

	if (statok) {
		if (fstat(fileno(stdin), &sbuf))
			err(EXIT_FAILURE, "%s", fname);
		if (S_ISREG(sbuf.st_mode) && hex->skip > sbuf.st_size) {
		  /* If size valid and skip >= size */
			hex->skip -= sbuf.st_size;
			address += sbuf.st_size;
			return;
		}
	}
	/* sbuf may be undefined here - do not test it */
	if (fseek(stdin, hex->skip, SEEK_SET))
		err(EXIT_FAILURE, "%s", fname);
	address += hex->skip;
	hex->skip = 0;
}
