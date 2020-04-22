
#include "smartcolsP.h"
#include "mbsalign.h"

/* This is private struct to work with output data */
struct libscols_buffer {
	char	*begin;		/* begin of the buffer */
	char	*cur;		/* current end of  the buffer */
	char	*encdata;	/* encoded buffer mbs_safe_encode() */

	size_t	bufsz;		/* size of the buffer */
	size_t	art_idx;	/* begin of the tree ascii art or zero */
};

struct libscols_buffer *new_buffer(size_t sz)
{
	struct libscols_buffer *buf = malloc(sz + sizeof(struct libscols_buffer));

	if (!buf)
		return NULL;

	buf->cur = buf->begin = ((char *) buf) + sizeof(struct libscols_buffer);
	buf->encdata = NULL;
	buf->bufsz = sz;

	DBG(BUFF, ul_debugobj(buf, "alloc (size=%zu)", sz));
	return buf;
}

void free_buffer(struct libscols_buffer *buf)
{
	if (!buf)
		return;
	DBG(BUFF, ul_debugobj(buf, "dealloc"));
	free(buf->encdata);
	free(buf);
}

int buffer_reset_data(struct libscols_buffer *buf)
{
	if (!buf)
		return -EINVAL;

	/*DBG(BUFF, ul_debugobj(buf, "reset data"));*/
	buf->begin[0] = '\0';
	buf->cur = buf->begin;
	buf->art_idx = 0;
	return 0;
}

int buffer_append_data(struct libscols_buffer *buf, const char *str)
{
	size_t maxsz, sz;

	if (!buf)
		return -EINVAL;
	if (!str || !*str)
		return 0;

	sz = strlen(str);
	maxsz = buf->bufsz - (buf->cur - buf->begin);

	if (maxsz <= sz)
		return -EINVAL;
	memcpy(buf->cur, str, sz + 1);
	buf->cur += sz;
	return 0;
}

int buffer_append_ntimes(struct libscols_buffer *buf, size_t n, const char *str)
{
	size_t i;

	for (i = 0; i < n; i++) {
		int rc = buffer_append_data(buf, str);
		if (rc)
			return rc;
	}
	return 0;
}

int buffer_set_data(struct libscols_buffer *buf, const char *str)
{
	int rc = buffer_reset_data(buf);
	return rc ? rc : buffer_append_data(buf, str);
}

/* save the current buffer position to art_idx */
void buffer_set_art_index(struct libscols_buffer *buf)
{
	if (buf) {
		buf->art_idx = buf->cur - buf->begin;
		/*DBG(BUFF, ul_debugobj(buf, "art index: %zu", buf->art_idx));*/
	}
}

char *buffer_get_data(struct libscols_buffer *buf)
{
	return buf ? buf->begin : NULL;
}

size_t buffer_get_size(struct libscols_buffer *buf)
{
	return buf ? buf->bufsz : 0;
}

/* encode data by mbs_safe_encode() to avoid control and non-printable chars */
char *buffer_get_safe_data(struct libscols_table *tb,
				  struct libscols_buffer *buf,
				  size_t *cells,
				  const char *safechars)
{
	char *data = buffer_get_data(buf);
	char *res = NULL;

	if (!data)
		goto nothing;

	if (!buf->encdata) {
		buf->encdata = malloc(mbs_safe_encode_size(buf->bufsz) + 1);
		if (!buf->encdata)
			goto nothing;
	}

	if (scols_table_is_noencoding(tb)) {
		*cells = mbs_width(data);
		strcpy(buf->encdata, data);
		res = buf->encdata;
	} else {
		res = mbs_safe_encode_to_buffer(data, cells, buf->encdata, safechars);
	}

	if (!res || !*cells || *cells == (size_t) -1)
		goto nothing;
	return res;
nothing:
	*cells = 0;
	return NULL;
}

/* returns size in bytes of the ascii art (according to art_idx) in safe encoding */
size_t buffer_get_safe_art_size(struct libscols_buffer *buf)
{
	char *data = buffer_get_data(buf);
	size_t bytes = 0;

	if (!data || !buf->art_idx)
		return 0;

	mbs_safe_nwidth(data, buf->art_idx, &bytes);
	return bytes;
}
