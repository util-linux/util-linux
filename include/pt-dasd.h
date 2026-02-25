/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_PT_DASD_H
#define UTIL_LINUX_PT_DASD_H

#include <stdint.h>
#include "bitops.h"

#define DASD_MAX_PARTITIONS	3

#define DASD_VOL1_MAGIC		"\xe5\xd6\xd3\xf1"	/* "VOL1" in EBCDIC */
#define DASD_LNX1_MAGIC		"\xd3\xd5\xe7\xf1"	/* "LNX1" in EBCDIC */
#define DASD_CMS1_MAGIC		"\xc3\xd4\xe2\xf1"	/* "CMS1" in EBCDIC */

#define DASD_FMT_ID_F1		0xf1
#define DASD_FMT_ID_F4		0xf4
#define DASD_FMT_ID_F5		0xf5
#define DASD_FMT_ID_F7		0xf7
#define DASD_FMT_ID_F8		0xf8
#define DASD_FMT_ID_F9		0xf9

/* Large volume compatibility cylinder threshold */
#define DASD_LV_COMPAT_CYL	0xFFFE

#define DASD_VOLSER_LENGTH	6

/* Format 4 key field: 44 bytes of 0x04 */
#define DASD_F4_KEYCD_BYTE	0x04
#define DASD_F4_KEYCD_LENGTH	44

struct dasd_cchhb {
	uint16_t cc;
	uint16_t hh;
	uint8_t  b;
} __attribute__ ((packed));

struct dasd_cchh {
	uint16_t cc;
	uint16_t hh;
} __attribute__ ((packed));

struct dasd_extent {
	uint8_t		typeind;
	uint8_t		seqno;
	struct dasd_cchh llimit;
	struct dasd_cchh ulimit;
} __attribute__ ((packed));

/*
 * CDL Volume Label.
 */
struct dasd_volume_label_cdl {
	char		volkey[4];	/* record key*/
	char		vollbl[4];	/* "VOL1" in EBCDIC */
	char		volid[6];	/* volume identifier (VOLSER) */
	uint8_t		security;
	struct dasd_cchhb vtoc;		/* VTOC address */
	char		res1[5];
	char		cisize[4];
	char		blkperci[4];
	char		labperci[4];
	char		res2[4];
	char		lvtoc[14];
	char		res3[29];
} __attribute__ ((packed));

/*
 * LDL Volume Label.
 */
struct dasd_volume_label_ldl {
	char		vollbl[4];	/* "LNX1" or "CMS1" in EBCDIC */
	char		volid[6];	/* volume identifier (VOLSER) */
	char		res1[69];
	char		ldl_version;
	uint64_t	formatted_blocks; /* for ldl_version 0xf2 */
} __attribute__ ((packed));

/*
 * Format 1 / Format 8 DSCB (partition description)
 */
struct dasd_format1_label {
	char		DS1DSNAM[44];	/* data set name (EBCDIC) */
	uint8_t		DS1FMTID;	/* format identifier (0xf1 or 0xf8) */
	unsigned char	DS1DSSN[6];
	uint16_t	DS1VOLSQ;
	uint8_t		DS1CREDT[3];
	uint8_t		DS1EXPDT[3];
	uint8_t		DS1NOEPV;
	uint8_t		DS1NOBDB;
	uint8_t		DS1FLAG1;
	unsigned char	DS1SYSCD[13];
	uint8_t		DS1REFD[3];
	uint8_t		DS1SMSFG;
	uint8_t		DS1SCXTF;
	uint16_t	DS1SCXTV;
	uint8_t		DS1DSRG1;
	uint8_t		DS1DSRG2;
	uint8_t		DS1RECFM;
	uint8_t		DS1OPTCD;
	uint16_t	DS1BLKL;
	uint16_t	DS1LRECL;
	uint8_t		DS1KEYL;
	uint16_t	DS1RKP;
	uint8_t		DS1DSIND;
	uint8_t		DS1SCAL1;
	char		DS1SCAL3[3];
	uint8_t		DS1LSTAR[3];
	uint16_t	DS1TRBAL;
	uint16_t	res1;
	struct dasd_extent DS1EXT1;
	struct dasd_extent DS1EXT2;
	struct dasd_extent DS1EXT3;
	struct dasd_cchhb  DS1PTRDS;
} __attribute__ ((packed));

/*
 * Format 4 DSCB (VTOC header with geometry information)
 */
struct dasd_format4_label {
	char		DS4KEYCD[44];	/* key: 44 bytes of 0x04 */
	uint8_t		DS4IDFMT;	/* format identifier (0xf4) */
	struct dasd_cchhb DS4HPCHR;
	uint16_t	DS4DSREC;
	struct dasd_cchh  DS4HCCHH;
	uint16_t	DS4NOATK;
	uint8_t		DS4VTOCI;
	uint8_t		DS4NOEXT;
	uint8_t		DS4SMSFG;
	uint8_t		DS4DEVAC;
	uint16_t	DS4DSCYL;	/* number of logical cylinders */
	uint16_t	DS4DSTRK;	/* number of tracks per cylinder */
	uint16_t	DS4DEVTK;	/* device track length */
	uint8_t		DS4DEVI;
	uint8_t		DS4DEVL;
	uint8_t		DS4DEVK;
	uint8_t		DS4DEVFG;
	uint16_t	DS4DEVTL;
	uint8_t		DS4DEVDT;
	uint8_t		DS4DEVDB;
	char		DS4AMTIM[8];
	char		DS4AMCAT[3];
	char		DS4R2TIM[8];
	char		res1[5];
	char		DS4F6PTR[5];
	struct dasd_extent DS4VTOCE;
	char		res2[10];
	uint8_t		DS4EFLVL;
	struct dasd_cchhb  DS4EFPTR;
	char		res3;
	uint32_t	DS4DCYL;	/* number of logical cylinders (large volumes) */
	char		res4[2];
	uint8_t		DS4DEVF2;
	char		res5;
} __attribute__ ((packed));

/*
 * EBCDIC to ASCII conversion table.
 */
static const unsigned char dasd_ebcdic_to_ascii[256] __attribute__((unused)) =
{
/* 0x00  NUL   SOH   STX   ETX  *SEL    HT  *RNL   DEL */
	0x00, 0x01, 0x02, 0x03, 0x07, 0x09, 0x07, 0x7F,
/* 0x08  -GE  -SPS  -RPT    VT    FF    CR    SO    SI */
	0x07, 0x07, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
/* 0x10  DLE   DC1   DC2   DC3  -RES   -NL    BS  -POC */
	0x10, 0x11, 0x12, 0x13, 0x07, 0x0A, 0x08, 0x07,
/* 0x18  CAN    EM  -UBS  -CU1  -IFS  -IGS  -IRS  -ITB */
	0x18, 0x19, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
/* 0x20  -DS  -SOS    FS  -WUS  -BYP    LF   ETB   ESC */
	0x07, 0x07, 0x1C, 0x07, 0x07, 0x0A, 0x17, 0x1B,
/* 0x28  -SA  -SFE   -SM  -CSP  -MFA   ENQ   ACK   BEL */
	0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06, 0x07,
/* 0x30 ----  ----   SYN   -IR   -PP  -TRN  -NBS   EOT */
	0x07, 0x07, 0x16, 0x07, 0x07, 0x07, 0x07, 0x04,
/* 0x38 -SBS   -IT  -RFF  -CU3   DC4   NAK  ----   SUB */
	0x07, 0x07, 0x07, 0x07, 0x14, 0x15, 0x07, 0x1A,
/* 0x40   SP   RSP */
	0x20, 0xFF, 0x83, 0x84, 0x85, 0xA0, 0x07, 0x86,
/* 0x48                      .     <     (     +     | */
	0x87, 0xA4, 0x9B, 0x2E, 0x3C, 0x28, 0x2B, 0x7C,
/* 0x50    & */
	0x26, 0x82, 0x88, 0x89, 0x8A, 0xA1, 0x8C, 0x07,
/* 0x58          ~     !     $     *     )     ; */
	0x8D, 0xE1, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0xAA,
/* 0x60    -     / */
	0x2D, 0x2F, 0x07, 0x8E, 0x07, 0x07, 0x07, 0x8F,
/* 0x68             ----     ,     %     _     >     ? */
	0x80, 0xA5, 0x07, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
/* 0x70  --- */
	0x07, 0x90, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
/* 0x78    *     `     :     #     @     '     =     " */
	0x70, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
/* 0x80    *     a     b     c     d     e     f     g */
	0x07, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
/* 0x88    h     i */
	0x68, 0x69, 0xAE, 0xAF, 0x07, 0x07, 0x07, 0xF1,
/* 0x90          j     k     l     m     n     o     p */
	0xF8, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
/* 0x98    q     r */
	0x71, 0x72, 0xA6, 0xA7, 0x91, 0x07, 0x92, 0x07,
/* 0xA0          ~     s     t     u     v     w     x */
	0xE6, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
/* 0xA8    y     z */
	0x79, 0x7A, 0xAD, 0xAB, 0x07, 0x07, 0x07, 0x07,
/* 0xB0    ^ */
	0x5E, 0x9C, 0x9D, 0xFA, 0x07, 0x07, 0x07, 0xAC,
/* 0xB8       ----     [     ] */
	0xAB, 0x07, 0x5B, 0x5D, 0x07, 0x07, 0x07, 0x07,
/* 0xC0    {     A     B     C     D     E     F     G */
	0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
/* 0xC8    H     I */
	0x48, 0x49, 0x07, 0x93, 0x94, 0x95, 0xA2, 0x07,
/* 0xD0    }     J     K     L     M     N     O     P */
	0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
/* 0xD8    Q     R */
	0x51, 0x52, 0x07, 0x96, 0x81, 0x97, 0xA3, 0x98,
/* 0xE0    \ */
	0x5C, 0xF6, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
/* 0xE8    Y     Z */
	0x59, 0x5A, 0xFD, 0x07, 0x99, 0x07, 0x07, 0x07,
/* 0xF0    0     1     2     3     4     5     6     7 */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
/* 0xF8    8     9 */
	0x38, 0x39, 0x07, 0x07, 0x9A, 0x07, 0x07, 0x07
};

#endif /* UTIL_LINUX_PT_DASD_H */
