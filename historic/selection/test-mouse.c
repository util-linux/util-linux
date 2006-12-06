/*
 * test-mouse: exercise rodent to test compatibility.
 * Any button to draw asterisks of different
 * colour. Left and right buttons (while mouse is stationary) to quit.
 * Andrew Haylett, 17th June 1993
 */

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "mouse.h"

#define SCALE	10

int
main(int argc, char *argv[])
{
    struct ms_event ev;
    struct winsize win;

    ms_params(argc, argv);
    ioctl(fileno(stdout), TIOCGWINSZ, &win);
    if (! win.ws_col || ! win.ws_row)
    {
    	fprintf(stderr, "selection: zero screen dimension: assuming 80x25.\n");
    	win.ws_col = 80;
    	win.ws_row = 25;
    }

    printf("\033[2J\033[%d;%dH", win.ws_row / 2, win.ws_col / 2);
    fflush(stdout);
    if (ms_init((win.ws_col + 1) * SCALE - 1, (win.ws_row + 1) * SCALE - 1))
    {
    	perror("ms_init");
    	exit(1);
    }
    while (1)
    {
    	if (get_ms_event(&ev))
    	{
	    perror("get_ms_event");
    	    exit(1);
	}
	if (ev.ev_code == MS_BUTDOWN && ev.ev_butstate == (MS_BUTLEFT | MS_BUTRIGHT))
	{
	    printf("\033[;H\033[2J\033[m");
	    exit(0);
	}
	else if (ev.ev_code == MS_MOVE || ev.ev_code == MS_DRAG)
	{
	    printf("\033[%d;%dH", ev.ev_y / SCALE, ev.ev_x / SCALE);
	    if (ev.ev_code == MS_DRAG)
	    {
	    	if (ev.ev_butstate == MS_BUTLEFT)
		    printf("\033[31m*\033[D");	/* red */
		else if (ev.ev_butstate == MS_BUTRIGHT)
		    printf("\033[35m*\033[D");	/* purple */
		else
		    printf("\033[34m*\033[D");	/* blue */
	    }
	}
	fflush(stdout);
    }
}
