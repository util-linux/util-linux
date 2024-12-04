/*
 * uuid_time.c --- Interpret the time field from a uuid.  This program
 * 	violates the UUID abstraction barrier by reaching into the guts
 *	of a UUID and interpreting it.
 *
 * Copyright (C) 1998, 1999 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0500
#include <windows.h>
#define UUID MYUUID
#endif

#include <errno.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <time.h>

#include "uuidP.h"
#include "timeutils.h"

#undef uuid_time

/* prototype to make compiler happy */
time_t __uuid_time(const uuid_t uu, struct timeval *ret_tv);

static uint64_t gregorian_to_unix(uint64_t ts)
{
	return ts - ((((uint64_t) 0x01B21DD2) << 32) + 0x13814000);
}

static void uuid_time_v1(const struct uuid *uuid, struct timeval *tv)
{
	uint32_t high;
	uint64_t clock_reg;

	high = uuid->time_mid | ((uuid->time_hi_and_version & 0xFFF) << 16);
	clock_reg = uuid->time_low | ((uint64_t) high << 32);

	clock_reg = gregorian_to_unix(clock_reg);
	tv->tv_sec = clock_reg / 10000000;
	tv->tv_usec = (clock_reg % 10000000) / 10;
}

static void uuid_time_v6(const struct uuid *uuid, struct timeval *tv)
{
	uint64_t clock_reg;

	clock_reg = uuid->time_low;
	clock_reg <<= 16;
	clock_reg |= uuid->time_mid;
	clock_reg <<= 12;
	clock_reg |= uuid->time_hi_and_version & 0xFFF;

	clock_reg = gregorian_to_unix(clock_reg);
	tv->tv_sec = clock_reg / 10000000;
	tv->tv_usec = (clock_reg % 10000000) / 10;
}

static void uuid_time_v7(const struct uuid *uuid, struct timeval *tv)
{
	uint64_t clock_reg;

	clock_reg = uuid->time_low;
	clock_reg <<= 16;
	clock_reg |= uuid->time_mid;

	tv->tv_sec = clock_reg / MSEC_PER_SEC;
	tv->tv_usec = (clock_reg % MSEC_PER_SEC) * USEC_PER_MSEC;
}

static uint8_t __uuid_type(const struct uuid *uuid)
{
	return (uuid->time_hi_and_version >> 12) & 0xF;
}

/* this function could be 32bit time_t and 32bit timeval or 64bit,
   depending on compiler flags and architecture. */
time_t __uuid_time(const uuid_t uu, struct timeval *ret_tv)
{
	struct timeval	tv;
	struct uuid	uuid;
	uint8_t		type;

	uuid_unpack(uu, &uuid);
	type = __uuid_type(&uuid);

	switch (type) {
	case UUID_TYPE_DCE_TIME:
		uuid_time_v1(&uuid, &tv);
		break;
	case UUID_TYPE_DCE_TIME_V6:
		uuid_time_v6(&uuid, &tv);
		break;
	case UUID_TYPE_DCE_TIME_V7:
		uuid_time_v7(&uuid, &tv);
		break;
	default:
		tv.tv_sec = -1;
		tv.tv_usec = -1;
		break;
	}

	if (ret_tv)
		*ret_tv = tv;

	return tv.tv_sec;
}
#if defined(__USE_TIME_BITS64) && defined(__GLIBC__)
extern time_t uuid_time64(const uuid_t uu, struct timeval *ret_tv) __attribute__((weak, alias("__uuid_time")));
#elif defined(__clang__) && defined(__APPLE__)
__asm__(".globl _uuid_time");
__asm__(".set _uuid_time, ___uuid_time");
extern time_t uuid_time(const uuid_t uu, struct timeval *ret_tv);
#else
extern time_t uuid_time(const uuid_t uu, struct timeval *ret_tv) __attribute__((weak, alias("__uuid_time")));
#endif

#if defined(__USE_TIME_BITS64) && defined(__GLIBC__)
struct timeval32
{
	int32_t tv_sec;
	int32_t tv_usec;
};

/* prototype to make compiler happy */
int32_t __uuid_time32(const uuid_t uu, struct timeval32 *ret_tv);

/* Check whether time fits in 32bit time_t.  */
static inline int
in_time32_t_range(time_t t)
{
	int32_t		s;

	s = t;

	return s == t;
}

int32_t __uuid_time32(const uuid_t uu, struct timeval32 *ret_tv)
{
	struct timeval		tv;
	time_t ret_time = __uuid_time(uu, &tv);

	if (! in_time32_t_range(ret_time)) {
		ret_tv->tv_sec = -1;
		ret_tv->tv_usec = -1;
	        errno = EOVERFLOW;
		return -1;
	}

	if (ret_tv) {
		ret_tv->tv_sec = tv.tv_sec;
		ret_tv->tv_usec = tv.tv_usec;
	}

	return tv.tv_sec;
}
extern int32_t uuid_time(const uuid_t uu, struct timeval32 *ret_tv) __attribute__((weak, alias("__uuid_time32")));
#endif

int uuid_type(const uuid_t uu)
{
	struct uuid		uuid;

	uuid_unpack(uu, &uuid);
	return __uuid_type(&uuid);
}

int uuid_variant(const uuid_t uu)
{
	struct uuid		uuid;
	int			var;

	uuid_unpack(uu, &uuid);
	var = uuid.clock_seq;

	if ((var & 0x8000) == 0)
		return UUID_VARIANT_NCS;
	if ((var & 0x4000) == 0)
		return UUID_VARIANT_DCE;
	if ((var & 0x2000) == 0)
		return UUID_VARIANT_MICROSOFT;
	return UUID_VARIANT_OTHER;
}

#ifdef DEBUG
static const char *variant_string(int variant)
{
	switch (variant) {
	case UUID_VARIANT_NCS:
		return "NCS";
	case UUID_VARIANT_DCE:
		return "DCE";
	case UUID_VARIANT_MICROSOFT:
		return "Microsoft";
	default:
		return "Other";
	}
}


int
main(int argc, char **argv)
{
	uuid_t		buf;
	time_t		time_reg;
	struct timeval	tv;
	int		type, variant;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s uuid\n", argv[0]);
		exit(1);
	}
	if (uuid_parse(argv[1], buf)) {
		fprintf(stderr, "Invalid UUID: %s\n", argv[1]);
		exit(1);
	}
	variant = uuid_variant(buf);
	type = uuid_type(buf);
	time_reg = uuid_time(buf, &tv);

	printf("UUID variant is %d (%s)\n", variant, variant_string(variant));
	if (variant != UUID_VARIANT_DCE) {
		printf("Warning: This program only knows how to interpret "
		       "DCE UUIDs.\n\tThe rest of the output is likely "
		       "to be incorrect!!\n");
	}
	printf("UUID type is %d", type);
	switch (type) {
	case 1:
		printf(" (time based)\n");
		break;
	case 2:
		printf(" (DCE)\n");
		break;
	case 3:
		printf(" (name-based)\n");
		break;
	case 4:
		printf(" (random)\n");
		break;
	default:
		printf("\n");
	}
	if (type != 1) {
		printf("Warning: not a time-based UUID, so UUID time "
		       "decoding will likely not work!\n");
	}
	printf("UUID time is: (%"PRId64", %"PRId64"): %s\n",
		(int64_t)tv.tv_sec, (int64_t)tv.tv_usec, ctime(&time_reg));

	return 0;
}
#endif
