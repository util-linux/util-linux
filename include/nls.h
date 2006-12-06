int main(int argc, char *argv[]);

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

#ifdef ENABLE_NLS
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


