%{
#include "smartcolsP.h"
#include "filter-parser.h"	/* define tokens (T_*) */
#include <string.h>
static unsigned long long exapnd_prefix(char c, unsigned int x);
%}

%option reentrant bison-bridge noyywrap noinput nounput

id	[a-zA-Z][a-zA-Z_.%:/\-0-9]*
int	[0-9]+
blank	[ \t]
str_qu	\"[^\"\n]*\"
str_ap	\'[^\'\n]*\'

%%

{blank}+	;	/* ignore */
[\n]+		;	/* ignore */

"("		return '(';
")"		return ')';
"'"		return '\'';

and|AND|"&&"	return T_AND;
or|OR|"||"	return T_OR;
"!"|not|NOT	return T_NEG;

eq|EQ|"=="	return T_EQ;
ne|NE|"!="	return T_NE;

le|LE|"<="	return T_LE;
lt|LT|"<"	return T_LT;

ge|GE|">="	return T_GE;
gt|GT|">"	return T_GT;

"=~"		return T_REG;
"!~"		return T_NREG;

false|FALSE	return T_FALSE;
true|TRUE	return T_TRUE;

{int}+\.{int}+ {
	yylval->param_float = strtold(yytext, NULL);
	return T_FLOAT;
}

{int}+[KMGT]i {
	size_t len = strlen(yytext);
	char c = yytext[len - 1 - 1];
	yytext[len - 1 - 1] = '\0';
	yylval->param_number = (int64_t) strtoumax(yytext, NULL, 10);
	yylval->param_number *= exapnd_prefix(c, 1024);
	return T_NUMBER;
}

{int}+[kMGT] {
	size_t len = strlen(yytext);
	char c = yytext[len - 1];
	yytext[len - 1] = '\0';
	yylval->param_number = (int64_t) strtoumax(yytext, NULL, 10);
	yylval->param_number *= exapnd_prefix(c, 1000);
	return T_NUMBER;
}

{int}+ {
	yylval->param_number = (int64_t) strtoumax(yytext, NULL, 10);
	return T_NUMBER;
}

{id} {
	yylval->param_name = yytext;
	return T_HOLDER;
}

{str_ap}|{str_qu} {
	yylval->param_string = yytext;
	return T_STRING;
}

%%

static unsigned long long exapnd_prefix(char c, unsigned int x)
{
    unsigned long long r = 1;
    switch (c) {
    case 'T':
	r *= x; /* fall through */
    case 'G':
	r *= x; /* fall through */
    case 'M':
	r *= x; /* fall through */
    case 'K':
    case 'k':
	r *= x; /* fall through */
    }
    return r;
}
