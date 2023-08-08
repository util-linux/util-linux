#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smartcolsP.h"

#include "filter-parser.h"
#include "filter-scanner.h"

static void filter_unref_node(struct filter_node *n);
static void filter_dump_node(struct ul_jsonwrt *json, struct filter_node *n);

struct libscols_filter *scols_new_filter(const char *str)
{
	struct libscols_filter *fltr = calloc(1, sizeof(*fltr));

	if (!fltr)
		return NULL;
	fltr->refcount = 1;

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

struct filter_node *filter_new_param(
		struct libscols_filter *fltr __attribute__((__unused__)),
		enum filter_ptype type,
		void *data)
{
	char *p;
	struct filter_param *n = (struct filter_param *) new_node(
					F_NODE_PARAM,
					sizeof(struct filter_param));
	n->type = type;

	switch (type) {
	case F_PARAM_STRING:
		p = data;
		if (*p == '"') {
			/* remove quotation marks */
			size_t len = strlen(p);
			if (*(p + (len - 1)) == '"')
				*(p + (len - 1)) = '\0';
			data = p + 1;
		}
		/* fallthrough */
	case F_PARAM_NAME:
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

static void dump_param(struct ul_jsonwrt *json, struct filter_param *n)
{
	ul_jsonwrt_object_open(json, "param");

	switch (n->type) {
	case F_PARAM_NAME:
		ul_jsonwrt_value_s(json, "name", n->val.str);
		break;
	case F_PARAM_STRING:
		ul_jsonwrt_value_s(json, "string", n->val.str);
		break;
	case F_PARAM_NUMBER:
		ul_jsonwrt_value_u64(json, "number", n->val.num);
		break;
	case F_PARAM_FLOAT:
		ul_jsonwrt_value_double(json, "float", n->val.fnum);
		break;
	case F_PARAM_BOOLEAN:
		ul_jsonwrt_value_boolean(json, "bool", n->val.boolean);
		break;
	}
	ul_jsonwrt_object_close(json);
}

struct filter_node *filter_new_expr(
			struct libscols_filter *fltr __attribute__((__unused__)),
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

static void dump_expr(struct ul_jsonwrt *json, struct filter_expr *n)
{
	ul_jsonwrt_object_open(json, "expr");
	ul_jsonwrt_value_s(json, "type", expr_type_as_string(n));

	if (n->left)
		filter_dump_node(json, n->left);
	if (n->right)
		filter_dump_node(json, n->right);

	ul_jsonwrt_object_close(json);
}

static void filter_unref_node(struct filter_node *n)
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

/*
static void filter_ref_node(struct filter_node *n)
{
	if (n)
		n->refcount++;
}
*/

static void filter_dump_node(struct ul_jsonwrt *json, struct filter_node *n)
{
	if (!n)
		return;

	switch (n->type) {
	case F_NODE_EXPR:
		dump_expr(json, (struct filter_expr *) n);
		break;
	case F_NODE_PARAM:
		dump_param(json, (struct filter_param *) n);
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
