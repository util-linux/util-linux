/*
 * lsfd-filter.c - filtering engine for lsfd
 *
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2021 Masatake YAMATO <yamato@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include "lsfd-filter.h"

#include "nls.h"
#include "strutils.h"
#include "xalloc.h"

#include <string.h>
#include <ctype.h>
#include <regex.h>		/* regcomp(), regexec() */

/*
 * Definitions
 */
#define COL_HEADER_EXTRA_CHARS ":-_%." /* ??? */
#define GOT_ERROR(PARSERorFILTER)(*((PARSERorFILTER)->errmsg))

/*
 * Types
 */

enum token_type {
	TOKEN_NAME,		/* [A-Za-z_][-_:%.A-Za-z0-9]* */
	TOKEN_STR,		/* "...", '...' */
	TOKEN_DEC,		/* [1-9][0-9]+, NOTE: negative value is no dealt. */
	TOKEN_HEX,		/* 0x[0-9a-f]+ not implemented */
	TOKEN_OCT,		/* 0[1-7]+ not implemented */
	TOKEN_TRUE,		/* true */
	TOKEN_FALSE,		/* false */
	TOKEN_OPEN,		/* ( */
	TOKEN_CLOSE,		/* ) */
	TOKEN_OP1,		/* !, not */
	TOKEN_OP2,		/* TODO: =*, !* (glob match with fnmatch() */
	TOKEN_EOF,
};

enum op1_type {
	OP1_NOT,
};

enum op2_type {
	OP2_EQ,
	OP2_NE,
	OP2_AND,
	OP2_OR,
	OP2_LT,
	OP2_LE,
	OP2_GT,
	OP2_GE,
	OP2_RE_MATCH,
	OP2_RE_UNMATCH,
};

struct token {
	enum token_type type;
	union {
		char *str;
		unsigned long long num;
		enum op1_type op1;
		enum op2_type op2;
	} val;
};

struct token_class {
	const char *name;
	void (*free)(struct token *);
	void (*dump)(struct token *, FILE *);
};

struct parameter {
	struct libscols_column *cl;
	bool has_value;
	union {
		const char *str;
		unsigned long long num;
		bool boolean;
	} val;
};

struct parser {
	const char *expr;
	const char *cursor;
	int paren_level;
	struct libscols_table *tb;
	int (*column_name_to_id)(const char *, void *);
	struct libscols_column *(*add_column_by_id)(struct libscols_table *, int, void*);
	void *data;
	struct parameter *parameters;
	char errmsg[128];
};

enum node_type {
	NODE_STR,
	NODE_NUM,
	NODE_BOOL,
	NODE_RE,
	NODE_OP1,
	NODE_OP2,
};

struct node {
	enum node_type type;
};

struct op1_class {
	const char *name;
	/* Return true if acceptable. */
	bool (*is_acceptable)(struct node *, struct parameter *, struct libscols_line *);
	/* Return true if o.k. */
	bool (*check_type)(struct parser *, struct op1_class *, struct node *);
};

struct op2_class {
	const char *name;
	/* Return true if acceptable. */
	bool (*is_acceptable)(struct node *, struct node *, struct parameter *, struct libscols_line *);
	/* Return true if o.k. */
	bool (*check_type)(struct parser *, struct op2_class *, struct node *, struct node *);
};

#define VAL(NODE,FIELD) (((struct node_val *)(NODE))->val.FIELD)
#define PINDEX(NODE) (((struct node_val *)(NODE))->pindex)
struct node_val {
	struct node base;
	int pindex;
	union {
		char *str;
		unsigned long long num;
		bool boolean;
		regex_t re;
	} val;
};

struct node_op1 {
	struct node base;
	struct op1_class *opclass;
	struct node *arg;
};

struct node_op2 {
	struct node base;
	struct op2_class *opclass;
	struct node *args[2];
};

struct node_class {
	const char *name;
	void (*free)(struct node *);
	void (*dump)(struct node *, struct parameter*, int, FILE *);
};

struct lsfd_filter {
	struct libscols_table *table;
	struct node  *node;
	struct parameter *parameters;
	int nparams;
	char errmsg[ sizeof_member(struct parser, errmsg) ];
};

/*
 * Prototypes
 */
static struct node *node_val_new(enum node_type, int pindex);
static void node_free (struct node *);
static bool node_apply(struct node *, struct parameter *, struct libscols_line *);
static void node_dump (struct node *, struct parameter *, int, FILE *);

static struct token *token_new (void);
static void          token_free(struct token *);
#ifdef DEBUG
static void          token_dump(struct token *, FILE *);
#endif	/* DEBUG */

static void token_free_str(struct token *);

static void token_dump_str(struct token *, FILE *);
static void token_dump_num(struct token *, FILE *);
static void token_dump_op1(struct token *, FILE *);
static void token_dump_op2(struct token *, FILE *);

static bool op1_not(struct node *, struct parameter*, struct libscols_line *);
static bool op1_check_type_bool_or_op(struct parser *, struct op1_class *, struct node *);

static bool op2_eq (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_ne (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_and(struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_or (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_lt (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_le (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_gt (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_ge (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_re_match (struct node *, struct node *, struct parameter*, struct libscols_line *);
static bool op2_re_unmatch (struct node *, struct node *, struct parameter*, struct libscols_line *);

static bool op2_check_type_eq_or_bool_or_op(struct parser *, struct op2_class *, struct node *, struct node *);
static bool op2_check_type_boolean_or_op   (struct parser *, struct op2_class *, struct node *, struct node *);
static bool op2_check_type_num             (struct parser *, struct op2_class *, struct node *, struct node *);
static bool op2_check_type_re              (struct parser *, struct op2_class *, struct node *, struct node *);

static void node_str_free(struct node *);
static void node_re_free (struct node *);
static void node_op1_free(struct node *);
static void node_op2_free(struct node *);

static void node_str_dump (struct node *, struct parameter*, int, FILE *);
static void node_num_dump (struct node *, struct parameter*, int, FILE *);
static void node_bool_dump(struct node *, struct parameter*, int, FILE *);
static void node_re_dump  (struct node *, struct parameter*, int, FILE *);
static void node_op1_dump (struct node *, struct parameter*, int, FILE *);
static void node_op2_dump (struct node *, struct parameter*, int, FILE *);

static struct node *dparser_compile(struct parser *);

/*
 * Data
 */
#define TOKEN_CLASS(TOKEN) (&token_classes[(TOKEN)->type])
static struct token_class token_classes [] = {
	[TOKEN_NAME] = {
		.name = "NAME",
		.free = token_free_str,
		.dump = token_dump_str,
	},
	[TOKEN_STR] = {
		.name = "STR",
		.free = token_free_str,
		.dump = token_dump_str,
	},
	[TOKEN_DEC] = {
		.name = "DEC",
		.dump = token_dump_num,
	},
	[TOKEN_TRUE] = {
		.name = "true",
	},
	[TOKEN_FALSE] = {
		.name = "false",
	},
	[TOKEN_OPEN] = {
		.name = "OPEN",
	},
	[TOKEN_CLOSE] = {
		.name = "CLOSE",
	},
	[TOKEN_OP1] = {
		.name = "OP1",
		.dump = token_dump_op1,
	},
	[TOKEN_OP2] = {
		.name = "OP2",
		.dump = token_dump_op2,
	},
	[TOKEN_EOF] = {
		.name = "TOKEN_EOF",
	},
};

#define TOKEN_OP1_CLASS(TOKEN) (&(op1_classes[(TOKEN)->val.op1]))
static struct op1_class op1_classes [] = {
	[OP1_NOT] = {
		.name = "!",
		.is_acceptable = op1_not,
		.check_type = op1_check_type_bool_or_op,
	},
};

#define TOKEN_OP2_CLASS(TOKEN) (&(op2_classes[(TOKEN)->val.op2]))
static struct op2_class op2_classes [] = {
	[OP2_EQ] = {
		.name = "==",
		.is_acceptable = op2_eq,
		.check_type = op2_check_type_eq_or_bool_or_op
	},
	[OP2_NE] = {
		.name = "!=",
		.is_acceptable = op2_ne,
		.check_type = op2_check_type_eq_or_bool_or_op,
	},
	[OP2_AND] = {
		.name = "&&",
		.is_acceptable = op2_and,
		.check_type = op2_check_type_boolean_or_op,
	},
	[OP2_OR] = {
		.name = "||",
		.is_acceptable = op2_or,
		.check_type = op2_check_type_boolean_or_op,
	},
	[OP2_LT] = {
		.name = "<",
		.is_acceptable = op2_lt,
		.check_type = op2_check_type_num,
	},
	[OP2_LE] = {
		.name = "<=",
		.is_acceptable = op2_le,
		.check_type = op2_check_type_num,
	},
	[OP2_GT] = {
		.name = ">",
		.is_acceptable = op2_gt,
		.check_type = op2_check_type_num,
	},
	[OP2_GE] = {
		.name = ">=",
		.is_acceptable = op2_ge,
		.check_type = op2_check_type_num,
	},
	[OP2_RE_MATCH] = {
		.name = "=~",
		.is_acceptable = op2_re_match,
		.check_type = op2_check_type_re,
	},
	[OP2_RE_UNMATCH] = {
		.name = "!~",
		.is_acceptable = op2_re_unmatch,
		.check_type = op2_check_type_re,
	},
};

#define NODE_CLASS(NODE) (&node_classes[(NODE)->type])
static struct node_class node_classes[] = {
	[NODE_STR] = {
		.name = "STR",
		.free = node_str_free,
		.dump = node_str_dump,
	},
	[NODE_NUM] = {
		.name = "NUM",
		.dump = node_num_dump,
	},
	[NODE_BOOL] = {
		.name = "BOOL",
		.dump = node_bool_dump,
	},
	[NODE_RE] = {
		.name = "STR",
		.free = node_re_free,
		.dump = node_re_dump,
	},
	[NODE_OP1]   = {
		.name = "OP1",
		.free = node_op1_free,
		.dump = node_op1_dump,
	},
	[NODE_OP2]   = {
		.name = "OP2",
		.free = node_op2_free,
		.dump = node_op2_dump,
	}
};

/*
 * Functions
 */
static int strputc(char **a, const char b)
{
	return strappend(a, (char [2]){b, '\0'});
}

static void xstrputc(char **a, const char b)
{
	int rc = strputc(a, b);
	if (rc < 0)
		errx(EXIT_FAILURE, _("failed to allocate memory"));
}

static void parser_init(struct parser *parser, const char *const expr, struct libscols_table *tb,
			int ncols,
			int (*column_name_to_id)(const char *, void *),
			struct libscols_column *(*add_column_by_id)(struct libscols_table *, int, void*),
			void *data)
{
	parser->expr = expr;
	parser->cursor = parser->expr;
	parser->paren_level = 0;
	parser->tb = tb;
	parser->column_name_to_id = column_name_to_id;
	parser->add_column_by_id = add_column_by_id;
	parser->data = data;
	parser->parameters = xcalloc(ncols, sizeof(struct parameter));
	parser->errmsg[0] = '\0';
}

static char parser_getc(struct parser *parser)
{
	char c = *parser->cursor;
	if (c != '\0')
		parser->cursor++;
	return c;
}

static void parser_ungetc(struct parser *parser, char c)
{
	assert(parser->cursor > parser->expr);
	if (c != '\0')
		parser->cursor--;
}

static void parser_read_str(struct parser *parser, struct token *token, char delimiter)
{
	bool escape = false;
	while (1) {
		char c = parser_getc(parser);

		if (c == '\0') {
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: string literal is not terminated: %s"),
				 token->val.str? : "");
			return;
		} else if (escape) {
			switch (c) {
			case '\\':
			case '\'':
			case '"':
				xstrputc(&token->val.str, c);
				break;
			case 'n':
				xstrputc(&token->val.str, '\n');
				break;
			case 't':
				xstrputc(&token->val.str, '\t');
				break;
				/* TODO: \f, \r, ... */
			default:
				xstrputc(&token->val.str, '\\');
				xstrputc(&token->val.str, c);
				return;
			}
			escape = false;
		} else if (c == delimiter) {
			if (token->val.str == NULL)
				token->val.str = xstrdup("");
			return;
		} else if (c == '\\')
			escape = true;
		else
			xstrputc(&token->val.str, c);
	}
}

static void parser_read_name(struct parser *parser, struct token *token)
{
	while (1) {
		char c = parser_getc(parser);
		if (c == '\0')
			break;
		if (strchr(COL_HEADER_EXTRA_CHARS, c) || isalnum((unsigned char)c)) {
			xstrputc(&token->val.str, c);
			continue;
		}
		parser_ungetc(parser, c);
		break;
	}
}

static int parser_read_dec(struct parser *parser, struct token *token)
{
	int rc = 0;
	while (1) {
		char c = parser_getc(parser);
		if (c == '\0')
			break;
		if (isdigit((unsigned char)c)) {
			xstrputc(&token->val.str, c);
			continue;
		}
		parser_ungetc(parser, c);
		break;
	}

	errno = 0;
	unsigned long long num = strtoull(token->val.str, NULL, 10);
	rc = errno;
	free(token->val.str);
	token->val.num = num;
	return rc;
}

static struct token *parser_read(struct parser *parser)
{
	struct token *t = token_new();
	char c, c0;

	do
		c = parser_getc(parser);
	while (isspace((unsigned char)c));

	switch (c) {
	case '\0':
		t->type = TOKEN_EOF;
		break;
	case '(':
		t->type = TOKEN_OPEN;
		parser->paren_level++;
		break;
	case ')':
		t->type = TOKEN_CLOSE;
		parser->paren_level--;
		if (parser->paren_level < 0)
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: unbalanced parenthesis: %s"), parser->cursor - 1);
		break;
	case '!':
		c0 = parser_getc(parser);
		if (c0 == '=') {
			t->type = TOKEN_OP2;
			t->val.op2 = OP2_NE;
			break;
		} else if (c0 == '~') {
			t->type = TOKEN_OP2;
			t->val.op2 = OP2_RE_UNMATCH;
			break;
		}
		parser_ungetc(parser, c0);
		t->type = TOKEN_OP1;
		t->val.op1 = OP1_NOT;
		break;
	case '<':
		t->type = TOKEN_OP2;
		c0 = parser_getc(parser);
		if (c0 == '=') {
			t->val.op2 = OP2_LE;
			break;
		}
		parser_ungetc(parser, c0);
		t->val.op2 = OP2_LT;
		break;
	case '>':
		t->type = TOKEN_OP2;
		c0 = parser_getc(parser);
		if (c0 == '=') {
			t->val.op2 = OP2_GE;
			break;
		}
		parser_ungetc(parser, c0);
		t->val.op2 = OP2_GT;
		break;
	case '=':
		c0 = parser_getc(parser);
		if (c0 == '=') {
			t->type = TOKEN_OP2;
			t->val.op2 = OP2_EQ;
			break;
		} else if (c0 == '~') {
			t->type = TOKEN_OP2;
			t->val.op2 = OP2_RE_MATCH;
			break;
		}
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected character %c after ="), c0);
		break;
	case '&':
		c0 = parser_getc(parser);
		if (c0 == '&') {
			t->type = TOKEN_OP2;
			t->val.op2 = OP2_AND;
			break;
		}
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected character %c after ="), c0);
		break;
	case '|':
		c0 = parser_getc(parser);
		if (c0 == '|') {
			t->type = TOKEN_OP2;
			t->val.op2 = OP2_OR;
			break;
		}
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected character %c after ="), c0);
		break;
	case '"':
	case '\'':
		t->type = TOKEN_STR;
		parser_read_str(parser, t, c);
		break;
	default:
		if (isalpha((unsigned char)c) || c == '_') {
			xstrputc(&t->val.str, c);
			parser_read_name(parser, t);
			if (strcmp(t->val.str, "true") == 0) {
				free(t->val.str);
				t->type = TOKEN_TRUE;
			} else if (strcmp(t->val.str, "false") == 0) {
				free(t->val.str);
				t->type = TOKEN_FALSE;
			} else if (strcmp(t->val.str, "or") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_OR;
			} else if (strcmp(t->val.str, "and") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_AND;
			} else if (strcmp(t->val.str, "eq") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_EQ;
			} else if (strcmp(t->val.str, "ne") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_NE;
			} else if (strcmp(t->val.str, "lt") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_LT;
			} else if (strcmp(t->val.str, "le") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_LE;
			} else if (strcmp(t->val.str, "gt") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_GT;
			} else if (strcmp(t->val.str, "ge") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP2;
				t->val.op2 = OP2_GE;
			} else if (strcmp(t->val.str, "not") == 0) {
				free(t->val.str);
				t->type = TOKEN_OP1;
				t->val.op1 = OP1_NOT;
			} else
				t->type = TOKEN_NAME;
			break;
		} else if (isdigit((unsigned char)c)) {
			t->type = TOKEN_DEC;
			xstrputc(&t->val.str, c);
			if (parser_read_dec(parser, t) != 0)
				snprintf(parser->errmsg, sizeof(parser->errmsg),
					 _("error: failed to convert input to number"));
			break;
		}
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected character %c"), c);
		break;
	}
	return t;
}

static void parameter_init(struct parameter *param, struct libscols_column *cl)
{
	param->cl = cl;
	param->has_value = false;
}

static struct libscols_column *search_column(struct libscols_table *tb, const char *name)
{
	size_t len = scols_table_get_ncols(tb);
	size_t i;

	for (i = 0; i < len; i++) {
		struct libscols_column *cl = scols_table_get_column(tb, i);
		const char *n = scols_column_get_name(cl);

		if (n && strcmp(n, name) == 0)
			return cl;
	}
	return NULL;
}

static struct node *dparser_compile1(struct parser *parser, struct node *last)
{
	struct token *t = parser_read(parser);

	if (GOT_ERROR(parser)) {
		token_free(t);
		return NULL;
	}

	if (t->type == TOKEN_EOF) {
		token_free(t);
		return last;
	}
	if (t->type == TOKEN_CLOSE) {
		token_free(t);
		return last;
	}

	if (last) {
		switch (t->type) {
		case TOKEN_NAME:
		case TOKEN_STR:
		case TOKEN_DEC:
		case TOKEN_TRUE:
		case TOKEN_FALSE:
		case TOKEN_OPEN:
		case TOKEN_OP1:
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: unexpected token: %s after %s"), t->val.str,
				 NODE_CLASS(last)->name);
			token_free(t);
			return NULL;
		default:
			break;
		}
	} else {
		switch (t->type) {
		case TOKEN_OP2:
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: empty left side expression: %s"),
				 TOKEN_OP2_CLASS(t)->name);
			token_free(t);
			return NULL;
		default:
			break;
		}
	}

	struct node *node = NULL;
	switch (t->type) {
	case TOKEN_NAME: {
		int col_id = parser->column_name_to_id(t->val.str, parser->data);
		if (col_id == LSFD_FILTER_UNKNOWN_COL_ID) {
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: no such column: %s"), t->val.str);
			token_free(t);
			return NULL;

		}

		struct libscols_column *cl = search_column(parser->tb, t->val.str);
		if (!cl) {
			cl = parser->add_column_by_id(parser->tb, col_id, parser->data);
			if (!cl) {
				snprintf(parser->errmsg, sizeof(parser->errmsg),
					 _("error: cannot add a column to table: %s"), t->val.str);
				token_free(t);
				return NULL;
			}
			scols_column_set_flags(cl, SCOLS_FL_HIDDEN);
		}
		parameter_init(parser->parameters + col_id, cl);

		int jtype = scols_column_get_json_type(cl);
		int ntype;
		switch (jtype) {
		case SCOLS_JSON_STRING:
		case SCOLS_JSON_ARRAY_STRING:
		case SCOLS_JSON_ARRAY_NUMBER:
			/* We handles SCOLS_JSON_ARRAY_* as a string
			 * till we implement operators for arrays. */
			ntype = NODE_STR;
			break;
		case SCOLS_JSON_NUMBER:
			ntype = NODE_NUM;
			break;
		case SCOLS_JSON_BOOLEAN:
			ntype = NODE_BOOL;
			break;
		default:
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: unsupported column data type: %d, column: %s"),
				 jtype, t->val.str);
			return NULL;
		}
		node = node_val_new(ntype, col_id);
		token_free(t);
		return node;
	}

	case TOKEN_STR:
		node = node_val_new(NODE_STR, -1);
		VAL(node, str) = xstrdup(t->val.str);
		token_free(t);
		return node;

	case TOKEN_DEC:
		node = node_val_new(NODE_NUM, -1);
		VAL(node, num) = t->val.num;
		token_free(t);
		return node;

	case TOKEN_TRUE:
	case TOKEN_FALSE:
		node = node_val_new(NODE_BOOL, -1);
		VAL(node, boolean) = (t->type == TOKEN_TRUE);
		token_free(t);
		return node;

	case TOKEN_OPEN:
		token_free(t);
		return dparser_compile(parser);

	case TOKEN_OP1: {
		struct node *op1_right = dparser_compile1(parser, NULL);
		struct op1_class *op1_class = TOKEN_OP1_CLASS(t);

		token_free(t);

		if (GOT_ERROR(parser)) {
			node_free(op1_right);
			return NULL;
		}

		if (op1_right == NULL) {
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: empty right side expression: %s"),
				 op1_class->name);
			return NULL;
		}

		if (!op1_class->check_type(parser, op1_class, op1_right)) {
			node_free(op1_right);
			return NULL;
		}

		node = xmalloc(sizeof(struct node_op1));
		node->type = NODE_OP1;
		((struct node_op1 *)node)->opclass = op1_class;
		((struct node_op1 *)node)->arg = op1_right;

		return node;
	}

	case TOKEN_OP2: {
		struct node *op2_right = dparser_compile1(parser, NULL);
		struct op2_class *op2_class = TOKEN_OP2_CLASS(t);

		token_free(t);

		if (GOT_ERROR(parser)) {
			node_free(op2_right);
			return NULL;
		}
		if (op2_right == NULL) {
			snprintf(parser->errmsg, sizeof(parser->errmsg),
				 _("error: empty right side expression: %s"),
				 op2_class->name);
			return NULL;
		}

		if (!op2_class->check_type(parser, op2_class, last, op2_right)) {
			node_free(op2_right);
			return NULL;
		}

		node = xmalloc(sizeof(struct node_op2));
		node->type = NODE_OP2;
		((struct node_op2 *)node)->opclass = op2_class;
		((struct node_op2 *)node)->args[0] = last;
		((struct node_op2 *)node)->args[1] = op2_right;

		return node;
	}

	default:
		warnx("unexpected token type: %d", t->type);
		token_free(t);
		return NULL;
	}
}

static struct node *dparser_compile(struct parser *parser)
{
	struct node *node = NULL;

	while (true) {
		struct node *node0 = dparser_compile1(parser, node);
		if (GOT_ERROR(parser)) {
			node_free(node);
			return NULL;
		}

		if (node == node0) {
			if (node == NULL)
				xstrncpy(parser->errmsg,
					_("error: empty filter expression"),
					sizeof(parser->errmsg));
			return node;
		}
		node = node0;
	}
}

static struct token *token_new(void)
{
	return xcalloc(1, sizeof(struct token));
}

static void token_free(struct token *token)
{
	if (TOKEN_CLASS(token)->free)
		TOKEN_CLASS(token)->free(token);
	free(token);
}

#ifdef DEBUG
static void token_dump(struct token *token, FILE *stream)
{
	fprintf(stream, "<%s>", TOKEN_CLASS(token)->name);
	if (TOKEN_CLASS(token)->dump)
		TOKEN_CLASS(token)->dump(token, stream);
	fputc('\n', stream);
}
#endif	/* DEBUG */

static void token_free_str(struct token *token)
{
	free(token->val.str);
}

static void token_dump_str(struct token *token, FILE *stream)
{
	fputs(token->val.str, stream);
}

static void token_dump_num(struct token *token, FILE *stream)
{
	fprintf(stream, "%llu", token->val.num);
}

static void token_dump_op1(struct token *token, FILE *stream)
{
	fputs(TOKEN_OP1_CLASS(token)->name, stream);
}

static void token_dump_op2(struct token *token, FILE *stream)
{
	fputs(TOKEN_OP2_CLASS(token)->name, stream);
}

static struct node *node_val_new(enum node_type type, int pindex)
{
	struct node *node = xmalloc(sizeof(struct node_val));
	node->type = type;
	PINDEX(node) = pindex;
	return node;
}

static void node_free(struct node *node)
{
	if (node == NULL)
		return;
	if (NODE_CLASS(node)->free)
		NODE_CLASS(node)->free(node);
	free(node);
}

static bool node_apply(struct node *node, struct parameter *params, struct libscols_line *ln)
{
	if (!node)
		return true;

	switch (node->type) {
	case NODE_OP1: {
		struct node_op1 *node_op1 = (struct node_op1*)node;
		return node_op1->opclass->is_acceptable(node_op1->arg, params, ln);
	}
	case NODE_OP2: {
		struct node_op2 *node_op2 = (struct node_op2*)node;
		return node_op2->opclass->is_acceptable(node_op2->args[0], node_op2->args[1], params, ln);
	}
	case NODE_BOOL:
		if (PINDEX(node) < 0)
			return VAL(node,boolean);

		if (!params[PINDEX(node)].has_value) {
			const char *data = scols_line_get_column_data(ln, params[PINDEX(node)].cl);
			if (data == NULL)
				return false;
			params[PINDEX(node)].val.boolean = !*data ? false :
				*data == '0' ? false :
				*data == 'N' || *data == 'n' ? false : true;
			params[PINDEX(node)].has_value = true;
		}
		return params[PINDEX(node)].val.boolean;
	default:
		warnx(_("unexpected type in filter application: %s"), NODE_CLASS(node)->name);
		return false;
	}
}

static void node_dump(struct node *node, struct parameter *param, int depth, FILE *stream)
{
	int i;

	if (!node)
		return;

	for (i = 0; i < depth; i++)
		fputc(' ', stream);
	fputs(NODE_CLASS(node)->name, stream);
	if (NODE_CLASS(node)->dump)
		NODE_CLASS(node)->dump(node, param, depth, stream);
}

static void node_str_dump(struct node *node, struct parameter* params, int depth __attribute__((__unused__)), FILE *stream)
{
	if (PINDEX(node) >= 0)
		fprintf(stream, ": |%s|\n", scols_column_get_name(params[PINDEX(node)].cl));
	else
		fprintf(stream, ": '%s'\n", VAL(node,str));
}

static void node_num_dump(struct node *node, struct parameter* params, int depth __attribute__((__unused__)), FILE *stream)
{
	if (PINDEX(node) >= 0)
		fprintf(stream, ": |%s|\n", scols_column_get_name(params[PINDEX(node)].cl));
	else
		fprintf(stream, ": %llu\n", VAL(node,num));
}

static void node_bool_dump(struct node *node, struct parameter* params, int depth __attribute__((__unused__)), FILE *stream)
{
	if (PINDEX(node) >= 0)
		fprintf(stream, ": |%s|\n", scols_column_get_name(params[PINDEX(node)].cl));
	else
		fprintf(stream, ": %s\n",
			VAL(node,boolean)
			? token_classes[TOKEN_TRUE].name
			: token_classes[TOKEN_FALSE].name);
}

static void node_re_dump(struct node *node, struct parameter* params __attribute__((__unused__)),
			 int depth __attribute__((__unused__)), FILE *stream)
{
	fprintf(stream, ": #<regexp %p>\n", &VAL(node,re));
}

static void node_op1_dump(struct node *node, struct parameter* params, int depth, FILE *stream)
{
	fprintf(stream, ": %s\n", ((struct node_op1 *)node)->opclass->name);
	node_dump(((struct node_op1 *)node)->arg, params, depth + 4, stream);
}

static void node_op2_dump(struct node *node, struct parameter* params, int depth, FILE *stream)
{
	int i;

	fprintf(stream, ": %s\n", ((struct node_op2 *)node)->opclass->name);
	for (i = 0; i < 2; i++)
		node_dump(((struct node_op2 *)node)->args[i], params, depth + 4, stream);
}

static void node_str_free(struct node *node)
{
	if (PINDEX(node) < 0)
		free(VAL(node,str));
}

static void node_re_free(struct node *node)
{
	regfree(&VAL(node,re));
}

static void node_op1_free(struct node *node)
{
	node_free(((struct node_op1 *)node)->arg);
}

static void node_op2_free(struct node *node)
{
	int i;

	for (i = 0; i < 2; i++)
		node_free(((struct node_op2 *)node)->args[i]);
}

static bool op1_not(struct node *node, struct parameter* params, struct libscols_line * ln)
{
	return !node_apply(node, params, ln);
}

static bool op1_check_type_bool_or_op(struct parser* parser, struct op1_class *op1_class,
				      struct node *node)
{
	if (! (node->type == NODE_OP1 || node->type == NODE_OP2 || node->type == NODE_BOOL)) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected operand type %s for: %s"),
			 NODE_CLASS(node)->name,
			 op1_class->name);
		return false;
	}
	return true;
}

#define OP2_GET_STR(NODE,DEST) do {					\
	int pindex = PINDEX(NODE);					\
	if (pindex < 0)							\
		DEST = VAL(NODE,str);					\
	else {								\
		struct parameter *p = params + pindex;			\
		if (!p->has_value) {					\
			p->val.str = scols_line_get_column_data(ln, p->cl); \
			if (p->val.str == NULL) return false;		\
			p->has_value = true;				\
		}							\
		DEST = p->val.str;					\
	}								\
} while(0)

#define OP2_GET_NUM(NODE,DEST) do {					\
	int pindex = PINDEX(NODE);					\
	if (pindex < 0)							\
		DEST = VAL(NODE,num);					\
	else {								\
		struct parameter *p = params + pindex;			\
		if (!p->has_value) {					\
			const char *tmp = scols_line_get_column_data(ln, p->cl); \
			if (tmp == NULL) return false;			\
			p->val.num = strtoull(tmp, NULL, 10);		\
			p->has_value = true;				\
		}							\
		DEST = p->val.num;					\
	}								\
} while(0)

#define OP2_EQ_BODY(OP,ELSEVAL) do {					\
	if (left->type == NODE_STR) {					\
		const char *lv, *rv;					\
		OP2_GET_STR(left,lv);					\
		OP2_GET_STR(right,rv);					\
		return strcmp(lv, rv) OP 0;				\
	} else if (left->type == NODE_NUM) {				\
		unsigned long long lv, rv;				\
		OP2_GET_NUM(left,lv);					\
		OP2_GET_NUM(right,rv);					\
		return lv OP rv;					\
	} else {							\
		return node_apply(left, params, ln) OP node_apply(right, params, ln); \
	}								\
} while(0)

#define OP2_CMP_BODY(OP) do {		\
	unsigned long long lv, rv;	\
	OP2_GET_NUM(left,lv);		\
	OP2_GET_NUM(right,rv);		\
	return (lv OP rv);		\
} while(0)
static bool op2_eq(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	OP2_EQ_BODY(==, false);
}

static bool op2_ne(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	OP2_EQ_BODY(!=, true);
}

static bool op2_and(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	return node_apply(left, params, ln) && node_apply(right, params, ln);
}

static bool op2_or(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	return node_apply(left, params, ln) || node_apply(right, params, ln);
}

static bool op2_lt(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	OP2_CMP_BODY(<);
}

static bool op2_le(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	OP2_CMP_BODY(<=);
}

static bool op2_gt(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	OP2_CMP_BODY(>);
}

static bool op2_ge(struct node *left, struct node *right, struct parameter *params, struct libscols_line *ln)
{
	OP2_CMP_BODY(>=);
}

static bool op2_re_match(struct node *left, struct node *right,
			 struct parameter *params, struct libscols_line *ln)
{
	const char *str;
	OP2_GET_STR(left, str);

	return (regexec(&VAL(right,re), str, 0, NULL, 0) == 0);
}

static bool op2_re_unmatch(struct node *left, struct node *right,
			 struct parameter *params, struct libscols_line *ln)
{
	return !op2_re_match(left, right, params, ln);
}

static bool op2_check_type_boolean_or_op(struct parser* parser, struct op2_class *op2_class,
					 struct node *left, struct node *right)
{
	enum node_type lt = left->type, rt = right->type;

	if (!(lt == NODE_OP1 || lt == NODE_OP2 || lt == NODE_BOOL)) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected left operand type %s for: %s"),
			 NODE_CLASS(left)->name,
			 op2_class->name);
		return false;
	}

	if (! (rt == NODE_OP1 || rt == NODE_OP2 || rt == NODE_BOOL)) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected right operand type %s for: %s"),
			 NODE_CLASS(right)->name,
			 op2_class->name);
		return false;
	}

	return true;
}

static bool op2_check_type_eq_or_bool_or_op(struct parser* parser, struct op2_class *op2_class,
					    struct node *left, struct node *right)
{
	enum node_type lt = left->type, rt = right->type;

	if (lt == rt)
		return true;

	return op2_check_type_boolean_or_op(parser, op2_class, left, right);
}

static bool op2_check_type_num(struct parser* parser, struct op2_class *op2_class,
			       struct node *left, struct node *right)
{
	if (left->type != NODE_NUM) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected left operand type %s for: %s"),
			 NODE_CLASS(left)->name,
			 op2_class->name);
		return false;
	}

	if (right->type != NODE_NUM) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected right operand type %s for: %s"),
			 NODE_CLASS(right)->name,
			 op2_class->name);
		return false;
	}

	return true;
}

static bool op2_check_type_re(struct parser* parser, struct op2_class *op2_class,
			      struct node *left, struct node *right)
{
	if (left->type != NODE_STR) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected left operand type %s for: %s"),
			 NODE_CLASS(left)->name,
			 op2_class->name);
		return false;
	}

	if (right->type != NODE_STR) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: unexpected right operand type %s for: %s"),
			 NODE_CLASS(right)->name,
			 op2_class->name);
		return false;
	}
	if (PINDEX(right) >= 0) {
		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: string literal is expected as right operand for: %s"),
			 op2_class->name);
		return false;
	}

	char *regex = VAL(right, str);
	VAL(right, str) = NULL;

	int err = regcomp(&VAL(right, re), regex, REG_NOSUB | REG_EXTENDED);
	if (err != 0) {
		size_t size = regerror(err, &VAL(right, re), NULL, 0);
		char *buf = xmalloc(size + 1);

		regerror(err, &VAL(right, re), buf, size);

		snprintf(parser->errmsg, sizeof(parser->errmsg),
			 _("error: could not compile regular expression %s: %s"),
			 regex, buf);
		free(buf);
		return false;
	}
	right->type = NODE_RE;
	free(regex);
	return true;
}

struct lsfd_filter *lsfd_filter_new(const char *const expr, struct libscols_table *tb,
				      int ncols,
				      int (*column_name_to_id)(const char *, void *),
				      struct libscols_column *(*add_column_by_id)(struct libscols_table *, int, void*),
				      void *data)
{
	struct parser parser;
	int i;
	struct node *node;
	struct lsfd_filter *filter;

	parser_init(&parser, expr, tb, ncols,
		    column_name_to_id,
		    add_column_by_id,
		    data);

	node = dparser_compile(&parser);
	filter = xcalloc(1, sizeof(struct lsfd_filter));

	if (GOT_ERROR(&parser)) {
		xstrncpy(filter->errmsg, parser.errmsg, sizeof(filter->errmsg));
		return filter;
	}
	assert(node);
	if (parser.paren_level > 0) {
		node_free(node);
		xstrncpy(filter->errmsg, _("error: unbalanced parenthesis: ("), sizeof(filter->errmsg));
		return filter;
	}
	if (*parser.cursor  != '\0') {
		node_free(node);
		snprintf(filter->errmsg, sizeof(filter->errmsg),
			 _("error: garbage at the end of expression: %s"), parser.cursor);
		return filter;
	}
	if (node->type == NODE_STR || node->type == NODE_NUM) {
		node_free(node);
		snprintf(filter->errmsg, sizeof(filter->errmsg),
			 _("error: bool expression is expected: %s"), expr);
		return filter;
	}

	filter->table = tb;
	scols_ref_table(filter->table);
	filter->node = node;
	filter->parameters = parser.parameters;
	filter->nparams = ncols;
	for (i = 0; i < filter->nparams; i++) {
		if (filter->parameters[i].cl)
			scols_ref_column(filter->parameters[i].cl);
	}
	return filter;
}

const char *lsfd_filter_get_errmsg(struct lsfd_filter *filter)
{
	if (GOT_ERROR(filter))
		return filter->errmsg;

	return NULL;
}

void lsfd_filter_dump(struct lsfd_filter *filter, FILE *stream)
{
	if (!filter) {
		fputs("EMPTY\n", stream);
		return;
	}

	if (GOT_ERROR(filter)) {
		fprintf(stream, "ERROR: %s\n", filter->errmsg);
		return;
	}

	node_dump(filter->node, filter->parameters, 0, stream);
}

void lsfd_filter_free(struct lsfd_filter *filter)
{
	int i;

	if (!filter)
		return;

	if (!GOT_ERROR(filter)) {
		for (i = 0; i < filter->nparams; i++) {
			if (filter->parameters[i].cl)
				scols_unref_column(filter->parameters[i].cl);
		}
		scols_unref_table(filter->table);
		node_free(filter->node);
	}
	free(filter->parameters);
	free(filter);
}

bool lsfd_filter_apply(struct lsfd_filter *filter, struct libscols_line * ln)
{
	int i;

	if (!filter)
		return true;

	if (GOT_ERROR(filter))
		return false;

	for (i = 0; i < filter->nparams; i++)
		filter->parameters[i].has_value = false;

	return node_apply(filter->node, filter->parameters, ln);
}
