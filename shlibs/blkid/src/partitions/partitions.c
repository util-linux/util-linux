/*
 * partitions - partition tables parsing
 *
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>

#include "partitions.h"

/**
 * SECTION:partitions
 * @title: Partitions probing
 * @short_description: partitions tables detection and parsing
 *
 * This chain supports binary and NAME=value interfaces, but complete PT
 * description is provided by binary interface only.
 *
 * NAME=value interface, supported tags:
 *
 * @PTTYPE: partition table type (dos, gpt, etc.).
 *
 * Example:
 *
 * <informalexample>
 *  <programlisting>
 * blkid_probe pr;
 * const char *ptname;
 *
 * pr = blkid_new_probe_from_filename(devname);
 * if (!pr)
 *	err("%s: faild to open device", devname);
 *
 * blkid_probe_enable_partitions(pr, TRUE);
 * blkid_do_fullprobe(pr);
 *
 * blkid_probe_lookup_value(pr, "PTTYPE", &ptname, NULL);
 * printf("%s partition type detected\n", pttype);
 *
 * blkid_free_probe(pr);
 *
 * // don't forget to check return codes in your code!
 *  </programlisting>
 * </informalexample>
 *
 * Binary interface:
 *
 * <informalexample>
 *  <programlisting>
 * blkid_probe pr;
 * blkid_partlist ls;
 * int nparts, i;
 *
 * pr = blkid_new_probe_from_filename(devname);
 * if (!pr)
 *	err("%s: faild to open device", devname);
 *
 * ls = blkid_probe_get_partitions(pr);
 * nparts = blkid_partlist_numof_partitions(ls);
 *
 * for (i = 0; i < nparts; i++) {
 *      blkid_partition par = blkid_partlist_get_partition(ls, i);
 *      printf("#%d: %llu %llu  0x%x",
 *               blkid_partition_get_partno(par),
 *               blkid_partition_get_start(par),
 *               blkid_partition_get_size(par),
 *               blkid_partition_get_type(par));
 * }
 *
 * blkid_free_probe(pr);
 *
 * // don't forget to check return codes in your code!
 *  </programlisting>
 * </informalexample>
 */

/*
 * Chain driver function
 */
static int partitions_probe(blkid_probe pr, struct blkid_chain *chn);
static void partitions_free_data(blkid_probe pr, void *data);

/*
 * Partitions chain probing functions
 */
static const struct blkid_idinfo *idinfos[] =
{
	&aix_pt_idinfo,
	&sgi_pt_idinfo,
	&sun_pt_idinfo,
	&dos_pt_idinfo,
	&gpt_pt_idinfo,
	&mac_pt_idinfo,
	&bsd_pt_idinfo,
	&unixware_pt_idinfo,
	&solaris_x86_pt_idinfo,
	&minix_pt_idinfo
};

/*
 * Driver definition
 */
const struct blkid_chaindrv partitions_drv = {
	.id           = BLKID_CHAIN_PARTS,
	.name         = "partitions",
	.dflt_enabled = FALSE,
	.idinfos      = idinfos,
	.nidinfos     = ARRAY_SIZE(idinfos),
	.has_fltr     = TRUE,
	.probe        = partitions_probe,
	.safeprobe    = partitions_probe,
	.free_data    = partitions_free_data
};


/*
 * For compatibility with the rest of libblkid API (with the old high-level
 * API) we use completely opaque typedefs for all structs. Don't forget that
 * the final blkid_* types are pointers! See blkid.h.
 *
 * [Just for the record, I hate typedef for pointers --kzak]
 */

/* exported as opaque type "blkid_parttable" */
struct blkid_struct_parttable {
	const char	*type;		/* partition table type */
	blkid_loff_t	offset;		/* begin of the partition table */
	int		nparts;		/* number of partitions */
	blkid_partition	parent;		/* parent of nested partition table */

	struct list_head t_tabs;	/* all tables */
};

/* exported as opaque type "blkid_partition" */
struct blkid_struct_partition {
	blkid_loff_t	start;		/* begin of the partition */
	blkid_loff_t	size;		/* size of the partitions */

	int		type;		/* partition type */
	char		typestr[37];	/* partition type string (GPT and Mac) */

	int		partno;		/* partition number */
	char		uuid[37];	/* UUID (when supported by PT), e.g GPT */
	unsigned char	name[128];	/* Partition in UTF8 name (when supporte by PT), e.g. Mac */

	blkid_parttable	tab;		/* partition table */
};

/* exported as opaque type "blkid_partlist" */
struct blkid_struct_partlist {
	int		next_partno;	/* next partition number */
	blkid_partition next_parent;	/* next parent if parsing nested PT */

	int		nparts;		/* number of partitions */
	int		nparts_max;	/* max.number of partitions */
	blkid_partition	parts;		/* array of partitions */

	struct list_head l_tabs;	/* list of partition tables */
};

/**
 * blkid_probe_enable_partitions:
 * @pr: probe
 * @enable: TRUE/FALSE
 *
 * Enables/disables the partitions probing for non-binary interface.
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_enable_partitions(blkid_probe pr, int enable)
{
	if (!pr)
		return -1;
	pr->chains[BLKID_CHAIN_PARTS].enabled = enable;
	return 0;
}

/**
 * blkid_probe_set_partitions_flags:
 * @pr: prober
 * @flags: BLKID_PARTS_* flags
 *
 * Sets probing flags to the partitions prober. This function is optional.
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_set_partitions_flags(blkid_probe pr, int flags)
{
	if (!pr)
		return -1;

	pr->chains[BLKID_CHAIN_PARTS].flags = flags;
	return 0;
}

/**
 * blkid_probe_reset_partitions_filter:
 * @pr: prober
 *
 * Resets partitions probing filter
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_reset_partitions_filter(blkid_probe pr)
{
	return __blkid_probe_reset_filter(pr, BLKID_CHAIN_PARTS);
}

/**
 * blkid_probe_invert_partitions_filter:
 * @pr: prober
 *
 * Inverts partitions probing filter
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_invert_partitions_filter(blkid_probe pr)
{
	return __blkid_probe_invert_filter(pr, BLKID_CHAIN_PARTS);
}

/**
 * blkid_probe_filter_partitions_type:
 * @pr: prober
 * @flag: filter BLKID_FLTR_{NOTIN,ONLYIN} flag
 * @names: NULL terminated array of probing function names (e.g. "vfat").
 *
 *  BLKID_FLTR_NOTIN  - probe for all items which are NOT IN @names
 *  BLKID_FLTR_ONLYIN - probe for items which are IN @names
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_filter_partitions_type(blkid_probe pr, int flag, char *names[])
{
	return __blkid_probe_filter_types(pr, BLKID_CHAIN_PARTS, flag, names);
}

/**
 * blkid_probe_get_partitions:
 * @pr: probe
 *
 * This is a binary interface for partitions. See also blkid_partlist_*
 * functions.
 *
 * This function is independent on blkid_do_[safe,full]probe() and
 * blkid_probe_enable_partitions() calls.
 *
 * WARNING: the returned object will be overwritten by the next
 *          blkid_probe_get_partitions() call for the same @pr. If you want to
 *          use more blkid_partlist objects in the same time you have to create
 *          more blkid_probe handlers (see blkid_new_probe()).
 *
 * TODO:    add blkid_ref() and blkid_unref() to allows to use blkid_partlist
 *          independently on libblkid probing stuff.
 *
 * Returns: list of partitions, or NULL in case of error.
 */
blkid_partlist blkid_probe_get_partitions(blkid_probe pr)
{
	return (blkid_partlist) blkid_probe_get_binary_data(pr,
			&pr->chains[BLKID_CHAIN_PARTS]);
}

/* for internal usage only */
blkid_partlist blkid_probe_get_partlist(blkid_probe pr)
{
	return (blkid_partlist) pr->chains[BLKID_CHAIN_PARTS].data;
}

static void ref_parttable(blkid_parttable tab)
{
	tab->nparts++;
}

static void unref_parttable(blkid_parttable tab)
{
	tab->nparts--;

	if (tab->nparts <= 0) {
		list_del(&tab->t_tabs);
		free(tab);
	}
}

/* free all allocated parttables */
static void free_parttables(blkid_partlist ls)
{
	if (!ls || !ls->l_tabs.next)
		return;

	/* remove unassigned partition tables */
	while (!list_empty(&ls->l_tabs)) {
		blkid_parttable tab = list_entry(ls->l_tabs.next,
					struct blkid_struct_parttable, t_tabs);
		unref_parttable(tab);
	}
}

static void reset_partlist(blkid_probe pr, blkid_partlist ls)
{
	if (!ls)
		return;

	free_parttables(ls);

	if (ls->next_partno) {
		/* already initialized - reset */
		int tmp_nparts = ls->nparts_max;
		blkid_partition tmp_parts = ls->parts;

		memset(ls, 0, sizeof(struct blkid_struct_partlist));

		ls->nparts_max = tmp_nparts;
		ls->parts = tmp_parts;
	}

	ls->nparts = 0;
	ls->next_partno = 1;
	INIT_LIST_HEAD(&ls->l_tabs);

	DBG(DEBUG_LOWPROBE, printf("partlist reseted\n"));
}

static blkid_partlist partitions_init_data(blkid_probe pr, struct blkid_chain *chn)
{
	blkid_partlist ls;

	if (chn->data)
		ls = (blkid_partlist) chn->data;
	else {
		/* allocate the new list of partitions */
		ls = calloc(1, sizeof(struct blkid_struct_partlist));
		if (!ls)
			return NULL;
		chn->data = (void *) ls;
	}

	reset_partlist(pr, ls);

	DBG(DEBUG_LOWPROBE,
		printf("parts: initialized partitions list (%p, size=%d)\n",
		ls, ls->nparts_max));
	return ls;
}

static void partitions_free_data(blkid_probe pr, void *data)
{
	blkid_partlist ls = (blkid_partlist) data;

	if (!ls)
		return;

	free_parttables(ls);

	/* deallocate partitions and partlist */
	free(ls->parts);
	free(ls);
}

blkid_parttable blkid_partlist_new_parttable(blkid_partlist ls,
				const char *type, blkid_loff_t offset)
{
	blkid_parttable tab;

	tab = calloc(1, sizeof(struct blkid_struct_parttable));
	if (!tab)
		return NULL;
	tab->type = type;
	tab->offset = offset;
	tab->parent = ls->next_parent;

	INIT_LIST_HEAD(&tab->t_tabs);
	list_add_tail(&tab->t_tabs, &ls->l_tabs);

	DBG(DEBUG_LOWPROBE,
		printf("parts: create a new partition table "
		       "(%p, type=%s, offset=%llu)\n", tab, type,
		       (unsigned long long) offset));
	return tab;
}

static blkid_partition new_partition(blkid_partlist ls, blkid_parttable tab)
{
	blkid_partition par;

	if (ls->nparts + 1 > ls->nparts_max) {
		/* Linux kernel has DISK_MAX_PARTS=256, but it's too much for
		 * generic Linux machine -- let start with 32 partititions.
		 */
		ls->parts = realloc(ls->parts, (ls->nparts_max + 32) *
					sizeof(struct blkid_struct_partition));
		if (!ls->parts)
			return NULL;
		ls->nparts_max += 32;
	}

	par = &ls->parts[ls->nparts++];
	memset(par, 0, sizeof(struct blkid_struct_partition));

	ref_parttable(tab);
	par->tab = tab;
	par->partno = ls->next_partno++;

	return par;
}

blkid_partition blkid_partlist_add_partition(blkid_partlist ls,
					blkid_parttable tab, int type,
					blkid_loff_t start, blkid_loff_t size)
{
	blkid_partition par = new_partition(ls, tab);

	if (!par)
		return NULL;

	par->type = type;
	par->start = start;
	par->size = size;

	DBG(DEBUG_LOWPROBE,
		printf("parts: add partition (%p type=0x%x, "
			"start=%llu, size=%llu, table=%p)\n",
			par, par->type,
			(unsigned long long) par->start,
			(unsigned long long) par->size,
			tab));
	return par;
}

/* allows to modify used partitions numbers (for example for logical partitions) */
int blkid_partlist_set_partno(blkid_partlist ls, int partno)
{
	if (!ls)
		return -1;
	ls->next_partno = partno;
	return 0;
}

/* allows to set "parent" for the next nested partition */
int blkid_partlist_set_parent(blkid_partlist ls, blkid_partition par)
{
	if (!ls)
		return -1;
	ls->next_parent = par;
	return 0;
}

blkid_partition blkid_partlist_get_parent(blkid_partlist ls)
{
	if (!ls)
		return NULL;
	return ls->next_parent;
}

int blkid_partitions_need_typeonly(blkid_probe pr)
{
	struct blkid_chain *chn = blkid_probe_get_chain(pr);

	return chn && chn->data && chn->binary ? FALSE : TRUE;
}

/* get private chain flags */
int blkid_partitions_get_flags(blkid_probe pr)
{
	struct blkid_chain *chn = blkid_probe_get_chain(pr);

	return chn ? chn->flags : 0;
}

/* check if @start and @size are within @par partition */
int blkid_is_nested_dimension(blkid_partition par,
			blkid_loff_t start, blkid_loff_t size)
{
	blkid_loff_t pstart;
	blkid_loff_t psize;

	if (!par)
		return 0;

	pstart = blkid_partition_get_start(par);
	psize = blkid_partition_get_size(par);

	if (start < pstart || start + size > pstart + psize)
		return 0;

	return 1;
}

static int idinfo_probe(blkid_probe pr, const struct blkid_idinfo *id)
{
	const struct blkid_idmag *mag;
	int hasmag = 0;
	int rc = 1;		/* = nothing detected */

	if (pr->size <= 0 || (id->minsz && id->minsz > pr->size))
		goto nothing;	/* the device is too small */

	mag = id->magics ? &id->magics[0] : NULL;

	/* try to detect by magic string */
	while(mag && mag->magic) {
		int idx;
		unsigned char *buf;

		idx = mag->kboff + (mag->sboff >> 10);
		buf = blkid_probe_get_buffer(pr, idx << 10, 1024);

		if (buf && !memcmp(mag->magic,
				buf + (mag->sboff & 0x3ff), mag->len)) {
			DBG(DEBUG_LOWPROBE, printf(
				"%s: magic sboff=%u, kboff=%ld\n",
				id->name, mag->sboff, mag->kboff));
			hasmag = 1;
			break;
		}
		mag++;
	}

	if (hasmag == 0 && id->magics && id->magics[0].magic)
		/* magic string(s) defined, but not found */
		goto nothing;

	/* final check by probing function */
	if (id->probefunc) {
		DBG(DEBUG_LOWPROBE, printf(
			"%s: ---> call probefunc()\n", id->name));
		rc = id->probefunc(pr, mag);
	        if (rc == -1) {
			/* reset after error */
			reset_partlist(pr, blkid_probe_get_partlist(pr));
			DBG(DEBUG_LOWPROBE, printf(
				"%s probefunc failed\n", id->name));
		}
		DBG(DEBUG_LOWPROBE, printf(
			"%s: <--- (rc = %d)\n", id->name, rc));
	}

nothing:
	return rc;
}

/*
 * The blkid_do_probe() backend.
 */
static int partitions_probe(blkid_probe pr, struct blkid_chain *chn)
{
	int i = 0;

	if (!pr || chn->idx < -1)
		return -1;
	blkid_probe_chain_reset_vals(pr, chn);

	if (chn->binary)
		partitions_init_data(pr, chn);

	DBG(DEBUG_LOWPROBE,
		printf("--> starting probing loop [PARTS idx=%d]\n",
		chn->idx));

	i = chn->idx + 1;

	for ( ; i < ARRAY_SIZE(idinfos); i++) {
		const char *name;

		chn->idx = i;

		/* apply filter */
		if (chn->fltr && blkid_bmp_get_item(chn->fltr, i))
			continue;

		/* apply checks from idinfo */
		if (idinfo_probe(pr, idinfos[i]) != 0)
			continue;

		name = idinfos[i]->name;

		/* all checks passed */
		blkid_probe_set_value(pr, "PTTYPE",
				(unsigned char *) name,	strlen(name) + 1);

		DBG(DEBUG_LOWPROBE,
			printf("<-- leaving probing loop (type=%s) [PARTS idx=%d]\n",
			name, chn->idx));
		return 0;
	}
	DBG(DEBUG_LOWPROBE,
		printf("<-- leaving probing loop (failed) [PARTS idx=%d]\n",
		chn->idx));
	return 1;
}

/* Probe for nested partition table within the parental partition */
int blkid_partitions_do_subprobe(blkid_probe pr, blkid_partition parent,
		const struct blkid_idinfo *id)
{
	int rc = 1;
	blkid_partlist ls;
	blkid_loff_t saved_sz, saved_off, sz, off;

	DBG(DEBUG_LOWPROBE, printf(
		"parts: ----> %s subprobe requested (parent=%p)\n",
		id->name, parent));

	if (!pr || !parent || !parent->size)
		return -1;

	ls = blkid_probe_get_partlist(pr);

	sz = ((blkid_loff_t) parent->size) << 9;
	off = ((blkid_loff_t) parent->start) << 9;

	/* get the current setting in bytes */
	blkid_probe_get_dimension(pr, &saved_off, &saved_sz);

	/* check the requested range */
	if (off < saved_off || saved_off + saved_sz < off + sz) {
		DBG(DEBUG_LOWPROBE, printf(
			"ERROR: parts: <---- '%s' sub-probe: overflow detected.\n",
			id->name));
		return -1;
	}

	/* define sub-range with in device */
	blkid_probe_set_dimension(pr, off, sz);

	blkid_partlist_set_parent(ls, parent);

	rc = idinfo_probe(pr, id);

	blkid_partlist_set_parent(ls, NULL);

	/* restore the original setting */
	blkid_probe_set_dimension(pr, saved_off, saved_sz);

	DBG(DEBUG_LOWPROBE, printf(
		"parts: <---- %s subprobe done (parent=%p, rc=%d)\n",
		id->name, parent, rc));

	return rc;
}

/**
 * blkid_known_pttype:
 * @pttype: partiton name
 *
 * Returns: 1 for known or 0 for unknown partition type.
 */
int blkid_known_pttype(const char *pttype)
{
	int i;

	if (!pttype)
		return 0;

	for (i = 0; i < ARRAY_SIZE(idinfos); i++) {
		const struct blkid_idinfo *id = idinfos[i];
		if (strcmp(id->name, pttype) == 0)
			return 1;
	}
	return 0;
}

/**
 * blkid_partlist_numof_partitions:
 * @ls: partitions list
 *
 * Returns: number of partitions in the list or -1 in case of error.
 */
int blkid_partlist_numof_partitions(blkid_partlist ls)
{
	return ls ? ls->nparts : -1;
}

/**
 * blkid_partlist_get_partition:
 * @ls: partitions list
 * @n: partition number in range 0..N, where 'N' is blkid_partlist_numof_partitions().
 *
 * It's possible that the list of partitions is *empty*, but there is a valid
 * partition table on the disk. This happen when on-disk details about
 * partitions are unknown, but we are able to detect partition table magic
 * string only. The nice example is AIX. If your question is: "Is there any
 * partition table?", use:
 *
 *	blkid_probe_lookup_value(pr, "PTTYPE", &type, NULL);
 *
 * Returns: partition object or NULL in case or error.
 */
blkid_partition blkid_partlist_get_partition(blkid_partlist ls, int n)
{
	if (!ls || n < 0 || n >= ls->nparts)
		return NULL;

	return &ls->parts[n];
}

/**
 * blkid_parttable_get_type:
 * @tab: partition table
 *
 * Returns: partition table type (type name, e.g. "dos", "gpt", ...)
 */
const char *blkid_parttable_get_type(blkid_parttable tab)
{
	return tab ? tab->type : NULL;
}

/**
 * blkid_parttable_get_parent:
 * @tab: partition table
 *
 * Returns: parent for nexted partitition tables or NULL.
 */
blkid_partition blkid_parttable_get_parent(blkid_parttable tab)
{
	return tab ? tab->parent : NULL;
}

/**
 * blkid_parttable_get_offset:
 * @tab: partition table
 *
 * Returns: position (in bytes) of the partition table or -1 in case of error.
 *
 * Note the position is relative to begin of the device as defined by
 * blkid_probe_set_device() for primary partition table, and relative
 * to parental partition for nested patition tables.
 *
 * <informalexample>
 *   <programlisting>
 * off_t offset;
 * blkid_partition parent = blkid_parttable_get_parent(tab);
 *
 * offset = blkid_parttable_get_offset(tab);
 *
 * if (parent)
 *      / * 'tab' is nested partition table * /
 *	offset += blkid_partition_get_start(parent);
 *   </programlisting>
 * </informalexample>
 */
blkid_loff_t blkid_parttable_get_offset(blkid_parttable tab)
{
	return tab ? tab->offset : -1;
}

/**
 * blkid_partition_get_table:
 * @par: partition
 *
 * The "parttable" describes partition table. The table is usually the same for
 * all partitions -- except nested partition tables.
 *
 * For example bsd, solaris, etc. use a nested partition table within
 * standard primary dos partition:
 *
 * <informalexample>
 *   <programlisting>
 *
 *  -- dos partition table
 *  0: sda1     dos primary partition
 *  1: sda2     dos primary partition
 *     -- bsd partition table (with in sda2)
 *  2:    sda5  bds partition
 *  3:    sda6  bds partition
 *
 *   </programlisting>
 * </informalexample>
 *
 * The library does not to use a separate partition table object for dos logical
 * partitions (partitions within extended partition). It's possible to
 * differentiate between logical, extended and primary partitions by
 *
 *	blkid_partition_is_{extended,primary,logical}().
 *
 * Returns: partition table object or NULL in case of error.
 */
blkid_parttable blkid_partition_get_table(blkid_partition par)
{
	return par ? par->tab : NULL;
}

static int partition_get_logical_type(blkid_partition par)
{
	blkid_parttable tab;

	if (!par)
		return -1;

	tab = blkid_partition_get_table(par);
	if (!tab || !tab->type)
		return -1;

	if (tab->parent)
		return 'L';  /* report nested partitions as logical */

	if (!strcmp(tab->type, "dos")) {
		if (par->partno > 4)
			return 'L';	/* logical */

	        if(par->type == BLKID_DOS_EXTENDED_PARTITION ||
                   par->type == BLKID_W95_EXTENDED_PARTITION ||
		   par->type == BLKID_LINUX_EXTENDED_PARTITION)
			return 'E';
	}
	return 'P';
}

/**
 * blkid_partition_is_primary:
 * @par: partition
 *
 * Note, this function returns FALSE for DOS extended partitions and
 * all partitions in nested partition tables.
 *
 * Returns: 1 if the partitions is primary partition or 0 if not.
 */
int blkid_partition_is_primary(blkid_partition par)
{
	return partition_get_logical_type(par) == 'P' ? TRUE : FALSE;
}

/**
 * blkid_partition_is_extended:
 * @par: partition
 *
 * Returns: 1 if the partitions is extended (dos, windows or linux)
 * partition or 0 if not.
 */
int blkid_partition_is_extended(blkid_partition par)
{
	return partition_get_logical_type(par) == 'E' ? TRUE : FALSE;
}

/**
 * blkid_partition_is_logical:
 * @par: partition
 *
 * Note that this function returns TRUE for all partitions in all
 * nested partition tables (e.g. BSD labels).
 *
 * Returns: 1 if the partitions is logical partition or 0 if not.
 */
int blkid_partition_is_logical(blkid_partition par)
{
	return partition_get_logical_type(par) == 'L' ? TRUE : FALSE;
}

static void set_string(unsigned char *item, size_t max,
				const unsigned char *data, size_t len)
{
	if (len >= max)
		len = max - 1;

	memcpy(item, data, len);
	item[len] = '\0';

	blkid_rtrim_whitespace(item);
}

int blkid_partition_set_name(blkid_partition par,
		const unsigned char *name, size_t len)
{
	if (!par)
		return -1;

	set_string(par->name, sizeof(par->name), name, len);
	return 0;
}

int blkid_partition_set_utf8name(blkid_partition par, const unsigned char *name,
		size_t len, int enc)
{
	if (!par)
		return -1;

	blkid_encode_to_utf8(enc, par->name, sizeof(par->name), name, len);
	blkid_rtrim_whitespace(par->name);
	return 0;
}

int blkid_partition_set_uuid(blkid_partition par, const unsigned char *uuid)
{
	if (!par)
		return -1;

	blkid_unparse_uuid(uuid, par->uuid, sizeof(par->uuid));
	return 0;
}

/**
 * blkid_partition_get_name:
 * @par: partition
 *
 * Returns: partition name string if supported by PT (e.g. Mac) or NULL.
 */
const char *blkid_partition_get_name(blkid_partition par)
{
	return par && *par->name ? (char *) par->name : NULL;
}

/**
 * blkid_partition_get_uuid:
 * @par: partition
 *
 * Returns: partition UUID string if supported by PT (e.g. GPT) or NULL.
 */
const char *blkid_partition_get_uuid(blkid_partition par)
{
	return par && *par->uuid ? par->uuid : NULL;
}

/**
 * blkid_partition_get_partno:
 * @par: partition
 *
 * Returns: proposed partitin number (e.g. 'N' from sda'N') or -1 in case of
 * error. Note that the number is generate by library independenly on your OS.
 */
int blkid_partition_get_partno(blkid_partition par)
{
	return par ? par->partno : -1;
}

/**
 * blkid_partition_get_start:
 * @par: partition
 *
 * Be careful if you _not_ probe whole disk:
 *
 * 1) the offset is usully relative to begin of the disk -- but if you probe a
 *    fragment of the disk only -- then the offset could be still relative to
 *    the begin of the disk rather that relative to the fragment.
 *
 * 2) the offset for nested partitions could be releative to parent (e.g. Solaris)
 *    _or_ relative to the begin of the whole disk (e.g. bsd).
 *
 * You don't have to care about such details if you proble whole disk. In such
 * a case libblkid always returns the offset relative to the begin of the disk.
 *
 * Returns: start of the partition (in 512-sectors).
 */
blkid_loff_t blkid_partition_get_start(blkid_partition par)
{
	return par ? par->start : -1;
}

/**
 * blkid_partition_get_size:
 * @par: partition
 *
 * WARNING: be very careful when you work with MS-DOS extended partitions. The
 *          library always returns full size of the partition. If you want add
 *          the partition to the Linux system (BLKPG_ADD_PARTITION ioctl) you
 *          need to reduce the size of the partition to 1 or 2 blocks. The
 *          rest of the partition has to be unaccessible for mkfs or mkswap
 *          programs, we need a small space for boot loaders only.
 *
 *          For some unknown reason this (safe) practice is not to used for
 *          nested BSD, Solaris, ..., partition tables in Linux kernel.
 *
 * Returns: size of the partition (in 512-sectors).
 */
blkid_loff_t blkid_partition_get_size(blkid_partition par)
{
	return par ? par->size : -1;
}

/**
 * blkid_partition_get_type:
 * @par: partition
 *
 * Returns: partition type.
 */
int blkid_partition_get_type(blkid_partition par)
{
	return par ? par->type : 0;
}

/* Sets partition 'type' for PT where the type is defined by string rather
 * than by number
 */
int blkid_partition_set_type_string(blkid_partition par,
		const unsigned char *type, size_t len)
{
	if (!par)
		return -1;

	set_string((unsigned char *) par->typestr,
			sizeof(par->typestr), type, len);
	return 0;
}

/* Sets partition 'type' for PT where the type is defined by UUIDrather
 * than by number
 */
int blkid_partition_set_type_uuid(blkid_partition par, const unsigned char *uuid)
{
	if (!par)
		return -1;

	blkid_unparse_uuid(uuid, par->typestr, sizeof(par->typestr));
	return 0;
}

/**
 * blkid_partition_get_type_string:
 * @par: partition
 *
 * The type string is supported by a small subset of partition tables (e.g Mac
 * and EFI GPT).  Note that GPT uses type UUID and this function returns this
 * UUID as string.
 *
 * Returns: partition type string or NULL.
 */
const char *blkid_partition_get_type_string(blkid_partition par)
{
	return par && *par->typestr ? par->typestr : NULL;
}

