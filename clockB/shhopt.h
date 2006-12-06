/* $Id: shhopt.h,v 1.2 1996/06/06 00:06:35 sverrehu Exp $ */
#ifndef SHHOPT_H
#define SHHOPT_H

/* constants for recognized option types. */
typedef enum {
    OPT_END,               /* nothing. used as ending element. */
    OPT_FLAG,              /* no argument following. sets variable to 1. */
    OPT_STRING,            /* string argument. */
    OPT_INT,               /* signed integer argument. */
    OPT_UINT,              /* unsigned integer argument. */
    OPT_LONG,              /* signed long integer argument. */
    OPT_ULONG,             /* unsigned long integer argument. */
} optArgType;

/* flags modifying the default way options are handeled. */
#define OPT_CALLFUNC  1    /* pass argument to a function. */

typedef struct {
    char       shortName;  /* Short option name. */
    char       *longName;  /* Long option name, no including '--'. */
    optArgType type;       /* Option type. */
    void       *arg;       /* Pointer to variable to fill with argument,
                            * or pointer to function if Type == OPT_FUNC. */
    int        flags;      /* Modifier flags. */
} optStruct;


void optSetFatalFunc(void (*f)(const char *, ...));
void optParseOptions(int *argc, char *argv[],
		     const optStruct opt[], const int allowNegNum);

#endif
