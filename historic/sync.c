/*
 * sync.c - flush Linux filesystem buffers
 *
 * Copyright 1992 Linus Torvalds.
 * This file may be redistributed as per the GNU copyright.
 */

#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	sync();
	return 0;
}
