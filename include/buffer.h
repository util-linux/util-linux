#ifndef UTIL_LINUX_BUFFER
#define UTIL_LINUX_BUFFER

#include "c.h"

struct ul_buffer {
	char *begin;		/* begin of the data */
	char *end;		/* current end of data */

	size_t sz;		/* allocated space for data */
	size_t chunksize;
};

#define UL_INIT_BUFFER { .begin = NULL }

void ul_buffer_reset_data(struct ul_buffer *buf);
void ul_buffer_free_data(struct ul_buffer *buf);
int ul_buffer_is_empty(struct ul_buffer *buf);
void ul_buffer_set_chunksize(struct ul_buffer *buf, size_t sz);
void ul_buffer_refer_string(struct ul_buffer *buf, char *str);
int ul_buffer_alloc_data(struct ul_buffer *buf, size_t sz);
int ul_buffer_append_data(struct ul_buffer *buf, const char *data, size_t sz);
int ul_buffer_append_string(struct ul_buffer *buf, const char *str);
int ul_buffer_append_ntimes(struct ul_buffer *buf, size_t n, const char *str);
int ul_buffer_set_data(struct ul_buffer *buf, const char *data, size_t sz);
char *ul_buffer_get_data(struct ul_buffer *buf);

#endif /* UTIL_LINUX_BUFFER */
