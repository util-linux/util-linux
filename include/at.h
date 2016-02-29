/*
 * wrappers for "at" functions.
 *
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_AT_H
#define UTIL_LINUX_AT_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "c.h"

extern FILE *fopen_at(int dir, const char *filename,
			int flags, const char *mode);

#endif /* UTIL_LINUX_AT_H */
