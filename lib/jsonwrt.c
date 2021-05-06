/*
 * JSON output formatting functions.
 *
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#include <stdio.h>
#include <inttypes.h>

#include "c.h"
#include "carefulputc.h"
#include "jsonwrt.h"


void ul_jsonwrt_init(struct ul_jsonwrt *fmt, FILE *out, int indent)
{
	fmt->out = out;
	fmt->indent = indent;
	fmt->after_close = 0;
}

void ul_jsonwrt_indent(struct ul_jsonwrt *fmt)
{
	int i;

	for (i = 0; i < fmt->indent; i++)
		fputs("   ", fmt->out);
}

void ul_jsonwrt_open(struct ul_jsonwrt *fmt, const char *name, int type)
{
	if (name) {
		if (fmt->after_close)
			fputs(",\n", fmt->out);
		ul_jsonwrt_indent(fmt);
		fputs_quoted_json_lower(name, fmt->out);
	} else {
		if (fmt->after_close)
			fputs(",", fmt->out);
		else
			ul_jsonwrt_indent(fmt);
	}

	switch (type) {
	case UL_JSON_OBJECT:
		fputs(name ? ": {\n" : "{\n", fmt->out);
		fmt->indent++;
		break;
	case UL_JSON_ARRAY:
		fputs(name ? ": [\n" : "[\n", fmt->out);
		fmt->indent++;
		break;
	case UL_JSON_VALUE:
		fputs(name ? ": " : " ", fmt->out);
		break;
	}
	fmt->after_close = 0;
}

void ul_jsonwrt_close(struct ul_jsonwrt *fmt, int type)
{
	if (fmt->indent == 1) {
		fputs("\n}\n", fmt->out);
		fmt->indent--;
		fmt->after_close = 1;
		return;
	}
	assert(fmt->indent > 0);

	switch (type) {
	case UL_JSON_OBJECT:
		fmt->indent--;
		fputc('\n', fmt->out);
		ul_jsonwrt_indent(fmt);
		fputs("}", fmt->out);
		break;
	case UL_JSON_ARRAY:
		fmt->indent--;
		fputc('\n', fmt->out);
		ul_jsonwrt_indent(fmt);
		fputs("]", fmt->out);
		break;
	case UL_JSON_VALUE:
		break;
	}

	fmt->after_close = 1;
}

void ul_jsonwrt_value_raw(struct ul_jsonwrt *fmt,
			const char *name, const char *data)
{
	ul_jsonwrt_value_open(fmt, name);
	if (data && *data)
		fputs(data, fmt->out);
	else
		fputs("null", fmt->out);
	ul_jsonwrt_value_close(fmt);
}

void ul_jsonwrt_value_s(struct ul_jsonwrt *fmt,
			const char *name, const char *data)
{
	ul_jsonwrt_value_open(fmt, name);
	if (data && *data)
		fputs_quoted_json(data, fmt->out);
	else
		fputs("null", fmt->out);
	ul_jsonwrt_value_close(fmt);
}

void ul_jsonwrt_value_u64(struct ul_jsonwrt *fmt,
			const char *name, uint64_t data)
{
	ul_jsonwrt_value_open(fmt, name);
	fprintf(fmt->out, "%"PRIu64, data);
	ul_jsonwrt_value_close(fmt);
}

void ul_jsonwrt_value_boolean(struct ul_jsonwrt *fmt,
			const char *name, int data)
{
	ul_jsonwrt_value_open(fmt, name);
	fputs(data ? "true" : "false", fmt->out);
	ul_jsonwrt_value_close(fmt);
}
