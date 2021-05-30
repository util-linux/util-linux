/*
 * lscpu-arm.c - ARM CPU identification tables
 *
 * Copyright (C) 2018 Riku Voipio <riku.voipio@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The information here is gathered from
 *  - ARM manuals
 *  - Linux kernel: arch/armX/include/asm/cputype.h
 *  - GCC sources: config/arch/arch-cores.def
 *  - Ancient wisdom
 *  - SMBIOS tables (if applicable)
 */
#include "lscpu.h"

struct id_part {
    const int id;
    const char* name;
};

static const struct id_part arm_part[] = {
    { 0x810, "ARM810" },
    { 0x920, "ARM920" },
    { 0x922, "ARM922" },
    { 0x926, "ARM926" },
    { 0x940, "ARM940" },
    { 0x946, "ARM946" },
    { 0x966, "ARM966" },
    { 0xa20, "ARM1020" },
    { 0xa22, "ARM1022" },
    { 0xa26, "ARM1026" },
    { 0xb02, "ARM11 MPCore" },
    { 0xb36, "ARM1136" },
    { 0xb56, "ARM1156" },
    { 0xb76, "ARM1176" },
    { 0xc05, "Cortex-A5" },
    { 0xc07, "Cortex-A7" },
    { 0xc08, "Cortex-A8" },
    { 0xc09, "Cortex-A9" },
    { 0xc0d, "Cortex-A17" },	/* Originally A12 */
    { 0xc0f, "Cortex-A15" },
    { 0xc0e, "Cortex-A17" },
    { 0xc14, "Cortex-R4" },
    { 0xc15, "Cortex-R5" },
    { 0xc17, "Cortex-R7" },
    { 0xc18, "Cortex-R8" },
    { 0xc20, "Cortex-M0" },
    { 0xc21, "Cortex-M1" },
    { 0xc23, "Cortex-M3" },
    { 0xc24, "Cortex-M4" },
    { 0xc27, "Cortex-M7" },
    { 0xc60, "Cortex-M0+" },
    { 0xd01, "Cortex-A32" },
    { 0xd03, "Cortex-A53" },
    { 0xd04, "Cortex-A35" },
    { 0xd05, "Cortex-A55" },
    { 0xd06, "Cortex-A65" },
    { 0xd07, "Cortex-A57" },
    { 0xd08, "Cortex-A72" },
    { 0xd09, "Cortex-A73" },
    { 0xd0a, "Cortex-A75" },
    { 0xd0b, "Cortex-A76" },
    { 0xd0c, "Neoverse-N1" },
    { 0xd0d, "Cortex-A77" },
    { 0xd0e, "Cortex-A76AE" },
    { 0xd13, "Cortex-R52" },
    { 0xd20, "Cortex-M23" },
    { 0xd21, "Cortex-M33" },
    { 0xd41, "Cortex-A78" },
    { 0xd42, "Cortex-A78AE" },
    { 0xd4a, "Neoverse-E1" },
    { 0xd4b, "Cortex-A78C" },
    { -1, "unknown" },
};

static const struct id_part brcm_part[] = {
    { 0x0f, "Brahma B15" },
    { 0x100, "Brahma B53" },
    { 0x516, "ThunderX2" },
    { -1, "unknown" },
};

static const struct id_part dec_part[] = {
    { 0xa10, "SA110" },
    { 0xa11, "SA1100" },
    { -1, "unknown" },
};

static const struct id_part cavium_part[] = {
    { 0x0a0, "ThunderX" },
    { 0x0a1, "ThunderX 88XX" },
    { 0x0a2, "ThunderX 81XX" },
    { 0x0a3, "ThunderX 83XX" },
    { 0x0af, "ThunderX2 99xx" },
    { -1, "unknown" },
};

static const struct id_part apm_part[] = {
    { 0x000, "X-Gene" },
    { -1, "unknown" },
};

static const struct id_part qcom_part[] = {
    { 0x00f, "Scorpion" },
    { 0x02d, "Scorpion" },
    { 0x04d, "Krait" },
    { 0x06f, "Krait" },
    { 0x201, "Kryo" },
    { 0x205, "Kryo" },
    { 0x211, "Kryo" },
    { 0x800, "Falkor V1/Kryo" },
    { 0x801, "Kryo V2" },
    { 0xc00, "Falkor" },
    { 0xc01, "Saphira" },
    { -1, "unknown" },
};

static const struct id_part samsung_part[] = {
    { 0x001, "exynos-m1" },
    { -1, "unknown" },
};

static const struct id_part nvidia_part[] = {
    { 0x000, "Denver" },
    { 0x003, "Denver 2" },
    { 0x004, "Carmel" },
    { -1, "unknown" },
};

static const struct id_part marvell_part[] = {
    { 0x131, "Feroceon 88FR131" },
    { 0x581, "PJ4/PJ4b" },
    { 0x584, "PJ4B-MP" },
    { -1, "unknown" },
};

static const struct id_part faraday_part[] = {
    { 0x526, "FA526" },
    { 0x626, "FA626" },
    { -1, "unknown" },
};

static const struct id_part intel_part[] = {
    { 0x200, "i80200" },
    { 0x210, "PXA250A" },
    { 0x212, "PXA210A" },
    { 0x242, "i80321-400" },
    { 0x243, "i80321-600" },
    { 0x290, "PXA250B/PXA26x" },
    { 0x292, "PXA210B" },
    { 0x2c2, "i80321-400-B0" },
    { 0x2c3, "i80321-600-B0" },
    { 0x2d0, "PXA250C/PXA255/PXA26x" },
    { 0x2d2, "PXA210C" },
    { 0x411, "PXA27x" },
    { 0x41c, "IPX425-533" },
    { 0x41d, "IPX425-400" },
    { 0x41f, "IPX425-266" },
    { 0x682, "PXA32x" },
    { 0x683, "PXA930/PXA935" },
    { 0x688, "PXA30x" },
    { 0x689, "PXA31x" },
    { 0xb11, "SA1110" },
    { 0xc12, "IPX1200" },
    { -1, "unknown" },
};

static const struct id_part fujitsu_part[] = {
    { 0x001, "A64FX" },
    { -1, "unknown" },
};

static const struct id_part hisi_part[] = {
    { 0xd01, "Kunpeng-920" },	/* aka tsv110 */
    { -1, "unknown" },
};

static const struct id_part unknown_part[] = {
    { -1, "unknown" },
};

struct hw_impl {
   const int    id;
   const struct id_part     *parts;
   const char   *name;
};

static const struct hw_impl hw_implementer[] = {
    { 0x41, arm_part,     "ARM" },
    { 0x42, brcm_part,    "Broadcom" },
    { 0x43, cavium_part,  "Cavium" },
    { 0x44, dec_part,     "DEC" },
    { 0x46, fujitsu_part, "FUJITSU" },
    { 0x48, hisi_part,    "HiSilicon" },
    { 0x49, unknown_part, "Infineon" },
    { 0x4d, unknown_part, "Motorola/Freescale" },
    { 0x4e, nvidia_part,  "Nvidia" },
    { 0x50, apm_part,     "APM" },
    { 0x51, qcom_part,    "Qualcomm" },
    { 0x53, samsung_part, "Samsung" },
    { 0x56, marvell_part, "Marvell" },
    { 0x61, unknown_part, "Apple" },
    { 0x66, faraday_part, "Faraday" },
    { 0x69, intel_part,   "Intel" },
    { 0xc0, unknown_part, "Ampere" },
    { -1,   unknown_part, "unknown" },
};

static int parse_id(const char *str)
{
	int id;
	char *end = NULL;

	if (!str || strncmp(str, "0x",2) != 0)
		return -EINVAL;

	errno = 0;
	id = (int) strtol(str, &end, 0);
	if (errno || str == end)
		return -EINVAL;

	return id;
}

#define parse_model_id(_cxt)		(parse_id((_cxt)->model))

static inline int parse_implementer_id(struct lscpu_cputype *ct)
{
	if (ct->vendor_id)
		return ct->vendor_id;
	ct->vendor_id = parse_id(ct->vendor);
	return ct->vendor_id;
}

/*
 * Use model and vendor IDs to decode to human readable names.
 */
static int arm_ids_decode(struct lscpu_cputype *ct)
{
	int impl, part, j;
	const struct id_part *parts = NULL;

	impl = parse_implementer_id(ct);
	if (impl <= 0)
		return -EINVAL;	/* no ARM or missing ID */

	/* decode vendor */
	for (j = 0; hw_implementer[j].id != -1; j++) {
		if (hw_implementer[j].id == impl) {
			parts = hw_implementer[j].parts;
			free(ct->vendor);
			ct->vendor = xstrdup(hw_implementer[j].name);
			break;
		}
	}

	/* decode model */
	if (!parts)
		goto done;

	part = parse_model_id(ct);
	if (part <= 0)
		goto done;

	for (j = 0; parts[j].id != -1; j++) {
		if (parts[j].id == part) {
			free(ct->modelname);
			ct->modelname = xstrdup(parts[j].name);
			break;
		}
	}
done:
	return 0;
}

/* use "rXpY" string as stepping */
static int arm_rXpY_decode(struct lscpu_cputype *ct)
{
	int impl, revision, variant;
	char *end = NULL;
	char buf[8];

	impl = parse_implementer_id(ct);

	if (impl != 0x41 || !ct->revision || !ct->stepping)
		return -EINVAL;

	errno = 0;
	revision = (int) strtol(ct->revision, &end, 10);
	if (errno || ct->revision == end)
		return -EINVAL;

	errno = 0;
	variant = (int) strtol(ct->stepping, &end, 0);
	if (errno || ct->stepping == end)
		return -EINVAL;

	snprintf(buf, sizeof(buf), "r%dp%d", variant, revision);
	free(ct->stepping);
	ct->stepping = xstrdup(buf);

	return 0;
}

#define PROC_MFR_OFFSET		0x07
#define PROC_VERSION_OFFSET	0x10

/*
 * Use firmware to get human readable names
 */
static int arm_smbios_decode(struct lscpu_cputype *ct)
{
	uint8_t data[8192];
	char buf[128], *str;
	struct lscpu_dmi_header h;
	int fd;
	ssize_t rs;

	fd = open(_PATH_SYS_DMI_TYPE4, O_RDONLY);
	if (fd < 0)
		return fd;

	rs = read_all(fd, (char *) data, 8192);
	close(fd);

	if (rs == -1)
		return -1;

	to_dmi_header(&h, data);

	str = dmi_string(&h, data[PROC_MFR_OFFSET]);
	if (str) {
		xstrncpy(buf, str, 127);
		ct->bios_vendor = xstrdup(buf);
	}

	str = dmi_string(&h, data[PROC_VERSION_OFFSET]);
	if (str) {
		xstrncpy(buf, str, 127);
		ct->bios_modelname = xstrdup(buf);
	}

	return 0;
}

static void arm_decode(struct lscpu_cxt *cxt, struct lscpu_cputype *ct)
{
	/* use SMBIOS Type 4 data if available */
	if (!cxt->noalive && access(_PATH_SYS_DMI_TYPE4, R_OK) == 0)
		arm_smbios_decode(ct);

	arm_ids_decode(ct);
	arm_rXpY_decode(ct);
	if (cxt->is_cluster)
		ct->nr_socket_on_cluster = get_number_of_physical_sockets_from_dmi();
}

static int is_cluster_arm(struct lscpu_cxt *cxt)
{
	struct stat st;

	if (!(strcmp(cxt->arch->name, "aarch64")) &&
	     (stat(_PATH_ACPI_PPTT, &st) < 0) && (cxt->ncputypes == 1))
		return 1;
	else
		return 0;
}

void lscpu_decode_arm(struct lscpu_cxt *cxt)
{
	size_t i;

	cxt->is_cluster = is_cluster_arm(cxt);

	for (i = 0; i < cxt->ncputypes; i++)
		arm_decode(cxt, cxt->cputypes[i]);
}
