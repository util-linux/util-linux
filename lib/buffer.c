/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#include "buffer.h"

void ul_buffer_reset_data(struct ul_buffer *buf)
{
	if (buf->begin)
		buf->begin[0] = '\0';
	buf->end = buf->begin;
}

void ul_buffer_free_data(struct ul_buffer *buf)
{
	assert(buf);

	free(buf->begin);
	buf->begin = NULL;
	buf->end = NULL;
	buf->sz = 0;
}

void ul_buffer_set_chunksize(struct ul_buffer *buf, size_t sz)
{
	buf->chunksize = sz;
}

int ul_buffer_is_empty(struct ul_buffer *buf)
{
	return buf->begin == buf->end;
}

void ul_buffer_refer_string(struct ul_buffer *buf, char *str)
{
	if (buf->sz)
		ul_buffer_free_data(buf);
	buf->begin = str;
	buf->sz = str ? strlen(str) : 0;
	buf->end = buf->begin ? buf->begin + buf->sz : buf->begin;
}

int ul_buffer_alloc_data(struct ul_buffer *buf, size_t sz)
{
	char *tmp;
	size_t len = 0;

	assert(buf);

	if (sz <= buf->sz)
		return 0;

	if (buf->end && buf->begin)
		len = buf->end - buf->begin;

	if (buf->chunksize)
		sz = ((sz + buf->chunksize) / buf->chunksize) * buf->chunksize + 1;

	tmp = realloc(buf->begin, sz);
	if (!tmp)
		return -ENOMEM;

	buf->begin = tmp;
	buf->end = buf->begin + len;
	buf->sz = sz;

	return 0;
}

int ul_buffer_append_data(struct ul_buffer *buf, const char *data, size_t sz)
{
	size_t maxsz = 0;

	if (!buf)
		return -EINVAL;
	if (!data || !*data)
		return 0;

	if (buf->begin && buf->end)
		maxsz = buf->sz - (buf->end - buf->begin);

	if (maxsz <= sz + 1) {
		int rc = ul_buffer_alloc_data(buf, buf->sz + sz + 1);
		if (rc)
			return rc;
	}
	if (!buf->end)
		return -EINVAL;	/* make static analyzers happy */

	memcpy(buf->end, data, sz);
	buf->end += sz;
	*buf->end = '\0';	/* make sure it's terminated */
	return 0;
}

int ul_buffer_append_string(struct ul_buffer *buf, const char *str)
{
	return ul_buffer_append_data(buf, str, strlen(str));
}

int ul_buffer_append_ntimes(struct ul_buffer *buf, size_t n, const char *str)
{
	size_t i;
	size_t len = strlen(str);

	for (i = 0; len && i < n; i++) {
		int rc = ul_buffer_append_data(buf, str, len);
		if (rc)
			return rc;
	}
	return 0;
}

int ul_buffer_set_data(struct ul_buffer *buf, const char *data, size_t sz)
{
	ul_buffer_reset_data(buf);
	return ul_buffer_append_data(buf, data, sz);
}

char *ul_buffer_get_data(struct ul_buffer *buf)
{
	return buf->begin;
}

#ifdef TEST_PROGRAM_BUFFER
int main(void)
{
	struct ul_buffer buf = UL_INIT_BUFFER;
	char *str;

	ul_buffer_set_chunksize(&buf, 16);

	ul_buffer_append_string(&buf, "AAA");
	ul_buffer_append_data(&buf, "=", 1);
	ul_buffer_append_string(&buf, "aaa");
	ul_buffer_append_data(&buf, ",", 1);
	ul_buffer_append_string(&buf, "BBB");
	ul_buffer_append_string(&buf, "=");
	ul_buffer_append_string(&buf, "bbb");

	str = ul_buffer_get_data(&buf);
	printf("data '%s'\n", str);

	ul_buffer_reset_data(&buf);
	ul_buffer_append_string(&buf, "This is really long string to test the buffer function.");
	ul_buffer_append_string(&buf, " YES!");
	str = ul_buffer_get_data(&buf);
	printf("data '%s'\n", str);

	ul_buffer_free_data(&buf);
	str = strdup("foo");
	ul_buffer_refer_string(&buf, str);
	ul_buffer_append_data(&buf, ",", 1);
	ul_buffer_append_string(&buf, "bar");
	str = ul_buffer_get_data(&buf);
	printf("data '%s'\n", str);

	ul_buffer_free_data(&buf);
}
#endif /* TEST_PROGRAM_BUFFER */
