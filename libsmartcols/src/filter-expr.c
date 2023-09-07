#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smartcolsP.h"

struct filter_node *filter_new_expr(
			struct libscols_filter *fltr __attribute__((__unused__)),
			enum filter_etype type,
			struct filter_node *left,
			struct filter_node *right)
{
	struct filter_expr *n = (struct filter_expr *) __filter_new_node(
					F_NODE_EXPR, sizeof(struct filter_expr));

	n->type = type;
	switch (type) {
	case F_EXPR_AND:
	case F_EXPR_OR:
	case F_EXPR_EQ:
	case F_EXPR_NE:
	case F_EXPR_LE:
	case F_EXPR_LT:
	case F_EXPR_GE:
	case F_EXPR_GT:
	case F_EXPR_REG:
	case F_EXPR_NREG:
		n->left = left;
		n->right = right;
		break;
	case F_EXPR_NEG:
		n->right = right;
		break;

	}
	return (struct filter_node *) n;
}

void filter_free_expr(struct filter_expr *n)
{
	filter_unref_node(n->left);
	filter_unref_node(n->right);
	free(n);
}

static const char *expr_type_as_string(struct filter_expr *n)
{
	switch (n->type) {
	case F_EXPR_AND:
		return "AND";
	case F_EXPR_OR:
		return "OR";
	case F_EXPR_EQ:
		return "EQ";
	case F_EXPR_NE:
		return "NE";
	case F_EXPR_LE:
		return "LE";
	case F_EXPR_LT:
		return "LT";
	case F_EXPR_GE:
		return "GE";
	case F_EXPR_GT:
		return "GT";
	case F_EXPR_REG:
		return "REG";
	case F_EXPR_NREG:
		return "NREG";
	case F_EXPR_NEG:
		return "NOT";
	}
	return "";
}

void filter_dump_expr(struct ul_jsonwrt *json, struct filter_expr *n)
{
	ul_jsonwrt_object_open(json, "expr");
	ul_jsonwrt_value_s(json, "type", expr_type_as_string(n));

	if (n->left)
		filter_dump_node(json, n->left);
	if (n->right)
		filter_dump_node(json, n->right);

	ul_jsonwrt_object_close(json);
}


int filter_eval_expr(struct libscols_filter *fltr, struct filter_expr *n,
		     struct libscols_line *ln, int *status)
{
	int rc = 0;
	struct filter_param *l = NULL, *r = NULL;
	enum filter_etype oper = n->type;

	/* logical operators */
	switch (oper) {
	case F_EXPR_AND:
		rc = filter_eval_node(fltr, n->left, ln, status);
		if (rc == 0 && *status)
			rc = filter_eval_node(fltr, n->right, ln, status);
		return rc;
	case F_EXPR_OR:
		rc = filter_eval_node(fltr, n->left, ln, status);
		if (rc == 0 && !*status)
			rc = filter_eval_node(fltr, n->right, ln, status);
		return rc;
	case F_EXPR_NEG:
		rc = filter_eval_node(fltr, n->right, ln, status);
		if (rc == 0)
			*status = !*status;
		return rc;
	default:
		break;
	}

	/* compare data */
	l = (struct filter_param *) n->left;
	r = (struct filter_param *) n->right;
	rc = filter_compare_params(fltr, ln, oper, l, r, status);

	return rc;
}
