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

int scols_filter_next_holder(struct libscols_filter *fltr,
			struct libscols_iter *itr,
			const char **name,
			int type)
{
	struct filter_param *prm = NULL;
	int rc = 0;

	*name = NULL;
	if (!type)
		type = F_HOLDER_COLUMN;	/* default */

	do {
		rc = filter_next_param(fltr, itr, &prm);
		if (rc == 0 && (int) prm->holder == type) {
			*name = prm->holder_name;
		}
	} while (rc == 0 && !*name);

	return rc;
}

/**
 * scols_filter_assign_column:
 * @fltr: pointer to filter
 * @itr: iterator
 * @name: holder name
 * @col: column
 *
 * Assign @col to filter parametr. The parametr is addressed by @itr or by @name.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_filter_assign_column(struct libscols_filter *fltr,
			struct libscols_iter *itr,
			const char *name, struct libscols_column *col)
{
	struct filter_param *n = NULL;

	if (itr && itr->p) {
		struct list_head *p = IS_ITER_FORWARD(itr) ?
						itr->p->prev : itr->p->next;
		n = list_entry(p, struct filter_param, pr_params);
	} else if (name) {
		struct libscols_iter xitr;
		struct filter_param *x = NULL;

		scols_reset_iter(&xitr, SCOLS_ITER_FORWARD);
		while (filter_next_param(fltr, &xitr, &x) == 0) {
			if (x->col
			    || x->holder != F_HOLDER_COLUMN
			    || strcmp(name, x->holder_name) != 0)
				continue;
			n = x;
			break;
		}
	}

	if (n) {
		if (n->col)
			scols_unref_column(n->col);

		DBG(FPARAM, ul_debugobj(n, "assing %s to column", name));
		n->col = col;
		scols_ref_column(col);
	}

	return n ? 0 : -EINVAL;
}

int filter_eval_node(struct libscols_filter *fltr, struct libscols_line *ln,
			struct filter_node *n, int *status)
{
	switch (n->type) {
	case F_NODE_PARAM:
		return filter_eval_param(fltr, ln, (struct filter_param *) n, status);
	case F_NODE_EXPR:
		return filter_eval_expr(fltr, ln, (struct filter_expr *) n, status);
	default:
		break;
	}
	return -EINVAL;
}

int scols_line_apply_filter(struct libscols_line *ln,
			struct libscols_filter *fltr, int *status)
{
	int rc;
	struct libscols_iter itr;
	struct filter_param *prm = NULL;

	if (!ln || !fltr || !fltr->root)
		return -EINVAL;

	/* reset column data and types stored in the filter */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (filter_next_param(fltr, &itr, &prm) == 0) {
		if (prm->col)
			filter_param_reset_holder(prm);
	}

	rc = filter_eval_node(fltr, ln, fltr->root, status);

	DBG(FLTR, ul_debugobj(fltr, "filter done [rc=%d, status=%d]", rc, *status));
	return rc;
}
