int main(int argc, char *argv[]);

#include "../defines.h"		/* for HAVE_locale_h */

#ifndef PACKAGE
#define PACKAGE	"util-linux"
#endif

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

#ifdef HAVE_locale_h
# include <locale.h>
#endif

#if defined MAY_ENABLE_NLS && !defined DISABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
# ifdef gettext_noop
#  define N_(String) gettext_noop (String)
# else
#  define N_(String) (String)
# endif
#else
# undef bindtextdomain
# define bindtextdomain(Domain, Directory) /* empty */
# undef textdomain
# define textdomain(Domain) /* empty */
# define _(Text) (Text)
# define N_(Text) (Text)
#endif


