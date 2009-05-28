/*
 * rfkill userspace tool
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#include "rfkill.h"
#include "core.h"

static void version(void)
{
	printf("rfkill %s\n", rfkill_version);
}

int main(int argc, char **argv)
{
	if (argc > 0 && strcmp(*argv, "--version") == 0) {
		version();
		return 0;
	}

	return 0;
}
