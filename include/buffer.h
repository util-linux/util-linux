/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_BUFFER
#define UTIL_LINUX_BUFFER

#include <stddef.h>
#include <stdarg.h>

struct ul_buffer {
	char *begin;		/* begin of the data */
	char *end;		/* current end of data */

	size_t sz;		/* allocated space for data */
	size_t chunksize;

	char *encoded;		/* encoded data (from mbs_safe_encode_to_buffer)) */
	size_t encoded_sz;	/* space allocated for encoded data */

	char **ptrs;		/* saved pointers */
	size_t nptrs;		/* number of saved pointers */
};

#define UL_INIT_BUFFER { .begin = NULL }

void ul_buffer_reset_data(struct ul_buffer *buf);
void ul_buffer_free_data(struct ul_buffer *buf);

/*
 * This is a variant of ul_buffer_free_data(). Instead of freeing the
 * BEGIN member, this function transfers its ownership to the caller.
 *
 * If the BEGIN member points to an empty C string,
 * ul_buffer_steal_string() frees the buffer and returns NULL.
 */
char *ul_buffer_steal_string(struct ul_buffer *buf);

int ul_buffer_is_empty(struct ul_buffer *buf);
void ul_buffer_set_chunksize(struct ul_buffer *buf, size_t sz);
void ul_buffer_refer_string(struct ul_buffer *buf, char *str);
int ul_buffer_alloc_data(struct ul_buffer *buf, size_t sz);
int ul_buffer_append_data(struct ul_buffer *buf, const char *data, size_t sz);
int ul_buffer_append_char(struct ul_buffer *buf, const char c);
int ul_buffer_append_string(struct ul_buffer *buf, const char *str);
int ul_buffer_append_ntimes(struct ul_buffer *buf, size_t n, const char *str);
int ul_buffer_appendf(struct ul_buffer *buf, const char *format, ...)
	__attribute__ ((__format__ (__printf__, 2, 3)));
int ul_buffer_appendvf(struct ul_buffer *buf, const char *format, va_list ap)
	__attribute__ ((__format__ (__printf__, 2, 0)));
int ul_buffer_set_data(struct ul_buffer *buf, const char *data, size_t sz);

char *ul_buffer_get_data(struct ul_buffer *buf,  size_t *sz, size_t *width);
char *ul_buffer_get_string(struct ul_buffer *buf,  size_t *sz, size_t *width);
char *ul_buffer_get_safe_data(struct ul_buffer *buf, size_t *sz, size_t *width, const char *safechars);

size_t ul_buffer_get_bufsiz(struct ul_buffer *buf);
size_t ul_buffer_get_datasiz(struct ul_buffer *buf);

int ul_buffer_save_pointer(struct ul_buffer *buf, unsigned short ptr_idx);
char *ul_buffer_get_pointer(struct ul_buffer *buf, unsigned short  ptr_idx);
size_t ul_buffer_get_pointer_length(struct ul_buffer *buf, unsigned short ptr_idx);
size_t ul_buffer_get_safe_pointer_width(struct ul_buffer *buf, unsigned short ptr_idx);

/*
 * Include xalloc.h first to use the buffer_x*() functions.
 */
#ifdef XALLOC_EXIT_CODE
static inline int ul_buffer_xappend_string(struct ul_buffer *buf, const char *str)
{
	int r = ul_buffer_append_string(buf, str);
	if (r < 0)
		err(XALLOC_EXIT_CODE, "cannot allocate buffer");
	return r;
}

static inline int ul_buffer_xappend_char(struct ul_buffer *buf, const char c)
{
	int r = ul_buffer_append_char(buf, c);
	if (r < 0)
		err(XALLOC_EXIT_CODE, "cannot allocate buffer");
	return r;
}

static inline
__attribute__ ((__format__ (__printf__, 2, 0)))
int ul_buffer_xappendvf(struct ul_buffer *buf, const char *format, va_list ap)
{
	int r = ul_buffer_appendvf(buf, format, ap);
	if (r < 0)
		err(XALLOC_EXIT_CODE, "cannot allocate buffer");
	return r;
}

static inline
__attribute__ ((__format__ (__printf__, 2, 3)))
int ul_buffer_xappendf(struct ul_buffer *buf, const char *format, ...)
{
	va_list ap;
	int res;

	va_start(ap, format);
	res = ul_buffer_xappendvf(buf, format, ap);
	va_end(ap);

	return res;
}
#endif/* XALLOC_EXIT_CODE */

#endif /* UTIL_LINUX_BUFFER */
