#ifndef UTIL_LINUX_FUZZ_H
#define UTIL_LINUX_FUZZ_H

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#endif /* UTIL_LINUX_FUZZ_H */
