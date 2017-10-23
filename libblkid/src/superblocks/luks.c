/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * Inspired by libvolume_id by
 *     Kay Sievers <kay.sievers@vrfy.org>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include "superblocks.h"

#define LUKS_CIPHERNAME_L		32
#define LUKS_CIPHERMODE_L		32
#define LUKS_HASHSPEC_L			32
#define LUKS_DIGESTSIZE			20
#define LUKS_SALTSIZE			32
#define LUKS_MAGIC_L			6
#define UUID_STRING_L			40
#define LUKS2_LABEL_L			48
#define LUKS2_SALT_L			64
#define LUKS2_CHECKSUM_ALG_L		32
#define LUKS2_CHECKSUM_L		64

struct luks_phdr {
	uint8_t		magic[LUKS_MAGIC_L];
	uint16_t	version;
	uint8_t		cipherName[LUKS_CIPHERNAME_L];
	uint8_t		cipherMode[LUKS_CIPHERMODE_L];
	uint8_t		hashSpec[LUKS_HASHSPEC_L];
	uint32_t	payloadOffset;
	uint32_t	keyBytes;
	uint8_t		mkDigest[LUKS_DIGESTSIZE];
	uint8_t		mkDigestSalt[LUKS_SALTSIZE];
	uint32_t	mkDigestIterations;
	uint8_t		uuid[UUID_STRING_L];
} __attribute__((packed));

struct luks2_phdr {
	char		magic[LUKS_MAGIC_L];
	uint16_t	version;
	uint64_t	hdr_size;	/* in bytes, including JSON area */
	uint64_t	seqid;		/* increased on every update */
	char		label[LUKS2_LABEL_L];
	char		checksum_alg[LUKS2_CHECKSUM_ALG_L];
	uint8_t		salt[LUKS2_SALT_L]; /* unique for every header/offset */
	char		uuid[UUID_STRING_L];
	char		subsystem[LUKS2_LABEL_L]; /* owner subsystem label */
	uint64_t	hdr_offset;	/* offset from device start in bytes */
	char		_padding[184];
	uint8_t		csum[LUKS2_CHECKSUM_L];
	/* Padding to 4k, then JSON area */
} __attribute__ ((packed));

static int probe_luks(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct luks_phdr *header_v1;
	struct luks2_phdr *header;
	int version;

	header = blkid_probe_get_sb(pr, mag, struct luks2_phdr);
	if (header == NULL)
		return errno ? -errno : 1;

	version = be16_to_cpu(header->version);
	blkid_probe_sprintf_version(pr, "%u", be16_to_cpu(header->version));

	if (version == 1) {
		header_v1 = (struct luks_phdr *)header;
		blkid_probe_strncpy_uuid(pr,
			(unsigned char *) header_v1->uuid, UUID_STRING_L);
	} else if (version == 2) {
		blkid_probe_strncpy_uuid(pr,
			(unsigned char *) header->uuid, UUID_STRING_L);
		blkid_probe_set_label(pr,
			(unsigned char *) header->label, LUKS2_LABEL_L);
		blkid_probe_set_id_label(pr, "SUBSYSTEM",
			(unsigned char *) header->subsystem, LUKS2_LABEL_L);
	}
	return 0;
}

const struct blkid_idinfo luks_idinfo =
{
	.name		= "crypto_LUKS",
	.usage		= BLKID_USAGE_CRYPTO,
	.probefunc	= probe_luks,
	.magics		=
	{
		{ .magic = "LUKS\xba\xbe", .len = 6 },
		{ NULL }
	}
};
