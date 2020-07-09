/*
 * irq-common.c - functions to display kernel interrupt information.
 *
 * Copyright (C) 2019 zhenwei pi <pizhenwei@bytedance.com>
 * Copyright (C) 2020 Karel Zak <kzak@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <libsmartcols.h>

#include "c.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "xalloc.h"

#include "irq-common.h"

#define IRQ_INFO_LEN	64

struct colinfo {
	const char *name;
	double whint;
	int flags;
	const char *help;
	int json_type;
};

static const struct colinfo infos[] = {
	[COL_IRQ]   = {"IRQ",   0.10, SCOLS_FL_RIGHT, N_("interrupts"),  SCOLS_JSON_STRING},
	[COL_TOTAL] = {"TOTAL", 0.10, SCOLS_FL_RIGHT, N_("total count"), SCOLS_JSON_NUMBER},
	[COL_DELTA] = {"DELTA", 0.10, SCOLS_FL_RIGHT, N_("delta count"), SCOLS_JSON_NUMBER},
	[COL_NAME]  = {"NAME",  0.70, SCOLS_FL_TRUNC, N_("name"),        SCOLS_JSON_STRING},
};

int irq_column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	assert(name);
	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static inline int get_column_id(struct irq_output *out, size_t const num)
{
	assert(num < out->ncolumns);
	assert(out->columns[num] < (int)ARRAY_SIZE(infos));

	return out->columns[num];
}

static inline const struct colinfo *get_column_info(
			struct irq_output *out, unsigned num)
{
	return &infos[get_column_id(out, num)];
}

void irq_print_columns(FILE *f, int nodelta)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		if (nodelta && i == COL_DELTA)
			continue;
		fprintf(f, "  %-5s  %s\n", infos[i].name, _(infos[i].help));
	}
}

static struct libscols_table *new_scols_table(struct irq_output *out)
{
	size_t i;
	struct libscols_table *table;

	table = scols_new_table();
	if (!table) {
		warn(_("failed to initialize output table"));
		return NULL;
	}
	scols_table_enable_json(table, out->json);
	scols_table_enable_noheadings(table, out->no_headings);
	scols_table_enable_export(table, out->pairs);

	if (out->json)
		scols_table_set_name(table, _("interrupts"));

	for (i = 0; i < out->ncolumns; i++) {
		const struct colinfo *col = get_column_info(out, i);
		int flags = col->flags;
		struct libscols_column *cl;

		cl = scols_table_new_column(table, col->name, col->whint, flags);
		if (cl == NULL) {
			warnx(_("failed to initialize output column"));
			goto err;
		}
		if (out->json)
			scols_column_set_json_type(cl, col->json_type);
	}

	return table;
 err:
	scols_unref_table(table);
	return NULL;
}

static void add_scols_line(struct irq_output *out,
			   struct irq_info *info,
			   struct libscols_table *table)
{
	size_t i;
	struct libscols_line *line;

	line = scols_table_new_line(table, NULL);
	if (!line) {
		warn(_("failed to add line to output"));
		return;
	}

	for (i = 0; i < out->ncolumns; i++) {
		char *str = NULL;

		switch (get_column_id(out, i)) {
		case COL_IRQ:
			xasprintf(&str, "%s", info->irq);
			break;
		case COL_TOTAL:
			xasprintf(&str, "%ld", info->total);
			break;
		case COL_DELTA:
			xasprintf(&str, "%ld", info->delta);
			break;
		case COL_NAME:
			xasprintf(&str, "%s", info->name);
			break;
		default:
			break;
		}

		if (str && scols_line_refer_data(line, i, str) != 0)
			err_oom();
	}
}

static char *remove_repeated_spaces(char *str)
{
	char *inp = str, *outp = str;
	uint8_t prev_space = 0;

	while (*inp) {
		if (isspace(*inp)) {
			if (!prev_space) {
				*outp++ = ' ';
				prev_space = 1;
			}
		} else {
			*outp++ = *inp;
			prev_space = 0;
		}
		++inp;
	}
	*outp = '\0';
	return str;
}

/*
 * irqinfo - parse the system's interrupts
 */
static struct irq_stat *get_irqinfo(void)
{
	FILE *irqfile;
	char *line = NULL, *tmp;
	size_t len = 0;
	struct irq_stat *stat;
	struct irq_info *curr;

	/* NAME + ':' + 11 bytes/cpu + IRQ_NAME_LEN */
	stat = xcalloc(1, sizeof(*stat));

	stat->irq_info = xmalloc(sizeof(*stat->irq_info) * IRQ_INFO_LEN);
	stat->nr_irq_info = IRQ_INFO_LEN;

	irqfile = fopen(_PATH_PROC_INTERRUPTS, "r");
	if (!irqfile) {
		warn(_("cannot open %s"), _PATH_PROC_INTERRUPTS);
		goto free_stat;
	}

	/* read header firstly */
	if (getline(&line, &len, irqfile) < 0) {
		warn(_("cannot read %s"), _PATH_PROC_INTERRUPTS);
		goto close_file;
	}

	tmp = line;
	while ((tmp = strstr(tmp, "CPU")) != NULL) {
		tmp += 3;	/* skip this "CPU", find next */
		stat->nr_active_cpu++;
	}

	/* parse each line of _PATH_PROC_INTERRUPTS */
	while (getline(&line, &len, irqfile) >= 0) {
		unsigned long count;
		int index, length;

		tmp = strchr(line, ':');
		if (!tmp)
			continue;

		length = strlen(line);

		curr = stat->irq_info + stat->nr_irq++;
		memset(curr, 0, sizeof(*curr));
		*tmp = '\0';
		curr->irq = xstrdup(line);
		ltrim_whitespace((unsigned char *)curr->irq);

		tmp += 1;
		for (index = 0; (index < stat->nr_active_cpu) && (tmp - line < length); index++) {
			sscanf(tmp, " %10lu", &count);
			curr->total += count;
			stat->total_irq += count;
			tmp += 11;
		}

		if (tmp - line < length) {
			/* strip all space before desc */
			while (isspace(*tmp))
				tmp++;
			tmp = remove_repeated_spaces(tmp);
			rtrim_whitespace((unsigned char *)tmp);
			curr->name = xstrdup(tmp);
		} else	/* no irq name string, we have to set '\0' here */
			curr->name = xstrdup("");

		if (stat->nr_irq == stat->nr_irq_info) {
			stat->nr_irq_info *= 2;
			stat->irq_info = xrealloc(stat->irq_info,
						  sizeof(*stat->irq_info) * stat->nr_irq_info);
		}
	}
	fclose(irqfile);
	free(line);
	return stat;

 close_file:
	fclose(irqfile);
 free_stat:
	free(stat->irq_info);
	free(stat);
	free(line);
	return NULL;
}

void free_irqstat(struct irq_stat *stat)
{
	size_t i;

	if (!stat)
		return;

	for (i = 0; i < stat->nr_irq; i++) {
		free(stat->irq_info[i].name);
		free(stat->irq_info[i].irq);
	}

	free(stat->irq_info);
	free(stat);
}

static inline int cmp_name(const struct irq_info *a,
		     const struct irq_info *b)
{
	return (strcmp(a->name, b->name) > 0) ? 1 : 0;
}

static inline int cmp_total(const struct irq_info *a,
		      const struct irq_info *b)
{
	return a->total < b->total;
}

static inline int cmp_delta(const struct irq_info *a,
		      const struct irq_info *b)
{
	return a->delta < b->delta;
}

static inline int cmp_interrupts(const struct irq_info *a,
			   const struct irq_info *b)
{
	return (strcmp(a->irq, b->irq) > 0) ? 1 : 0;
}

static void sort_result(struct irq_output *out,
			struct irq_info *result,
			size_t nmemb)
{
	irq_cmp_t *func = cmp_total;	/* default */

	if (out->sort_cmp_func)
		func = out->sort_cmp_func;

	qsort(result, nmemb, sizeof(*result),
			(int (*)(const void *, const void *)) func);
}

void set_sort_func_by_name(struct irq_output *out, const char *name)
{
	if (strcasecmp(name, "IRQ") == 0)
		out->sort_cmp_func = cmp_interrupts;
	else if (strcasecmp(name, "TOTAL") == 0)
		out->sort_cmp_func = cmp_total;
	else if (strcasecmp(name, "DELTA") == 0)
		out->sort_cmp_func = cmp_delta;
	else if (strcasecmp(name, "NAME") == 0)
		out->sort_cmp_func = cmp_name;
	else
		errx(EXIT_FAILURE, _("unsupported column name to sort output"));
}

void set_sort_func_by_key(struct irq_output *out, char c)
{
	switch (c) {
	case 'i':
		out->sort_cmp_func = cmp_interrupts;
		break;
	case 't':
		out->sort_cmp_func = cmp_total;
		break;
	case 'd':
		out->sort_cmp_func = cmp_delta;
		break;
	case 'n':
		out->sort_cmp_func = cmp_name;
		break;
	}
}

struct libscols_table *get_scols_table(struct irq_output *out,
					      struct irq_stat *prev,
					      struct irq_stat **xstat)
{
	struct libscols_table *table;
	struct irq_info *result;
	struct irq_stat *stat;
	size_t size;
	size_t i;

	/* the stats */
	stat = get_irqinfo();
	if (!stat)
		return NULL;

	size = sizeof(*stat->irq_info) * stat->nr_irq;
	result = xmalloc(size);
	memcpy(result, stat->irq_info, size);

	if (prev) {
		stat->delta_irq = 0;
		for (i = 0; i < stat->nr_irq; i++) {
			struct irq_info *cur = &result[i];
			struct irq_info *pre = &prev->irq_info[i];

			cur->delta = cur->total - pre->total;
			stat->delta_irq += cur->delta;
		}
	}
	sort_result(out, result, stat->nr_irq);

	table = new_scols_table(out);
	if (!table)
		return NULL;

	for (i = 0; i < stat->nr_irq; i++)
		add_scols_line(out, &result[i], table);

	free(result);

	if (xstat)
		*xstat = stat;
	else
		free_irqstat(stat);

	return table;
}
