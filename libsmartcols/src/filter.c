#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smartcolsP.h"

#include "filter-parser.h"
#include "filter-scanner.h"

struct libscols_filter *scols_new_filter(const char *str)
{
	struct libscols_filter *fltr = calloc(1, sizeof(*fltr));

	if (!fltr)
		return NULL;
	fltr->refcount = 1;
	INIT_LIST_HEAD(&fltr->params);

	if (str && scols_filter_parse_string(fltr, str) != 0) {
		scols_unref_filter(fltr);
		return NULL;
	}

	return fltr;
}

void scols_ref_filter(struct libscols_filter *fltr)
{
	if (fltr)
		fltr->refcount++;
}

static void reset_filter(struct libscols_filter *fltr)
{
	if (!fltr)
		return;
	filter_unref_node(fltr->root);
	fltr->root = NULL;

	if (fltr->src)
		fclose(fltr->src);
	fltr->src = NULL;

	free(fltr->errmsg);
	fltr->errmsg = NULL;
}

void scols_unref_filter(struct libscols_filter *fltr)
{
	if (fltr && --fltr->refcount <= 0) {
		DBG(FLTR, ul_debugobj(fltr, "dealloc"));
		reset_filter(fltr);
		free(fltr);
	}
}

/* This is generic allocater for a new node, always use the node type specific
 * functions (e.g. filter_new_param() */
struct filter_node *__filter_new_node(enum filter_ntype type, size_t sz)
{
	void *x = calloc(1, sz);
	struct filter_node *n = (struct filter_node *) x;

	if (!x)
		return NULL;

	n->type = type;
	n->refcount = 1;
	return n;
}

void filter_unref_node(struct filter_node *n)
{
	if (!n || --n->refcount > 0)
		return;

	switch (n->type) {
	case F_NODE_EXPR:
		filter_free_expr((struct filter_expr *) n);
		break;
	case F_NODE_PARAM:
		filter_free_param((struct filter_param *) n);
		break;
	}
}

void filter_ref_node(struct filter_node *n)
{
	if (n)
		n->refcount++;
}

void filter_dump_node(struct ul_jsonwrt *json, struct filter_node *n)
{
	if (!n)
		return;

	switch (n->type) {
	case F_NODE_EXPR:
		filter_dump_expr(json, (struct filter_expr *) n);
		break;
	case F_NODE_PARAM:
		filter_dump_param(json, (struct filter_param *) n);
		break;
	}
}



extern int yyparse(void *scanner, struct libscols_filter *fltr);

int scols_filter_parse_string(struct libscols_filter *fltr, const char *str)
{
	yyscan_t sc;
	int rc;

	reset_filter(fltr);

	fltr->src = fmemopen((void *) str, strlen(str) + 1, "r");
	if (!fltr->src)
		return -errno;

	yylex_init(&sc);
	yyset_in(fltr->src, sc);

	rc = yyparse(sc, fltr);
	yylex_destroy(sc);

	fclose(fltr->src);
	fltr->src = NULL;

	return rc;
}

int scols_dump_filter(struct libscols_filter *fltr, FILE *out)
{
	struct ul_jsonwrt json;

	if (!fltr || !out)
		return -EINVAL;

	ul_jsonwrt_init(&json, out, 0);
	ul_jsonwrt_root_open(&json);

	filter_dump_node(&json, fltr->root);
	ul_jsonwrt_root_close(&json);
	return 0;
}

const char *scols_filter_get_errmsg(struct libscols_filter *fltr)
{
	return fltr ? fltr->errmsg : NULL;
}

int scols_filter_next_name(struct libscols_filter *fltr,
			struct libscols_iter *itr, const char **name)
{
	struct filter_param *prm = NULL;
	int rc = 0;

	*name = NULL;

	do {
		rc = filter_next_param(fltr, itr, &prm);
		if (rc == 0 && prm->holder == F_HOLDER_COLUMN)
			*name = prm->val.str;
	} while (rc == 0 && !*name);

	return rc;
}

int scols_filter_assign_column(struct libscols_filter *fltr,
			const char *name, struct libscols_column *col)
{
	struct libscols_iter itr;
	struct filter_param *prm = NULL;
	int ct = 0;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (filter_next_param(fltr, &itr, &prm) == 0) {
		if (prm->holder != F_HOLDER_COLUMN || strcmp(name, prm->val.str) != 0)
			continue;
		prm->col = col;
		scols_ref_column(col);
		ct++;
	}

	return ct == 0 ? 1 : 0;
}

int filter_eval_node(struct libscols_filter *fltr, struct filter_node *n,
			struct libscols_line *ln, int *status)
{
	switch (n->type) {
	case F_NODE_PARAM:
		return filter_eval_param(fltr, (struct filter_param *) n, ln, status);
	case F_NODE_EXPR:
		return filter_eval_expr(fltr, (struct filter_expr *) n, ln, status);
	default:
		break;
	}
	return -EINVAL;
}

int scols_line_apply_filter(struct libscols_line *ln,
			struct libscols_filter *fltr, int *status)
{
	if (!ln || !fltr || !fltr->root)
		return -EINVAL;

	return filter_eval_node(fltr, fltr->root, ln, status);
}
