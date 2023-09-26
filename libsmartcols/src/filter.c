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

	DBG(FLTR, ul_debugobj(fltr, "alloc"));
	fltr->refcount = 1;
	INIT_LIST_HEAD(&fltr->params);
	INIT_LIST_HEAD(&fltr->counters);

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

static void remove_counters(struct libscols_filter *fltr)
{
	if (!fltr)
		return;

	DBG(FLTR, ul_debugobj(fltr, "remove all counters"));
	while (!list_empty(&fltr->counters)) {
		struct libscols_counter *ct = list_entry(fltr->counters.next,
				struct libscols_counter, counters);

		filter_unref_node((struct filter_node *) ct->param);
		list_del_init(&ct->counters);
		free(ct->name);
		free(ct);
	}
}

void scols_unref_filter(struct libscols_filter *fltr)
{
	if (fltr && --fltr->refcount <= 0) {
		DBG(FLTR, ul_debugobj(fltr, "dealloc"));
		reset_filter(fltr);
		remove_counters(fltr);
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

	if (!str || !*str)
		return 0;	/* empty filter is not error */

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
	int rc, res = 0;
	struct libscols_iter itr;
	struct filter_param *prm = NULL;

	if (!ln || !fltr)
		return -EINVAL;

	/* reset column data and types stored in the filter */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (filter_next_param(fltr, &itr, &prm) == 0) {
		filter_param_reset_holder(prm);
	}

	if (fltr->root)
		rc = filter_eval_node(fltr, ln, fltr->root, &res);
	else
		rc = 0, res = 1;	/* empty filter matches all lines */

	if (rc == 0) {
		struct libscols_counter *ct = NULL;

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (scols_filter_next_counter(fltr, &itr, &ct) == 0) {
			if ((ct->neg && res == 0) || res == 1)
				filter_count_param(fltr, ln, ct);
		}
	}

	if (status)
		*status = res;
	DBG(FLTR, ul_debugobj(fltr, "filter done [rc=%d, status=%d]", rc, res));
	return rc;
}

int scols_filter_set_filler_cb(struct libscols_filter *fltr,
				int (*cb)(struct libscols_filter *,
					  struct libscols_line *, size_t, void *),
				void *userdata)
{
	if (!fltr)
		return -EINVAL;
	fltr->filler_cb = cb;
	fltr->filler_data = userdata;

	return 0;
}

struct libscols_counter *scols_filter_new_counter(struct libscols_filter *fltr)
{
	struct libscols_counter *ct;

	if (!fltr)
		return NULL;

	ct = calloc(1, sizeof(*ct));
	if (!ct)
		return NULL;

	DBG(FLTR, ul_debugobj(fltr, "alloc counter"));

	ct->filter = fltr;		/* don't use ref.counting here */
	INIT_LIST_HEAD(&ct->counters);
	list_add_tail(&ct->counters, &fltr->counters);


	return ct;
}

int scols_counter_set_name(struct libscols_counter *ct, const char *name)
{
	if (!ct)
		return -EINVAL;
	return strdup_to_struct_member(ct, name, name);
}

int scols_counter_set_param(struct libscols_counter *ct, const char *name)
{
	if (!ct)
		return -EINVAL;

	if (ct->param) {
		filter_unref_node((struct filter_node *) ct->param);
		ct->param = NULL;
	}
	if (name) {
		ct->param = (struct filter_param *)
				filter_new_param(ct->filter, F_DATA_NUMBER,
					     F_HOLDER_COLUMN, (void *) name);
		if (!ct->param)
			return -ENOMEM;
	}
	return 0;
}

int scols_counter_set_func(struct libscols_counter *ct, int func)
{
	if (!ct || func < 0 || func >= __SCOLS_NCOUNTES)
		return -EINVAL;

	ct->func = func;
	return 0;
}

unsigned long long scols_counter_get_result(struct libscols_counter *ct)
{
	return ct ? ct->result : 0;
}

const char *scols_counter_get_name(struct libscols_counter *ct)
{
	return ct ? ct->name : NULL;;
}

int scols_filter_next_counter(struct libscols_filter *fltr,
		      struct libscols_iter *itr, struct libscols_counter **ct)
{
	int rc = 1;

	if (!fltr || !itr || !ct)
		return -EINVAL;
	*ct = NULL;

	if (!itr->head)
		SCOLS_ITER_INIT(itr, &fltr->counters);
	if (itr->p != itr->head) {
		SCOLS_ITER_ITERATE(itr, *ct, struct libscols_counter, counters);
		rc = 0;
	}

	return rc;
}
