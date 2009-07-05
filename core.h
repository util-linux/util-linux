#ifndef __CORE
#define __CORE

#include "rfkill.h"

extern const char rfkill_version[];

/*
 * first version of event size,
 * members idx, type, op, soft, hard
 */
#ifndef RFKILL_EVENT_SIZE_V1
#define RFKILL_EVENT_SIZE_V1	8
#endif

#endif
