
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

time_t
time(time_t *t)
{
	time_t tt = 0;
	char *e = getenv("TEST_TIME");

	if (e && isdigit((unsigned char) *e))
		tt = atol(e);
	else {
		struct timeval tv;

		if (gettimeofday(&tv, NULL) == 0)
			tt = tv.tv_sec;
	}
	if (t)
		*t = tt;

	return tt;
}
