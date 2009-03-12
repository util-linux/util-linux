#ifndef PTTYPE_H
#define PTTYPE_H

/*
 * Note that this is a temporary solution. The final solution will be to move
 * libdisk from xfsprogs to util-linux-ng.
 */
extern const char *get_pt_type(const char *device);
extern const char *get_pt_type_fd(int fd);

#endif
