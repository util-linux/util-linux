#include <stdio.h>
#include <sys/utsname.h>
#include <inttypes.h>
#include <stdint.h>

#include "c.h"
#include "linux_version.h"

uint32_t get_linux_version (void)
{
	static uint32_t kver = UINT32_MAX;
	struct utsname uts;
	/* The KERNEL_VERSION macro makes assumption these are 8 bit
	 * variables, see bit shift in linux/version.h system header */
	uint8_t x = 0, y = 0, z = 0;
	int n;

	if (kver != UINT32_MAX)
		return kver;
	if (uname(&uts))
		return kver = 0;

	/*
	 * uts.release values come from linux kernel Makefile where
	 *   VERSION is x
	 *   PATCHLEVEL is y
	 *   SUBLEVEL and is z in here
	 */
	n = sscanf(uts.release, "%" SCNu8 ".%" SCNu8 ".%" SCNu8, &x, &y, &z);
	if (n < 1 || n > 3)
		return kver = 0;

	return kver = KERNEL_VERSION(x, y, z);
}

#ifdef TEST_PROGRAM_LINUXVERSION
# include <stdlib.h>
int main(int argc, char *argv[])
{
	int rc = EXIT_FAILURE;

	if (argc == 1) {
		printf("Linux version: %d\n", get_linux_version());
		rc = EXIT_SUCCESS;

	} else if (argc == 5) {
		const char *oper = argv[1];

		int x = atoi(argv[2]),
		    y = atoi(argv[3]),
		    z = atoi(argv[4]);
		int kver = get_linux_version();
		int uver = KERNEL_VERSION(x, y, z);

		if (strcmp(oper, "==") == 0)
			rc = kver == uver;
		else if (strcmp(oper, "<=") == 0)
			rc = kver <= uver;
		else if (strcmp(oper, ">=") == 0)
			rc = kver >= uver;
		else
			errx(EXIT_FAILURE, "unsupported operator");

		if (rc)
			printf("match\n");
		else
			printf("not-match [%d %s %d, x.y.z: %d.%d.%d]\n",
					kver, oper, uver, x, y, z);

		rc = rc ? EXIT_SUCCESS : EXIT_FAILURE;

	} else
		 fprintf(stderr, "Usage:\n"
				 "   %s [<oper> <x> <y> <z>]\n"
				 "supported operators:\n"
				 "   ==, <=, >=\n",
				 program_invocation_short_name);

	return rc;
}
#endif
