%{
#include <stdio.h>
#include "smartcolsP.h"

#include "filter-parser.h"
#include "filter-scanner.h"

void yyerror(yyscan_t *locp, struct libscols_filter *fltr, char const *msg);
%}

%define api.pure full

%lex-param {void *scanner}
%parse-param {void *scanner}{struct libscols_filter *fltr}

%define parse.error verbose

/*%header "filter-parser.h"*/

%code requires
{
	#include "smartcolsP.h"
}

/* Elegant way, but not compatible with biron -y (autotools):
%define api.value.type union
%token <unsigned long long>	param_number
%token <const char*>		param_string
%token <const char*>		param_name
%token <long double>		param_float
%type <struct filter_node*> param
%type <struct filter_node*> expr
*/

%union {
	unsigned long long	param_number;
	const char*		param_string;
	const char*		param_name;
	long double		param_float;
	struct filter_node	*param;
	struct filter_node	*expr;
}
%token <param_number> T_NUMBER
%token <param_string> T_STRING
%token <param_name> T_NAME
%token <param_float> T_FLOAT
%type <param> param expr

%token T_OR T_AND T_EQ T_NE T_LT T_LE T_GT T_GE T_REG T_NREG T_TRUE T_FALSE T_NEG
%left T_OR T_AND
%left T_EQ T_NE T_LT T_LE T_GT T_GE T_REG T_NREG T_TRUE T_FALSE T_NEG

%%

%start filter;

filter:
	expr { fltr->root = $1; }
;

expr:
	param			{ $$ = $1; }
	| '(' expr ')'		{ $$ = $2; }
	| expr T_AND expr	{ $$ = filter_new_expr(fltr, F_EXPR_AND, $1, $3); }
	| expr T_OR expr	{ $$ = filter_new_expr(fltr, F_EXPR_OR, $1, $3); }
	| T_NEG expr		{ $$ = filter_new_expr(fltr, F_EXPR_NEG, NULL, $2); }
	| expr T_EQ expr	{ $$ = filter_new_expr(fltr, F_EXPR_EQ, $1, $3); }
	| expr T_NE expr	{ $$ = filter_new_expr(fltr, F_EXPR_NE, $1, $3); }
	| expr T_LE expr	{ $$ = filter_new_expr(fltr, F_EXPR_LE, $1, $3); }
	| expr T_LT expr	{ $$ = filter_new_expr(fltr, F_EXPR_LT, $1, $3); }
	| expr T_GE expr	{ $$ = filter_new_expr(fltr, F_EXPR_GE, $1, $3); }
	| expr T_GT expr	{ $$ = filter_new_expr(fltr, F_EXPR_GT, $1, $3); }
	| expr T_REG expr	{ $$ = filter_new_expr(fltr, F_EXPR_REG, $1, $3); }
	| expr T_NREG expr	{ $$ = filter_new_expr(fltr, F_EXPR_NREG, $1, $3); }
;

param:
	T_NUMBER	{ $$ = filter_new_param(fltr, F_DATA_NUMBER, 0, (void *) (&$1)); }
	| T_FLOAT	{ $$ = filter_new_param(fltr, F_DATA_FLOAT, 0, (void *) (&$1)); }
	| T_NAME	{ $$ = filter_new_param(fltr, F_DATA_NONE, F_HOLDER_COLUMN, (void *) $1); }
	| T_STRING	{ $$ = filter_new_param(fltr, F_DATA_STRING, 0, (void *) $1); }
	| T_TRUE	{
		bool x = true;
		$$ = filter_new_param(fltr, F_DATA_BOOLEAN, 0, (void *) &x);
	}
	| T_FALSE	{
		bool x = false;
		$$ = filter_new_param(fltr, F_DATA_BOOLEAN, 0, (void *) &x);
	}

;


%%

void yyerror (yyscan_t *locp __attribute__((__unused__)),
	      struct libscols_filter *fltr,
	      char const *msg)
{
	if (msg) {
		fltr->errmsg = strdup(msg);
		if (!fltr->errmsg)
			return;
	}
	errno = EINVAL;
}