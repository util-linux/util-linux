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
}

void ul_jsonwrt_indent(struct ul_jsonwrt *fmt)
{
	int i;

	for (i = 0; i < fmt->indent; i++)
		fputs("   ", fmt->out);
}

void ul_jsonwrt_open(struct ul_jsonwrt *fmt, const char *name, int type)
{
	if (fmt->postponed_break && !name)
		;
	else {
		ul_jsonwrt_indent(fmt);
		if (name)
			fputs_quoted_json_lower(name, fmt->out);
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
	fmt->postponed_break = 0;
}

void ul_jsonwrt_close(struct ul_jsonwrt *fmt, int type, int islast)
{
	if (fmt->indent == 0) {
		fputs("}\n", fmt->out);
		fmt->indent--;
		return;
	}
	assert(fmt->indent > 0);

	switch (type) {
	case UL_JSON_OBJECT:
		fmt->indent--;
		ul_jsonwrt_indent(fmt);
		fputs(islast ? "}" : "},", fmt->out);
		break;
	case UL_JSON_ARRAY:
		fmt->indent--;
		ul_jsonwrt_indent(fmt);
		fputs(islast ? "]" : "],", fmt->out);
		break;
	case UL_JSON_VALUE:
		if (!islast)
			fputc(',', fmt->out);
		break;
	}

	if (!islast && (type == UL_JSON_OBJECT || type == UL_JSON_ARRAY))
		fmt->postponed_break = 1;
	else {
		fputc('\n', fmt->out);
		fmt->postponed_break = 0;
	}
}

void ul_jsonwrt_value_raw(struct ul_jsonwrt *fmt,
			const char *name, const char *data, int islast)
{
	ul_jsonwrt_value_open(fmt, name);
	if (data && *data)
		fputs(data, fmt->out);
	else
		fputs("null", fmt->out);
	ul_jsonwrt_value_close(fmt, islast);
}

void ul_jsonwrt_value_s(struct ul_jsonwrt *fmt,
			const char *name, const char *data, int islast)
{
	ul_jsonwrt_value_open(fmt, name);
	if (data && *data)
		fputs_quoted_json(data, fmt->out);
	else
		fputs("null", fmt->out);
	ul_jsonwrt_value_close(fmt, islast);
}

void ul_jsonwrt_value_u64(struct ul_jsonwrt *fmt,
			const char *name, uint64_t data, int islast)
{
	ul_jsonwrt_value_open(fmt, name);
	fprintf(fmt->out, "%"PRIu64, data);
	ul_jsonwrt_value_close(fmt, islast);
}

void ul_jsonwrt_value_boolean(struct ul_jsonwrt *fmt,
			const char *name, int data, int islast)
{
	ul_jsonwrt_value_open(fmt, name);
	fputs(data ? "true" : "false", fmt->out);
	ul_jsonwrt_value_close(fmt, islast);
}

