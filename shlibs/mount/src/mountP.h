/*
 * mountP.h - private library header file
 *
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#ifndef _LIBMOUNT_PRIVATE_H
#define _LIBMOUNT_PRIVATE_H

/* features */
#define CONFIG_LIBMOUNT_ASSERT

#ifdef CONFIG_LIBMOUNT_ASSERT
#include <assert.h>
#endif

#include "mount.h"
#endif
