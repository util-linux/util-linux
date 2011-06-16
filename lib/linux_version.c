#include <stdio.h>
#include <sys/utsname.h>

#include "linux_version.h"

int
get_linux_version (void)
{
	static int kver = -1;
	struct utsname uts;
	int major;
	int minor;
	int teeny;

	if (kver != -1)
		return kver;
	if (uname (&uts))
		kver = 0;
	else if (sscanf (uts.release, "%d.%d.%d", &major, &minor, &teeny) == 3)
		kver = KERNEL_VERSION (major, minor, teeny);
	else if (sscanf (uts.release, "%d.%d", &major, &minor) == 2)
		kver = KERNEL_VERSION (major, minor, 0);

	return kver;
}
