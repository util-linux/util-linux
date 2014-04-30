/*
 * Copyright (C) 2012 Ondrej Oprala <ooprala@redhat.com>
 *
 * This file may be distributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include "c.h"
#include "colors.h"
#include "xalloc.h"
#include "pathnames.h"

enum {
	UL_COLORFILE_DISABLE,		/* .disable */
	UL_COLORFILE_ENABLE,		/* .enable */
	UL_COLORFILE_SCHEME,		/* .scheme */

	__UL_COLORFILE_COUNT
};

struct ul_color_ctl {
	const char	*utilname;	/* util name */
	const char	*termname;	/* terminal name ($TERM) */

	char		*scheme;	/* path to scheme */

	int		mode;		/* UL_COLORMODE_* */
	int		use_colors;	/* based on mode and scores[] */
	int		scores[__UL_COLORFILE_COUNT];
};

static struct ul_color_ctl ul_colors;


static void colors_reset(struct ul_color_ctl *cc)
{
	if (!cc)
		return;

	free(cc->scheme);

	cc->scheme = NULL;
	cc->utilname = NULL;
	cc->termname = NULL;
	cc->mode = UL_COLORMODE_UNDEF;

	memset(cc->scores, 0, sizeof(cc->scores));
}

#ifdef TEST_PROGRAM
static void colors_debug(struct ul_color_ctl *cc)
{
	int i;

	if (!cc)
		return;

	printf("Colors:\n");
	printf("\tutilname = '%s'\n", cc->utilname);
	printf("\ttermname = '%s'\n", cc->termname);
	printf("\tscheme = '%s'\n", cc->scheme);
	printf("\tmode = %s\n",
			cc->mode == UL_COLORMODE_UNDEF ? "undefined" :
			cc->mode == UL_COLORMODE_AUTO ?  "auto" :
			cc->mode == UL_COLORMODE_NEVER ? "never" :
			cc->mode == UL_COLORMODE_ALWAYS ? "always" : "???");

	for (i = 0; i < ARRAY_SIZE(cc->scores); i++)
		printf("\tscore %s = %d\n",
				i == UL_COLORFILE_DISABLE ? "disable" :
				i == UL_COLORFILE_ENABLE ? "enable" :
				i == UL_COLORFILE_SCHEME ? "scheme" : "???",
				cc->scores[i]);
}

#endif

static int filename_to_tokens(const char *str,
			      const char **name, size_t *namesz,
			      const char **term, size_t *termsz,
			      int  *filetype)
{
	const char *type_start, *term_start, *p;

	if (!str || !*str || *str == '.' || strlen(str) > PATH_MAX)
		return -EINVAL;

	/* parse .type */
	p = strrchr(str, '.');
	type_start = p ? p + 1 : str;

	if (strcmp(type_start, "disable") == 0)
		*filetype = UL_COLORFILE_DISABLE;
	else if (strcmp(type_start, "enable") == 0)
		*filetype = UL_COLORFILE_ENABLE;
	else if (strcmp(type_start, "scheme") == 0)
		*filetype = UL_COLORFILE_SCHEME;
	else
		return 1;				/* unknown type */

	if (type_start == str)
		return 0;				/* "type" only */

	/* parse @termname */
	p = strchr(str, '@');
	term_start = p ? p + 1 : NULL;
	if (term_start) {
		*term = term_start;
		*termsz = type_start - term_start - 1;
		if (term_start - 1 == str)
			return 0;			/* "@termname.type" */
	}

	/* parse utilname */
	p = term_start ? term_start : type_start;
	*name =  str;
	*namesz	= p - str - 1;

	return 0;
}


/*
 * Checks for:
 *
 *  filename                    score
 *  ---------------------------------
 *  type			1
 *  @termname.type		10 + 1
 *  utilname.type		20 + 1
 *  utilname@termname.type	20 + 10 + 1
 */
static int colors_readdir(struct ul_color_ctl *cc, const char *dirname)
{
	DIR *dir;
	int rc = 0;
	struct dirent *d;
	char scheme[PATH_MAX] = { '\0' };
	size_t namesz, termsz;

	if (!dirname || !cc || !cc->utilname || !*cc->utilname)
		return -EINVAL;
	dir = opendir(dirname);
	if (!dir)
		return -errno;

	namesz = strlen(cc->utilname);
	termsz = cc->termname ? strlen(cc->termname) : 0;

	while ((d = readdir(dir))) {
		int type, score = 1;
		const char *tk_name = NULL, *tk_term = NULL;
		size_t tk_namesz = 0, tk_termsz = 0;

		if (*d->d_name == '.')
			continue;
#ifdef _DIRENT_HAVE_D_TYPE
		if (d->d_type != DT_UNKNOWN && d->d_type != DT_LNK &&
		    d->d_type != DT_REG)
			continue;
#endif
		if (filename_to_tokens(d->d_name,
				       &tk_name, &tk_namesz,
				       &tk_term, &tk_termsz, &type) != 0)
			continue;

		/* count teoretical score before we check names to avoid
		 * unnecessary strcmp() */
		if (tk_name)
			score += 20;
		if (tk_term)
			score += 10;
		/*
		fprintf(stderr, "%20s score: %2d [cur max: %2d]\n",
				d->d_name, score, cc->scores[type]);
		*/
		if (score < cc->scores[type])
			continue;

		/* filter out by names */
		if (tk_namesz != namesz
		    || strncmp(tk_name, cc->utilname, namesz) != 0)
			continue;
		if (tk_termsz != termsz
		    || termsz == 0
		    || strncmp(tk_term, cc->termname, termsz) != 0)
			continue;

		cc->scores[type] = score;
		if (type == UL_COLORFILE_SCHEME)
			strncpy(scheme, d->d_name, sizeof(scheme));
	}

	if (*scheme) {
		scheme[sizeof(scheme) - 1] = '\0';
		if (asprintf(&cc->scheme, "%s/%s", dirname, scheme) <= 0)
			rc = -ENOMEM;
	}

	closedir(dir);
	return rc;
}

static void colors_deinit(void)
{
	colors_reset(&ul_colors);
}

static char *colors_get_homedir(char *buf, size_t bufsz)
{
	char *p = getenv("XDG_CONFIG_HOME");

	if (p) {
		snprintf(buf, bufsz, "%s/" _PATH_TERMCOLORS_DIRNAME, p);
		return buf;
	}

	p = getenv("HOME");
	if (p) {
		snprintf(buf, bufsz, "%s/.config/" _PATH_TERMCOLORS_DIRNAME, p);
		return buf;
	}

	return NULL;
}

int colors_init(int mode, const char *name)
{
	int atty = -1;
	struct ul_color_ctl *cc = &ul_colors;

	cc->utilname = name;
	cc->mode = mode;

	if (mode == UL_COLORMODE_UNDEF && (atty = isatty(STDOUT_FILENO))) {
		int rc = -ENOENT;
		char *dirname, buf[PATH_MAX];

		cc->termname = getenv("TERM");

		dirname = colors_get_homedir(buf, sizeof(buf));
		if (dirname)
			rc = colors_readdir(cc, dirname);		/* ~/.config */
		if (rc == -EPERM || rc == -EACCES || rc == -ENOENT)
			rc = colors_readdir(cc, _PATH_TERMCOLORS_DIR);	/* /etc */

		if (rc)
			cc->mode = UL_COLORMODE_AUTO;
		else {
			/* evaluate scores */
			if (cc->scores[UL_COLORFILE_DISABLE] >
			    cc->scores[UL_COLORFILE_ENABLE])
				cc->mode = UL_COLORMODE_NEVER;
			else
				cc->mode = UL_COLORMODE_AUTO;

			atexit(colors_deinit);
		}
	}

	switch (cc->mode) {
	case UL_COLORMODE_AUTO:
		cc->use_colors = atty == -1 ? isatty(STDOUT_FILENO) : atty;
		break;
	case UL_COLORMODE_ALWAYS:
		cc->use_colors = 1;
		break;
	case UL_COLORMODE_NEVER:
	default:
		cc->use_colors = 0;
	}
	return cc->use_colors;
}

int colors_wanted(void)
{
	return ul_colors.use_colors;
}

void color_fenable(const char *color_scheme, FILE *f)
{
	if (ul_colors.use_colors && color_scheme)
		fputs(color_scheme, f);
}

void color_fdisable(FILE *f)
{
	if (ul_colors.use_colors)
		fputs(UL_COLOR_RESET, f);
}

int colormode_from_string(const char *str)
{
	size_t i;
	static const char *modes[] = {
		[UL_COLORMODE_AUTO]   = "auto",
		[UL_COLORMODE_NEVER]  = "never",
		[UL_COLORMODE_ALWAYS] = "always",
		[UL_COLORMODE_UNDEF] = ""
	};

	if (!str || !*str)
		return -EINVAL;

	assert(ARRAY_SIZE(modes) == __UL_NCOLORMODES);

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strcasecmp(str, modes[i]) == 0)
			return i;
	}

	return -EINVAL;
}

int colormode_or_err(const char *str, const char *errmsg)
{
	const char *p = str && *str == '=' ? str + 1 : str;
	int colormode;

	colormode = colormode_from_string(p);
	if (colormode < 0)
		errx(EXIT_FAILURE, "%s: '%s'", errmsg, p);

	return colormode;
}

struct colorscheme {
	const char *name, *scheme;
};

static int cmp_colorscheme_name(const void *a0, const void *b0)
{
	struct colorscheme *a = (struct colorscheme *) a0,
			   *b = (struct colorscheme *) b0;
	return strcmp(a->name, b->name);
}

const char *colorscheme_from_string(const char *str)
{
	static const struct colorscheme basic_schemes[] = {
		{ "black",	UL_COLOR_BLACK           },
		{ "blue",	UL_COLOR_BLUE            },
		{ "brown",	UL_COLOR_BROWN           },
		{ "cyan",	UL_COLOR_CYAN            },
		{ "darkgray",	UL_COLOR_DARK_GRAY       },
		{ "gray",	UL_COLOR_GRAY            },
		{ "green",	UL_COLOR_GREEN           },
		{ "lightblue",	UL_COLOR_BOLD_BLUE       },
		{ "lightcyan",	UL_COLOR_BOLD_CYAN       },
		{ "lightgray,",	UL_COLOR_GRAY            },
		{ "lightgreen", UL_COLOR_BOLD_GREEN      },
		{ "lightmagenta", UL_COLOR_BOLD_MAGENTA  },
		{ "lightred",	UL_COLOR_BOLD_RED        },
		{ "magenta",	UL_COLOR_MAGENTA         },
		{ "red",	UL_COLOR_RED             },
		{ "yellow",	UL_COLOR_BOLD_YELLOW     },
	};
	struct colorscheme key = { .name = str }, *res;
	if (!str)
		return NULL;

	res = bsearch(&key, basic_schemes, ARRAY_SIZE(basic_schemes),
				sizeof(struct colorscheme),
				cmp_colorscheme_name);
	return res ? res->scheme : NULL;
}

#ifdef TEST_PROGRAM
# include <getopt.h>
int main(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "mode",     optional_argument, 0, 'm' },
		{ "color",    required_argument, 0, 'c' },
		{ "read-dir", required_argument, 0, 'd' },
		{ NULL, 0, 0, 0 }
	};
	int c, mode = UL_COLORMODE_UNDEF;	/* default */
	const char *color = "red", *dir = NULL;

	while ((c = getopt_long(argc, argv, "m::c:d:", longopts, NULL)) != -1) {
		switch (c) {
		case 'm':
			if (optarg)
				mode = colormode_or_err(optarg, "unsupported color mode");
			break;
		case 'c':
			color = optarg;
			break;
		case 'd':
			dir = optarg;
			break;
		}
	}

	if (dir) {
		struct ul_color_ctl cc = { .utilname = "foo",
					   .termname = "footerm" };

		colors_readdir(&cc, dir);
		colors_debug(&cc);
		colors_reset(&cc);
	}

	colors_init(mode, program_invocation_short_name);

	color_enable(colorscheme_from_string(color));
	printf("Hello World!");
	color_disable();

	return EXIT_SUCCESS;
}
#endif

