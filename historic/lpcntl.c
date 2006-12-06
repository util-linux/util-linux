/*
 * Simple command interface to ioctl(fd, LPSETIRQ, irq).
 * Nigel Gamble (nigel@gate.net)
 * e.g.
 * 	lpcntl /dev/lp1 7
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/lp.h>

int
main(int argc, char **argv)
{
	unsigned int irq;
	int fd;
	int ret;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <lp device> [<irq>]\n", argv[0]);
		exit(1);
	}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror(argv[1]);
		exit(1);
	}

	if (argc == 2) {
		irq = ioctl(fd, LPGETIRQ);
		if (irq == -1) {
			perror(argv[1]);
			exit(1);
		}
		if (irq)
			printf("%s using IRQ %d\n", argv[1], irq);
		else
			printf("%s using polling\n", argv[1]);
	} else {
		irq = atoi(argv[2]);
		ret = ioctl(fd, LPSETIRQ, irq);
		if (ret == -1) {
			if (errno == EPERM)
				fprintf(stderr, "%s: only super-user can change the IRQ\n", argv[0]);
			else
				perror(argv[1]);
			exit(1);
		}
	}

	return 0;
}
