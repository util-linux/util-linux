/*
 * lscpu-dmi - Module to parse SMBIOS information
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
 * Code originally taken from the dmidecode utility and slightly rewritten
 * to suite the needs of lscpu
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "lscpu.h"

#define _PATH_SYS_DMI	 "/sys/firmware/dmi/tables/DMI"

#define WORD(x) (uint16_t)(*(const uint16_t *)(x))
#define DWORD(x) (uint32_t)(*(const uint32_t *)(x))

struct dmi_header
{
	uint8_t type;
	uint8_t length;
	uint16_t handle;
	uint8_t *data;
};

static int checksum(const uint8_t *buf, size_t len)
{
	uint8_t sum = 0;
	size_t a;

	for (a = 0; a < len; a++)
		sum += buf[a];
	return (sum == 0);
}

static void *get_mem_chunk(size_t base, size_t len, const char *devmem)
{
	void *p = NULL;
	int fd;

	if ((fd = open(devmem, O_RDONLY)) < 0)
		return NULL;

	if (!(p = malloc(len)))
		goto nothing;
	if (lseek(fd, base, SEEK_SET) == -1)
		goto nothing;
	if (read_all(fd, p, len) == -1)
		goto nothing;

	close(fd);
	return p;

nothing:
	free(p);
	close(fd);
	return NULL;
}

static void to_dmi_header(struct dmi_header *h, uint8_t *data)
{
	h->type = data[0];
	h->length = data[1];
	h->handle = WORD(data + 2);
	h->data = data;
}

static char *dmi_string(const struct dmi_header *dm, uint8_t s)
{
	char *bp = (char *)dm->data;

	if (s == 0)
		return NULL;

	bp += dm->length;
	while (s > 1 && *bp)
	{
		bp += strlen(bp);
		bp++;
		s--;
	}

	if (!*bp)
		return NULL;

	return bp;
}

static int hypervisor_from_dmi_table(uint32_t base, uint16_t len,
				uint16_t num, const char *devmem)
{
	uint8_t *buf;
	uint8_t *data;
	int i = 0;
	char *vendor = NULL;
	char *product = NULL;
	char *manufacturer = NULL;
	int rc = HYPER_NONE;

	data = buf = get_mem_chunk(base, len, devmem);
	if (!buf)
		goto done;

	 /* 4 is the length of an SMBIOS structure header */
	while (i < num && data + 4 <= buf + len) {
		uint8_t *next;
		struct dmi_header h;

		to_dmi_header(&h, data);

		/*
		 * If a short entry is found (less than 4 bytes), not only it
		 * is invalid, but we cannot reliably locate the next entry.
		 * Better stop at this point.
		 */
		if (h.length < 4)
			goto done;

		/* look for the next handle */
		next = data + h.length;
		while (next - buf + 1 < len && (next[0] != 0 || next[1] != 0))
			next++;
		next += 2;
		switch (h.type) {
			case 0:
				vendor = dmi_string(&h, data[0x04]);
				break;
			case 1:
				manufacturer = dmi_string(&h, data[0x04]);
				product = dmi_string(&h, data[0x05]);
				break;
			default:
				break;
		}

		data = next;
		i++;
	}
	if (manufacturer && !strcmp(manufacturer, "innotek GmbH"))
		rc = HYPER_INNOTEK;
	else if (manufacturer && strstr(manufacturer, "HITACHI") &&
					product && strstr(product, "LPAR"))
		rc = HYPER_HITACHI;
	else if (vendor && !strcmp(vendor, "Parallels"))
		rc = HYPER_PARALLELS;
done:
	free(buf);
	return rc;
}

#if defined(__x86_64__) || defined(__i386__)
static int hypervisor_decode_legacy(uint8_t *buf, const char *devmem)
{
	if (!checksum(buf, 0x0F))
		return -1;

	return hypervisor_from_dmi_table(DWORD(buf + 0x08), WORD(buf + 0x06),
			 WORD(buf + 0x0C),
		devmem);
}
#endif

static int hypervisor_decode_smbios(uint8_t *buf, const char *devmem)
{
	if (!checksum(buf, buf[0x05])
	    || memcmp(buf + 0x10, "_DMI_", 5) != 0
	    || !checksum(buf + 0x10, 0x0F))
		return -1;

	return hypervisor_from_dmi_table(DWORD(buf + 0x18), WORD(buf + 0x16),
			 WORD(buf + 0x1C),
		devmem);
}

static int hypervisor_decode_sysfw(void)
{
	static char const sys_fw_dmi_tables[] = _PATH_SYS_DMI;
	struct stat st;

	if (stat(sys_fw_dmi_tables, &st))
		return -1;

	return hypervisor_from_dmi_table(0, st.st_size, st.st_size / 4,
					 sys_fw_dmi_tables);
}

/*
 * Probe for EFI interface
 */
#define EFI_NOT_FOUND   (-1)
#define EFI_NO_SMBIOS   (-2)
static int address_from_efi(size_t *address)
{
	FILE *tab;
	char linebuf[64];
	int ret;

	*address = 0; /* Prevent compiler warning */

	/*
	 * Linux up to 2.6.6: /proc/efi/systab
	 * Linux 2.6.7 and up: /sys/firmware/efi/systab
	 */
	if (!(tab = fopen("/sys/firmware/efi/systab", "r")) &&
	    !(tab = fopen("/proc/efi/systab", "r")))
		return EFI_NOT_FOUND;		/* No EFI interface */

	ret = EFI_NO_SMBIOS;
	while ((fgets(linebuf, sizeof(linebuf) - 1, tab)) != NULL) {
		char *addrp = strchr(linebuf, '=');
		if (!addrp)
			continue;
		*(addrp++) = '\0';
		if (strcmp(linebuf, "SMBIOS") == 0) {
			*address = strtoul(addrp, NULL, 0);
			ret = 0;
			break;
		}
	}

	fclose(tab);
	return ret;
}

int read_hypervisor_dmi(void)
{
	int rc = HYPER_NONE;
	uint8_t *buf = NULL;
	size_t fp = 0;

	if (sizeof(uint8_t) != 1
	    || sizeof(uint16_t) != 2
	    || sizeof(uint32_t) != 4
	    || '\0' != 0)
		goto done;

	/* -1 : no DMI in /sys,
	 *  0 : DMI exist, nothing detected (HYPER_NONE)
	 * >0 : hypervisor detected
	 */
	rc = hypervisor_decode_sysfw();
	if (rc >= HYPER_NONE)
		goto done;

	/* First try EFI (ia64, Intel-based Mac) */
	switch (address_from_efi(&fp)) {
		case EFI_NOT_FOUND:
			goto memory_scan;
		case EFI_NO_SMBIOS:
			goto done;
	}

	buf = get_mem_chunk(fp, 0x20, _PATH_DEV_MEM);
	if (!buf)
		goto done;

	rc = hypervisor_decode_smbios(buf, _PATH_DEV_MEM);
	if (rc >= HYPER_NONE)
		goto done;

	free(buf);
	buf = NULL;
memory_scan:
#if defined(__x86_64__) || defined(__i386__)
	/* Fallback to memory scan (x86, x86_64) */
	buf = get_mem_chunk(0xF0000, 0x10000, _PATH_DEV_MEM);
	if (!buf)
		goto done;

	for (fp = 0; fp <= 0xFFF0; fp += 16) {
		if (memcmp(buf + fp, "_SM_", 4) == 0 && fp <= 0xFFE0) {
			rc = hypervisor_decode_smbios(buf + fp, _PATH_DEV_MEM);
			if (rc < 0)
				fp += 16;

		} else if (memcmp(buf + fp, "_DMI_", 5) == 0)
			rc = hypervisor_decode_legacy(buf + fp, _PATH_DEV_MEM);

		if (rc >= HYPER_NONE)
			break;
	}
#endif
done:
	free(buf);
	return rc < 0 ? HYPER_NONE : rc;
}
