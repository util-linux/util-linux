#include "carefulputc.h"
#include "smartcolsP.h"

void fput_indent(struct libscols_table *tb)
{
	int i;

	for (i = 0; i <= tb->indent; i++)
		fputs("   ", tb->out);
}

void fput_table_open(struct libscols_table *tb)
{
	tb->indent = 0;

	if (scols_table_is_json(tb)) {
		fputc('{', tb->out);
		fputs(linesep(tb), tb->out);

		fput_indent(tb);
		fputs_quoted(tb->name, tb->out);
		fputs(": [", tb->out);
		fputs(linesep(tb), tb->out);

		tb->indent++;
		tb->indent_last_sep = 1;
	}
}

void fput_table_close(struct libscols_table *tb)
{
	tb->indent--;

	if (scols_table_is_json(tb)) {
		fput_indent(tb);
		fputc(']', tb->out);
		tb->indent--;
		fputs(linesep(tb), tb->out);
		fputc('}', tb->out);
		tb->indent_last_sep = 1;
	}
}

void fput_children_open(struct libscols_table *tb)
{
	if (scols_table_is_json(tb)) {
		fputc(',', tb->out);
		fputs(linesep(tb), tb->out);
		fput_indent(tb);
		fputs("\"children\": [", tb->out);
	}
	/* between parent and child is separator */
	fputs(linesep(tb), tb->out);
	tb->indent_last_sep = 1;
	tb->indent++;
	tb->termlines_used++;
}

void fput_children_close(struct libscols_table *tb)
{
	tb->indent--;

	if (scols_table_is_json(tb)) {
		fput_indent(tb);
		fputc(']', tb->out);
		fputs(linesep(tb), tb->out);
		tb->indent_last_sep = 1;
	}
}

void fput_line_open(struct libscols_table *tb)
{
	if (scols_table_is_json(tb)) {
		fput_indent(tb);
		fputc('{', tb->out);
		tb->indent_last_sep = 0;
	}
	tb->indent++;
}

void fput_line_close(struct libscols_table *tb, int last, int last_in_table)
{
	tb->indent--;
	if (scols_table_is_json(tb)) {
		if (tb->indent_last_sep)
			fput_indent(tb);
		fputs(last ? "}" : "},", tb->out);
		if (!tb->no_linesep)
			fputs(linesep(tb), tb->out);

	} else if (tb->no_linesep == 0 && last_in_table == 0) {
		fputs(linesep(tb), tb->out);
		tb->termlines_used++;
	}

	tb->indent_last_sep = 1;
}
