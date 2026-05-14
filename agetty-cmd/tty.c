#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include "agetty.h"

struct Speedtab {
	long speed;
	speed_t code;
};

static const struct Speedtab speedtab[] = {
	{50, B50},
	{75, B75},
	{110, B110},
	{134, B134},
	{150, B150},
	{200, B200},
	{300, B300},
	{600, B600},
	{1200, B1200},
	{1800, B1800},
	{2400, B2400},
	{4800, B4800},
	{9600, B9600},
#ifdef B19200
	{19200, B19200},
#elif defined(EXTA)
	{19200, EXTA},
#endif
#ifdef B38400
	{38400, B38400},
#elif defined(EXTB)
	{38400, EXTB},
#endif
#ifdef B57600
	{57600, B57600},
#endif
#ifdef B115200
	{115200, B115200},
#endif
#ifdef B230400
	{230400, B230400},
#endif
#ifdef B460800
	{460800, B460800},
#endif
#ifdef B500000
	{500000, B500000},
#endif
#ifdef B576000
	{576000, B576000},
#endif
#ifdef B921600
	{921600, B921600},
#endif
#ifdef B1000000
	{1000000, B1000000},
#endif
#ifdef B1152000
	{1152000, B1152000},
#endif
#ifdef B1500000
	{1500000, B1500000},
#endif
#ifdef B2000000
	{2000000, B2000000},
#endif
#ifdef B2500000
	{2500000, B2500000},
#endif
#ifdef B3000000
	{3000000, B3000000},
#endif
#ifdef B3500000
	{3500000, B3500000},
#endif
#ifdef B4000000
	{4000000, B4000000},
#endif
	{0, 0},
};

speed_t agetty_bcode(char *s)
{
	const struct Speedtab *sp;
	char *end = NULL;
	long speed;

	errno = 0;
	speed = strtol(s, &end, 10);

	if (errno || !end || end == s)
		return 0;

	for (sp = speedtab; sp->speed; sp++)
		if (sp->speed == speed)
			return sp->code;
	return 0;
}

void agetty_list_speeds(void)
{
	const struct Speedtab *sp;

	for (sp = speedtab; sp->speed; sp++)
		printf("%10ld\n", sp->speed);
}

void agetty_fprint_speed(FILE *out, speed_t speed)
{
	int i;

	for (i = 0; speedtab[i].speed; i++) {
		if (speedtab[i].code == speed) {
			fprintf(out, "%ld", speedtab[i].speed);
			break;
		}
	}
}
