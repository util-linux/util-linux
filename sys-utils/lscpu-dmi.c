// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 FUJITSU LIMITED.  All rights reserved.
 */

#include "lscpu.h"

void to_dmi_header(struct lscpu_dmi_header *h, uint8_t *data)
{
	h->type = data[0];
	h->length = data[1];
	memcpy(&h->handle, data + 2, sizeof(h->handle));
	h->data = data;
}

char *dmi_string(const struct lscpu_dmi_header *dm, uint8_t s)
{
	char *bp = (char *)dm->data;

	if (!s || !bp)
		return NULL;

	bp += dm->length;
	while (s > 1 && *bp) {
		bp += strlen(bp);
		bp++;
		s--;
	}

	return !*bp ? NULL : bp;
}

int parse_dmi_table(uint16_t len, uint16_t num,
				uint8_t *data,
				struct dmi_info *di)
{
	uint8_t *buf = data;
	int rc = -1;
	int i = 0;

	 /* 4 is the length of an SMBIOS structure header */
	while (i < num && data + 4 <= buf + len) {
		uint8_t *next;
		struct lscpu_dmi_header h;

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
			di->vendor = dmi_string(&h, data[0x04]);
			break;
		case 1:
			di->manufacturer = dmi_string(&h, data[0x04]);
			di->product = dmi_string(&h, data[0x05]);
			break;
		default:
			break;
		}

		data = next;
		i++;
	}
	rc = 0;
done:
	return rc;
}
