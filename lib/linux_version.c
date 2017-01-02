#include <stdio.h>
#include <sys/utsname.h>

#include "c.h"
#include "linux_version.h"

int get_linux_version (void)
{
	static int kver = -1;
	struct utsname uts;
	int x = 0, y = 0, z = 0;
	int n;

	if (kver != -1)
		return kver;
	if (uname(&uts))
		return kver = 0;

	n = sscanf(uts.release, "%d.%d.%d", &x, &y, &z);
	if (n < 1 || n > 3)
		return kver = 0;

	return kver = KERNEL_VERSION(x, y, z);
}
