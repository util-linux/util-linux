
#include "smartcolsP.h"

/* This is generic allocater for a new node, always use the node type specific
 * functions (e.g. filter_new_param() */
static struct filter_node *new_node(enum filter_ntype type, size_t sz)
{
	void *x = calloc(1, sz);
	struct filter_node *n = (struct filter_node *) x;

	if (!x)
		return NULL;

	n->type = type;
	n->refcount = 1;
	return n;
}

struct filter_node *filter_new_param(struct libscols_filter *fltr __attribute__((__unused__)),
				 enum filter_ptype type,
				 void *data)
{
	struct filter_param *n = (struct filter_param *) new_node(
					F_NODE_PARAM,
					sizeof(struct filter_param));
	n->type = type;

	switch (type) {
	case F_PARAM_NAME:
	case F_PARAM_STRING:
		n->val.str = strdup((char *) data);
		break;
	case F_PARAM_NUMBER:
		n->val.num = *((unsigned long long *) data);
		break;
	case F_PARAM_FLOAT:
		n->val.fnum = *((long double *) data);
		break;
	case F_PARAM_BOOLEAN:
		n->val.boolean = *((bool *) data) == 0 ? 0 : 1;
		break;
	}
	return (struct filter_node *) n;
}

static void free_param(struct filter_param *n)
{
	if (n->type == F_PARAM_NAME || n->type == F_PARAM_STRING)
		free(n->val.str);
	free(n);
}

#define plus_indent(i)		((i) + 5)
#define indent(f, i)		fprintf(f, "%*s", (i), "");
#define indent_inside(f, i)	indent(f, plus_indent(i))

static void dump_param(FILE *out, int i __attribute__((__unused__)), struct filter_param *n)
{
	fprintf(out, "param { ");

	switch (n->type) {
	case F_PARAM_NAME:
		fprintf(out, "name: '%s'", n->val.str);
		break;
	case F_PARAM_STRING:
		fprintf(out, "string: '%s'", n->val.str);
		break;
	case F_PARAM_NUMBER:
		fprintf(out, "number: %llu", n->val.num);
		break;
	case F_PARAM_FLOAT:
		fprintf(out, "float: %Lg", n->val.fnum);
		break;
	case F_PARAM_BOOLEAN:
		fprintf(out, "bool: %s", n->val.boolean ? "true" : "false");
		break;
	}
	fprintf(out, " }\n");
}

struct filter_node *filter_new_expr(struct libscols_filter *fltr __attribute__((__unused__)),
				 enum filter_etype type,
				 struct filter_node *left,
				 struct filter_node *right)
{
	struct filter_expr *n = (struct filter_expr *) new_node(
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

static void free_expr(struct filter_expr *n)
{
	filter_unref_node(n->left);
	filter_unref_node(n->right);
	free(n);
}

static void dump_expr(FILE *out, int i, struct filter_expr *n)
{
	fprintf(out, "expr {\n");

	indent_inside(out, i);
	fprintf(out, "type: ");

	switch (n->type) {
	case F_EXPR_AND:
		fprintf(out, "AND");
		break;
	case F_EXPR_OR:
		fprintf(out, "OR");
		break;
	case F_EXPR_EQ:
		fprintf(out, "EQ");
		break;
	case F_EXPR_NE:
		fprintf(out, "NE");
		break;
	case F_EXPR_LE:
		fprintf(out, "LE");
		break;
	case F_EXPR_LT:
		fprintf(out, "LT");
		break;
	case F_EXPR_GE:
		fprintf(out, "GE");
		break;
	case F_EXPR_GT:
		fprintf(out, "GT");
		break;
	case F_EXPR_REG:
		fprintf(out, "REG");
		break;
	case F_EXPR_NREG:
		fprintf(out, "NREG");
		break;
	case F_EXPR_NEG:
		fprintf(out, "NOT");
		break;
	}

	fprintf(out, "\n");

	if (n->left) {
		indent_inside(out, i);
		fprintf(out, "left: ");
		filter_dump_node(out, plus_indent(i), n->left);
	}

	if (n->right) {
		indent_inside(out, i);
		fprintf(out, "right: ");
		filter_dump_node(out, plus_indent(i), n->right);
	}

	indent(out, i);
	fprintf(out, "}\n");
}


void filter_unref_node(struct filter_node *n)
{
	if (!n || --n->refcount > 0)
		return;

	switch (n->type) {
	case F_NODE_EXPR:
		free_expr((struct filter_expr *) n);
		break;
	case F_NODE_PARAM:
		free_param((struct filter_param *) n);
		break;
	}
}

void filter_dump_node(FILE *out, int i, struct filter_node *n)
{
	if (!n)
		return;

	switch (n->type) {
	case F_NODE_EXPR:
		dump_expr(out, i, (struct filter_expr *) n);
		break;
	case F_NODE_PARAM:
		dump_param(out, i, (struct filter_param *) n);
		break;
	}
	if (i == 0)
		fprintf(out, "\n");
}
