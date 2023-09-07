#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smartcolsP.h"

static int param_set_data(struct filter_param *n, enum filter_data type, const void *data)
{
	const char *p;

	switch (type) {
	case F_DATA_STRING:
		p = data;
		if (*p == '"') {
			/* remove quotation marks */
			size_t len = strlen(p);

			if (*(p + (len - 1)) == '"')
				len -= 2;
			n->val.str = strndup(p + 1, len);
		} else
			n->val.str = strdup((char *) data);
		if (!n->val.str)
			return -ENOMEM;
		break;
	case F_DATA_NUMBER:
		n->val.num = *((unsigned long long *) data);
		break;
	case F_DATA_FLOAT:
		n->val.fnum = *((long double *) data);
		break;
	case F_DATA_BOOLEAN:
		n->val.boolean = *((bool *) data) == 0 ? 0 : 1;
		break;
	default:
		return 0;
	}

	n->type = type;
	n->has_value = 1;
	return 0;
}

struct filter_node *filter_new_param(
		struct libscols_filter *fltr,
		enum filter_data type,
		enum filter_holder holder,
		void *data)
{
	struct filter_param *n = (struct filter_param *) __filter_new_node(
					F_NODE_PARAM,
					sizeof(struct filter_param));
	n->type = type;
	n->holder = holder;
	INIT_LIST_HEAD(&n->pr_params);

	if (param_set_data(n, type, data) != 0)
		return NULL;

	switch (holder) {
	case F_HOLDER_COLUMN:
		n->holder_name = strdup((char *) data);
		DBG(FLTR, ul_debugobj(fltr, "new %s holder", n->holder_name));
		break;
	default:
		break;
	}

	list_add_tail(&n->pr_params, &fltr->params);

	return (struct filter_node *) n;
}

static void param_reset_data(struct filter_param *n)
{
	if (n->type == F_DATA_STRING)
		free(n->val.str);

	memset(&n->val, 0, sizeof(n->val));
	n->has_value = 0;
}


void filter_free_param(struct filter_param *n)
{
	param_reset_data(n);

	free(n->holder_name);
	list_del_init(&n->pr_params);
	scols_unref_column(n->col);
	free(n);
}

void filter_dump_param(struct ul_jsonwrt *json, struct filter_param *n)
{
	ul_jsonwrt_object_open(json, "param");

	if (!n->has_value) {
		ul_jsonwrt_value_boolean(json, "has_value", false);
		ul_jsonwrt_value_s(json, "type",
				n->type == F_DATA_STRING ? "string" :
				n->type == F_DATA_NUMBER ? "number" :
				n->type == F_DATA_FLOAT  ? "float" :
				n->type == F_DATA_BOOLEAN ? "bool" :
				"unknown");
	} else {
		switch (n->type) {
		case F_DATA_STRING:
			ul_jsonwrt_value_s(json, "string", n->val.str);
			break;
		case F_DATA_NUMBER:
			ul_jsonwrt_value_u64(json, "number", n->val.num);
			break;
		case F_DATA_FLOAT:
			ul_jsonwrt_value_double(json, "float", n->val.fnum);
			break;
		case F_DATA_BOOLEAN:
			ul_jsonwrt_value_boolean(json, "bool", n->val.boolean);
			break;
		default:
			break;
		}
	}

	switch (n->holder) {
	case F_HOLDER_COLUMN:
		ul_jsonwrt_value_s(json, "column", n->holder_name);
		break;
	default:
		break;
	}

	ul_jsonwrt_object_close(json);
}

static int fetch_holder_data(struct libscols_filter *fltr,
			struct filter_param *n, struct libscols_line *ln)
{
	const char *data;
	int rc;

	if (n->holder != F_HOLDER_COLUMN)
		return 0;
	if (!n->col) {
		DBG(FLTR, ul_debugobj(fltr, "no column for %s holder", n->holder_name));
		return -EINVAL;
	}

	DBG(FLTR, ul_debugobj(fltr, "fetching %s data", n->holder_name));

	param_reset_data(n);

	data = scols_line_get_column_data(ln, n->col);
	rc = param_set_data(n, F_DATA_STRING, data);

	return rc;
}

int filter_eval_param(struct libscols_filter *fltr,
		struct libscols_line *ln,
		struct filter_param *n,
		int *status)
{
	int rc = 0;

	DBG(FLTR, ul_debugobj(fltr, "eval param"));

	if (!n->has_value && n->holder)
		rc = fetch_holder_data(fltr, n, ln);

	if (!n->has_value || rc) {
		*status = 0;
		goto done;
	}

	switch (n->type) {
	case F_DATA_STRING:
		*status = n->val.str != NULL && *n->val.str != '\0';
		break;
	case F_DATA_NUMBER:
		*status = n->val.num != 0;
		break;
	case F_DATA_FLOAT:
		*status = n->val.fnum != 0.0;
		break;
	case F_DATA_BOOLEAN:
		*status = n->val.boolean != false;
		break;
	default:
		rc = -EINVAL;
		break;
	}
done:
	if (rc)
		DBG(FLTR, ul_debugobj(fltr, "failed eval param [rc=%d]", rc));
	return rc;
}

static int string_opers(enum filter_etype oper, struct filter_param *l,
			struct filter_param *r, int *status)
{
	switch (oper) {
	case F_EXPR_EQ:
		*status = strcmp(l->val.str, r->val.str) == 0;
		break;
	case F_EXPR_NE:
		*status = strcmp(l->val.str, r->val.str) != 0;
		break;
	case F_EXPR_LE:
		*status = strcmp(l->val.str, r->val.str) <= 0;
		break;
	case F_EXPR_LT:
		*status = strcmp(l->val.str, r->val.str) < 0;
		break;
	case F_EXPR_GE:
		*status = strcmp(l->val.str, r->val.str) >= 0;
		break;
	case F_EXPR_GT:
		*status = strcmp(l->val.str, r->val.str) > 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int number_opers(enum filter_etype oper, struct filter_param *l,
			     struct filter_param *r, int *status)
{
	switch (oper) {
	case F_EXPR_EQ:
		*status = l->val.num == r->val.num;
		break;
	case F_EXPR_NE:
		*status = l->val.num != r->val.num;
		break;
	case F_EXPR_LE:
		*status = l->val.num <= r->val.num;
		break;
	case F_EXPR_LT:
		*status = l->val.num < r->val.num;
		break;
	case F_EXPR_GE:
		*status = l->val.num >= r->val.num;
		break;
	case F_EXPR_GT:
		*status = l->val.num > r->val.num;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int float_opers(enum filter_etype oper, struct filter_param *l,
			     struct filter_param *r, int *status)
{
	switch (oper) {
	case F_EXPR_EQ:
		*status = l->val.fnum == r->val.fnum;
		break;
	case F_EXPR_NE:
		*status = l->val.fnum != r->val.fnum;
		break;
	case F_EXPR_LE:
		*status = l->val.fnum <= r->val.fnum;
		break;
	case F_EXPR_LT:
		*status = l->val.fnum < r->val.fnum;
		break;
	case F_EXPR_GE:
		*status = l->val.fnum >= r->val.fnum;
		break;
	case F_EXPR_GT:
		*status = l->val.fnum > r->val.fnum;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int bool_opers(enum filter_etype oper, struct filter_param *l,
			     struct filter_param *r, int *status)
{
	switch (oper) {
	case F_EXPR_EQ:
		*status = l->val.boolean == r->val.boolean;
		break;
	case F_EXPR_NE:
		*status = l->val.boolean != r->val.boolean;
		break;
	case F_EXPR_LE:
		*status = l->val.boolean <= r->val.boolean;
		break;
	case F_EXPR_LT:
		*status = l->val.boolean < r->val.boolean;
		break;
	case F_EXPR_GE:
		*status = l->val.boolean >= r->val.boolean;
		break;
	case F_EXPR_GT:
		*status = l->val.boolean > r->val.boolean;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int filter_compare_params(struct libscols_filter *fltr __attribute__((__unused__)),
			  struct libscols_line *ln __attribute__((__unused__)),
			  enum filter_etype oper,
			  struct filter_param *l,
			  struct filter_param *r,
			  int *status)
{
	int rc;

	if (!l || !r || l->type != r->type)
		return -EINVAL;

	*status = 0;

	switch (l->type) {
	case F_DATA_STRING:
		rc = string_opers(oper, l, r, status);
		break;
	case F_DATA_NUMBER:
		rc = number_opers(oper, l, r, status);
		break;
	case F_DATA_FLOAT:
		rc = float_opers(oper, l, r, status);
		break;
	case F_DATA_BOOLEAN:
		rc = bool_opers(oper, l, r, status);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

int filter_next_param(struct libscols_filter *fltr,
		      struct libscols_iter *itr, struct filter_param **prm)
{
	int rc = 1;

	if (!fltr || !itr || !prm)
		return -EINVAL;
	*prm = NULL;

	if (!itr->head)
		SCOLS_ITER_INIT(itr, &fltr->params);
	if (itr->p != itr->head) {
		SCOLS_ITER_ITERATE(itr, *prm, struct filter_param, pr_params);
		rc = 0;
	}

	return rc;
}

