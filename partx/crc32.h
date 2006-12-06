/*
 * crc32.h
 */
#ifndef _CRC32_H
#define _CRC32_H

#include <inttypes.h>
#include <stdlib.h>

extern int init_crc32(void);
extern void cleanup_crc32(void);
extern uint32_t  crc32_le(uint32_t crc, unsigned char const *p, size_t len);
extern uint32_t  crc32_be(uint32_t crc, unsigned char const *p, size_t len);

#define crc32(seed, data, length)  crc32_le(seed, (unsigned char const *)data, length)
#define ether_crc_le(length, data) crc32_le(~0, data, length)
#define ether_crc(length, data)    crc32_be(~0, data, length)

#endif /* _CRC32_H */
