#ifndef UTIL_LINUX_CRC64_H
#define UTIL_LINUX_CRC64_H

#include <sys/types.h>
#include <stdint.h>

extern uint64_t crc64(uint64_t seed, const unsigned char *data, size_t len);

#endif
