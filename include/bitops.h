/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#ifndef BITOPS_H
#define BITOPS_H

#include <stdint.h>
#include <sys/param.h>

#if defined(HAVE_BYTESWAP_H)
# include <byteswap.h>
#endif

#if defined(HAVE_ENDIAN_H)
#  include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)	/* BSDs have them here */
#  include <sys/endian.h>
#endif

#if defined(__OpenBSD__)
# include <sys/types.h>
# define be16toh(x) betoh16(x)
# define be32toh(x) betoh32(x)
# define be64toh(x) betoh64(x)
#endif

/*
 * Fallbacks
 */
#ifndef bswap_16
# define bswap_16(x)   ((((x) & 0x00FF) << 8) | \
			(((x) & 0xFF00) >> 8))
#endif

#ifndef bswap_32
# define bswap_32(x)   ((((x) & 0x000000FF) << 24) | \
			(((x) & 0x0000FF00) << 8)  | \
			(((x) & 0x00FF0000) >> 8)  | \
			(((x) & 0xFF000000) >> 24))
#endif

#ifndef bswap_64
# define bswap_64(x) ((((x) & 0x00000000000000FFULL) << 56) | \
                      (((x) & 0x000000000000FF00ULL) << 40) | \
                      (((x) & 0x0000000000FF0000ULL) << 24) | \
                      (((x) & 0x00000000FF000000ULL) << 8)  | \
                      (((x) & 0x000000FF00000000ULL) >> 8)  | \
                      (((x) & 0x0000FF0000000000ULL) >> 24) | \
                      (((x) & 0x00FF000000000000ULL) >> 40) | \
                      (((x) & 0xFF00000000000000ULL) >> 56))
#endif

#ifndef htobe16
# if !defined(WORDS_BIGENDIAN)
#  define htobe16(x) bswap_16 (x)
#  define htole16(x) (x)
#  define be16toh(x) bswap_16 (x)
#  define le16toh(x) (x)
#  define htobe32(x) bswap_32 (x)
#  define htole32(x) (x)
#  define be32toh(x) bswap_32 (x)
#  define le32toh(x) (x)
#  define htobe64(x) bswap_64 (x)
#  define htole64(x) (x)
#  define be64toh(x) bswap_64 (x)
#  define le64toh(x) (x)
# else
#  define htobe16(x) (x)
#  define htole16(x) bswap_16 (x)
#  define be16toh(x) (x)
#  define le16toh(x) bswap_16 (x)
#  define htobe32(x) (x)
#  define htole32(x) bswap_32 (x)
#  define be32toh(x) (x)
#  define le32toh(x) bswap_32 (x)
#  define htobe64(x) (x)
#  define htole64(x) bswap_64 (x)
#  define be64toh(x) (x)
#  define le64toh(x) bswap_64 (x)
# endif
#endif

/*
 * Byte swab macros (based on linux/byteorder/swab.h)
 */
#define swab16(x) bswap_16(x)
#define swab32(x) bswap_32(x)
#define swab64(x) bswap_64(x)

#define cpu_to_le16(x) ((uint16_t) htole16(x))
#define cpu_to_le32(x) ((uint32_t) htole32(x))
#define cpu_to_le64(x) ((uint64_t) htole64(x))

#define cpu_to_be16(x) ((uint16_t) htobe16(x))
#define cpu_to_be32(x) ((uint32_t) htobe32(x))
#define cpu_to_be64(x) ((uint64_t) htobe64(x))

#define le16_to_cpu(x) ((uint16_t) le16toh(x))
#define le32_to_cpu(x) ((uint32_t) le32toh(x))
#define le64_to_cpu(x) ((uint64_t) le64toh(x))

#define be16_to_cpu(x) ((uint16_t) be16toh(x))
#define be32_to_cpu(x) ((uint32_t) be32toh(x))
#define be64_to_cpu(x) ((uint64_t) be64toh(x))

/*
 * Bit map related macros. Usually provided by libc.
 */
#ifndef NBBY
# define NBBY            CHAR_BIT
#endif

#ifndef setbit
# define setbit(a,i)	((a)[(i)/NBBY] |= 1<<((i)%NBBY))
# define clrbit(a,i)	((a)[(i)/NBBY] &= ~(1<<((i)%NBBY)))
# define isset(a,i)	((a)[(i)/NBBY] & (1<<((i)%NBBY)))
# define isclr(a,i)	(((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)
#endif

#endif /* BITOPS_H */

