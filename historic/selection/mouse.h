/* interface file for mouse driver */
/* Andrew Haylett, 17th June 1993 */

#ifndef MOUSE_H
#define MOUSE_H

#define MS_BUTLEFT	4
#define MS_BUTMIDDLE	2
#define MS_BUTRIGHT	1

typedef enum {
    P_MS = 0,
    P_SUN = 1,
    P_MSC = 2,
    P_MM = 3,
    P_LOGI = 4,
    P_BM = 5,
    P_PS2 = 6
} mouse_type;

#define NR_TYPES 7	/* keep in step with mouse_type! */

struct ms_event {
    enum { MS_NONE, MS_BUTUP, MS_BUTDOWN, MS_MOVE, MS_DRAG } ev_code;
    char ev_butstate;
    int ev_x, ev_y;
    int ev_dx, ev_dy;
};

void ms_params(int argc, char *argv[]);
int ms_init(const int maxx, const int maxy);
int get_ms_event(struct ms_event *ev);

#endif /* MOUSE_H */
