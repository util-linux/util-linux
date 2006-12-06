/* $Id: shhopt.c,v 2.2 1997/07/06 23:11:55 aebr Exp $ */
/**************************************************************************
 *
 *  FILE            shhopt.c
 *
 *  DESCRIPTION     Functions for parsing command line arguments. Values
 *                  of miscellaneous types may be stored in variables,
 *                  or passed to functions as specified.
 *
 *  REQUIREMENTS    Some systems lack the ANSI C -function strtoul. If your
 *                  system is one of those, you'll ned to write one yourself,
 *                  or get the GNU liberty-library (from prep.ai.mit.edu).
 *
 *  WRITTEN BY      Sverre H. Huseby <sverrehu@ifi.uio.no>
 *
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

#include "shhopt.h"
#include "nls.h"

/**************************************************************************
 *                                                                        *
 *                       P R I V A T E    D A T A                         *
 *                                                                        *
 **************************************************************************/

static void optFatalFunc(const char *, ...);
static void (*optFatal)(const char *format, ...) = optFatalFunc;



/**************************************************************************
 *                                                                        *
 *                   P R I V A T E    F U N C T I O N S                   *
 *                                                                        *
 **************************************************************************/

/*-------------------------------------------------------------------------
 *
 *  NAME          optFatalFunc
 *
 *  FUNCTION      Show given message and abort the program.
 *
 *  INPUT         format, ...
 *                        Arguments used as with printf().
 *
 *  RETURNS       Never returns. The program is aborted.
 *
 */
void optFatalFunc(const char *format, ...)
{
    va_list ap;

    fflush(stdout);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    exit(99);
}



/*-------------------------------------------------------------------------
 *
 *  NAME          optStructCount
 *
 *  FUNCTION      Get number of options in a optStruct.
 *
 *  INPUT         opt     array of possible options.
 *
 *  RETURNS       Number of options in the given array.
 *
 *  DESCRIPTION   Count elements in an optStruct-array. The strcture must
 *                be ended using an element of type OPT_END.
 *
 */
static int optStructCount(const optStruct opt[])
{
    int ret = 0;

    while (opt[ret].type != OPT_END)
        ++ret;
    return ret;
}



/*-------------------------------------------------------------------------
 *
 *  NAME          optMatch
 *
 *  FUNCTION      Find a matching option.
 *
 *  INPUT         opt     array of possible options.
 *                s       string to match, without `-' or `--'.
 *                lng     match long option, otherwise short.
 *
 *  RETURNS       Index to the option if found, -1 if not found.
 *
 *  DESCRIPTION   Short options are matched from the first character in
 *                the given string.
 *
 */
static int optMatch(const optStruct opt[], const char *s, int lng)
{
    int  nopt, q, matchlen = 0;
    char *p;

    nopt = optStructCount(opt);
    if (lng) {
	if ((p = strchr(s, '=')) != NULL)
	    matchlen = p - s;
	else
	    matchlen = strlen(s);
    }
    for (q = 0; q < nopt; q++) {
	if (lng) {
	    if (!opt[q].longName)
		continue;
	    if (strncmp(s, opt[q].longName, matchlen) == 0)
		return q;
	} else {
	    if (!opt[q].shortName)
		continue;
	    if (*s == opt[q].shortName)
		return q;
	}
    }
    return -1;
}



/*-------------------------------------------------------------------------
 *
 *  NAME          optString
 *
 *  FUNCTION      Return a (static) string with the option name.
 *
 *  INPUT         opt     the option to stringify.
 *                lng     is it a long option?
 *
 *  RETURNS       Pointer to static string.
 *
 */
static char *optString(const optStruct *opt, int lng)
{
    static char ret[31];

    if (lng) {
	strcpy(ret, "--");
	strncpy(ret + 2, opt->longName, 28);
    } else {
	ret[0] = '-';
	ret[1] = opt->shortName;
	ret[2] = '\0';
    }
    return ret;
}



/*-------------------------------------------------------------------------
 *
 *  NAME          optNeedsArgument
 *
 *  FUNCTION      Check if an option requires an argument.
 *
 *  INPUT         opt     the option to check.
 *
 *  RETURNS       Boolean value.
 *
 */
static int optNeedsArgument(const optStruct *opt)
{
    return opt->type == OPT_STRING
	|| opt->type == OPT_INT
	|| opt->type == OPT_UINT
	|| opt->type == OPT_LONG
	|| opt->type == OPT_ULONG;
}



/*-------------------------------------------------------------------------
 *
 *  NAME          argvRemove
 *
 *  FUNCTION      Remove an entry from an argv-array.
 *
 *  INPUT         argc    pointer to number of options.
 *                argv    array of option-/argument-strings.
 *                i       index of option to remove.
 *
 *  OUTPUT        argc    new argument count.
 *                argv    array with given argument removed.
 *
 */
static void argvRemove(int *argc, char *argv[], int i)
{
    if (i >= *argc)
        return;
    while (i++ < *argc)
        argv[i - 1] = argv[i];
    --*argc;
}



/*-------------------------------------------------------------------------
 *
 *  NAME          optExecute
 *
 *  FUNCTION      Perform the action of an option.
 *
 *  INPUT         opt     array of possible options.
 *                arg     argument to option, if it applies.
 *                lng     was the option given as a long option?
 *
 *  RETURNS       Nothing. Aborts in case of error.
 *
 */
static void optExecute(const optStruct *opt, char *arg, int lng)
{
    switch (opt->type) {
      case OPT_FLAG:
	if (opt->flags & OPT_CALLFUNC)
	    ((void (*)(void)) opt->arg)();
	else
	    *((int *) opt->arg) = 1;
	break;

      case OPT_STRING:
	if (opt->flags & OPT_CALLFUNC)
	    ((void (*)(char *)) opt->arg)(arg);
	else
	    *((char **) opt->arg) = arg;
	break;

      case OPT_INT:
      case OPT_LONG: {
	  long tmp;
	  char *e;
	  
	  tmp = strtol(arg, &e, 10);
	  if (*e)
	      optFatal(_("invalid number `%s'\n"), arg);
	  if (errno == ERANGE
	      || (opt->type == OPT_INT && (tmp > INT_MAX || tmp < INT_MIN)))
	      optFatal(_("number `%s' to `%s' out of range\n"),
		       arg, optString(opt, lng));
	  if (opt->type == OPT_INT) {
	      if (opt->flags & OPT_CALLFUNC)
		  ((void (*)(int)) opt->arg)((int) tmp);
	      else
		  *((int *) opt->arg) = (int) tmp;
	  } else /* OPT_LONG */ {
	      if (opt->flags & OPT_CALLFUNC)
		  ((void (*)(long)) opt->arg)(tmp);
	      else
		  *((long *) opt->arg) = tmp;
	  }
	  break;
      }
	
      case OPT_UINT:
      case OPT_ULONG: {
	  unsigned long tmp;
	  char *e;
	  
	  tmp = strtoul(arg, &e, 10);
	  if (*e)
	      optFatal(_("invalid number `%s'\n"), arg);
	  if (errno == ERANGE
	      || (opt->type == OPT_UINT && tmp > UINT_MAX))
	      optFatal(_("number `%s' to `%s' out of range\n"),
		       arg, optString(opt, lng));
	  if (opt->type == OPT_UINT) {
	      if (opt->flags & OPT_CALLFUNC)
		  ((void (*)(unsigned)) opt->arg)((unsigned) tmp);
	      else
		  *((unsigned *) opt->arg) = (unsigned) tmp;
	  } else /* OPT_ULONG */ {
	      if (opt->flags & OPT_CALLFUNC)
		  ((void (*)(unsigned long)) opt->arg)(tmp);
	      else
		  *((unsigned long *) opt->arg) = tmp;
	  }
	  break;
      }

      default:
	break;
    }
}



/**************************************************************************
 *                                                                        *
 *                    P U B L I C    F U N C T I O N S                    *
 *                                                                        *
 **************************************************************************/

/*-------------------------------------------------------------------------
 *
 *  NAME          optSetFatalFunc
 *
 *  FUNCTION      Set function used to display error message and exit.
 *
 *  SYNOPSIS      #include "shhmsg.h"
 *                void optSetFatalFunc(void (*f)(const char *, ...));
 *
 *  INPUT         f       function accepting printf()'like parameters,
 *                        that _must_ abort the program.
 *
 */
void optSetFatalFunc(void (*f)(const char *, ...))
{
    optFatal = f;
}



/*-------------------------------------------------------------------------
 *
 *  NAME          optParseOptions
 *
 *  FUNCTION      Parse commandline options.
 *
 *  SYNOPSIS      #include "shhopt.h"
 *                void optParseOptions(int *argc, char *argv[],
 *                                     const optStruct opt[], int allowNegNum);
 *
 *  INPUT         argc    Pointer to number of options.
 *                argv    Array of option-/argument-strings.
 *                opt     Array of possible options.
 *                allowNegNum
 *                        a negative number is not to be taken as
 *                        an option.
 *
 *  OUTPUT        argc    new argument count.
 *                argv    array with arguments removed.
 *
 *  RETURNS       Nothing. Aborts in case of error.
 *
 *  DESCRIPTION   This function checks each option in the argv-array
 *                against strings in the opt-array, and `executes' any
 *                matching action. Any arguments to the options are
 *                extracted and stored in the variables or passed to
 *                functions pointed to by entries in opt.
 *
 *                Options and arguments used are removed from the argv-
 *                array, and argc is decreased accordingly.
 *
 *                Any error leads to program abortion.
 *
 */
void optParseOptions(int *argc, char *argv[],
		     const optStruct opt[], int allowNegNum)
{
    int  ai,        /* argv index. */
         optarg,    /* argv index of option argument, or -1 if none. */
         mi,        /* Match index in opt. */
         done;
    char *arg,      /* Pointer to argument to an option. */
         *o,        /* pointer to an option character */
         *p;

    /*
     *  Loop through all arguments.
     */
    for (ai = 0; ai < *argc; ) {
	/*
	 *  "--" indicates that the rest of the argv-array does not
	 *  contain options.
	 */
	if (strcmp(argv[ai], "--") == 0) {
            argvRemove(argc, argv, ai);
	    break;
	}

	if (allowNegNum && argv[ai][0] == '-' && isdigit(argv[ai][1])) {
	    ++ai;
	    continue;
	} else if (strncmp(argv[ai], "--", 2) == 0) {
	    /* long option */
	    /* find matching option */
	    if ((mi = optMatch(opt, argv[ai] + 2, 1)) < 0)
		optFatal(_("unrecognized option `%s'\n"), argv[ai]);

	    /* possibly locate the argument to this option. */
	    arg = NULL;
	    if ((p = strchr(argv[ai], '=')) != NULL)
		arg = p + 1;
	    
	    /* does this option take an argument? */
	    optarg = -1;
	    if (optNeedsArgument(&opt[mi])) {
		/* option needs an argument. find it. */
		if (!arg) {
		    if ((optarg = ai + 1) == *argc)
			optFatal(_("option `%s' requires an argument\n"),
				 optString(&opt[mi], 1));
		    arg = argv[optarg];
		}
	    } else {
		if (arg)
		    optFatal(_("option `%s' doesn't allow an argument\n"),
			     optString(&opt[mi], 1));
	    }
	    /* perform the action of this option. */
	    optExecute(&opt[mi], arg, 1);
	    /* remove option and any argument from the argv-array. */
            if (optarg >= 0)
                argvRemove(argc, argv, ai);
            argvRemove(argc, argv, ai);
	} else if (*argv[ai] == '-') {
	    /* A dash by itself is not considered an option. */
	    if (argv[ai][1] == '\0') {
		++ai;
		continue;
	    }
	    /* Short option(s) following */
	    o = argv[ai] + 1;
	    done = 0;
	    optarg = -1;
	    while (*o && !done) {
		/* find matching option */
		if ((mi = optMatch(opt, o, 0)) < 0)
		    optFatal(_("unrecognized option `-%c'\n"), *o);

		/* does this option take an argument? */
		optarg = -1;
		arg = NULL;
		if (optNeedsArgument(&opt[mi])) {
		    /* option needs an argument. find it. */
		    arg = o + 1;
		    if (!*arg) {
			if ((optarg = ai + 1) == *argc)
			    optFatal(_("option `%s' requires an argument\n"),
				     optString(&opt[mi], 0));
			arg = argv[optarg];
		    }
		    done = 1;
		}
		/* perform the action of this option. */
		optExecute(&opt[mi], arg, 0);
		++o;
	    }
	    /* remove option and any argument from the argv-array. */
            if (optarg >= 0)
                argvRemove(argc, argv, ai);
            argvRemove(argc, argv, ai);
	} else {
	    /* a non-option argument */
	    ++ai;
	}
    }
}
