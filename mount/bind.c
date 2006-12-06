#include <stdio.h>
#include <sys/mount.h>

main(int argc, char **argv) {
	int ret;

	ret = mount(argv[1], argv[2], "bind", MS_MGC_VAL, NULL);
	if (ret)
		perror("bind");
	return ret;
}
