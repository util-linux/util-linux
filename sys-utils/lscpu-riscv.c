/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 */
#include "lscpu.h"
#include "strutils.h"
#include "strv.h"
#include "cctype.h"

static int riscv_cmp_func(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

bool is_riscv(struct lscpu_cputype *ct)
{
	const char *base_isa[] = {"rv32", "rv64", "rv128"};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(base_isa); i++) {
		if (!c_strncasecmp(ct->isa, base_isa[i], strlen(base_isa[i])))
			return true;
	}

	return false;
}

/*
 * Reformat the isa string, but the length stays the same.
 */
void lscpu_format_isa_riscv(struct lscpu_cputype *ct)
{
	char **split;
	size_t i;

	split = strv_split(ct->isa, "_");

	/* Sort multi-letter extensions alphabetically */
	if (strv_length(split) > 1)
		qsort(&split[1], strv_length(split) - 1, sizeof(char *), riscv_cmp_func);

	/* Keep Base ISA and single-letter extensions at the start */
	strcpy(ct->isa, split[0]);

	for (i = 1; i < strv_length(split); i++) {
		strcat(ct->isa, " ");
		strcat(ct->isa, split[i]);
	}

	strv_free(split);
}
