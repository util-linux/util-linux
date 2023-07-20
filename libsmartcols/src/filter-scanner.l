%{
#include "filter-parser.h"	/* define tokens (T_*) */
%}

%option reentrant bison-bridge noyywrap noinput nounput

id	[a-zA-Z][a-zA-Z_0-9]*
int	[0-9]+
blank	[ \t]
string	\"[^\"\n]*\"

%%

{blank}+	;	/* ignore */
[\n]+		;	/* ignore */

"("		return '(';
")"		return ')';
"'"		return '\'';

false|FALSE	return T_FALSE;
true|TRUE	return T_TRUE;

{int}+\.{int}+ {
	yylval->param_float = strtold(yytext, NULL);
	return param_float;
}

{int}+ {
	yylval->param_number = (int64_t) strtoumax(yytext, NULL, 10);
	return param_number;
}

{id} {
	yylval->param_name = yytext;
	return param_name;
}

{string} {
	yylval->param_string = yytext;
	return param_string;
}

and|AND|"&&"	return T_AND;
or|OR|"||"	return T_OR;
"!"|not|NOT	return T_NEG;

eq|"=="		return T_EQ;
ne|"!="		return T_NE;

le|"<="		return T_LE;
lt|"<"		return T_LT;

ge|">="		return T_GE;
gt|">"		return T_GT;

"=~"		return T_REG;
"!~"		return T_NREG;
