
#include "c.h"
#include "nls.h"
#include "cpuset.h"
#include "xalloc.h"
#include "pathnames.h"
#include "path.h"
#include "strutils.h"
#include "debug.h"

UL_DEBUG_DEFINE_MASK(lscpu);
UL_DEBUG_DEFINE_MASKNAMES(lscpu) = UL_DEBUG_EMPTY_MASKNAMES;

/*** TODO: move to lscpu.h ***/
#define LSCPU_DEBUG_INIT	(1 << 1)
#define LSCPU_DEBUG_MISC	(1 << 2)
#define LSCPU_DEBUG_GATHER	(1 << 3)
#define LSCPU_DEBUG_TYPE	(1 << 4)
#define LSBLK_DEBUG_ALL		0xFFFF

/*UL_DEBUG_DECLARE_MASK(lscpu);*/
#define DBG(m, x)       __UL_DBG(lscpu, LSCPU_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(lscpu, LSCPU_DEBUG_, m, x)

#define UL_DEBUG_CURRENT_MASK	UL_DEBUG_MASK(lscpu)
#include "debugobj.h"

#define _PATH_SYS_SYSTEM	"/sys/devices/system"
#define _PATH_SYS_HYP_FEATURES	"/sys/hypervisor/properties/features"
#define _PATH_SYS_CPU		_PATH_SYS_SYSTEM "/cpu"
#define _PATH_SYS_NODE		_PATH_SYS_SYSTEM "/node"

struct lscpu_cputype {
	cpu_set_t       *map;	/* which cpus use this type */

	int	refcount;

	char	*arch;
	char	*vendor;
	char	*machinetype;	/* s390 */
	char	*family;
	char	*model;
	char	*modelname;
	char	*revision;	/* alternative for model (ppc) */
	char	*cpu;		/* alternative for modelname (ppc, sparc) */
	char	*virtflag;	/* virtualization flag (vmx, svm) */
	char	*hypervisor;	/* hypervisor software */
	int	hyper;		/* hypervisor vendor ID */
	int	virtype;	/* VIRT_PARA|FULL|NONE ? */
	char	*stepping;
	char    *bogomips;
	char	*mhz;
	char	*dynamic_mhz;
	char	*static_mhz;
	char	*flags;
	char	*mtid;		/* maximum thread id (s390) */
	char	*addrsz;	/* address sizes */
	int	dispatching;	/* none, horizontal or vertical */
	int	freqboost;	/* -1 if not evailable */

	int	*polarization;	/* cpu polarization */
	int	*addresses;	/* physical cpu addresses */
	int	*configured;	/* cpu configured */
	int	physsockets;	/* Physical sockets (modules) */
	int	physchips;	/* Physical chips */
	int	physcoresperchip;	/* Physical cores per chip */

	int	ncores;
	int	nbooks;
	int	threads;
	int	ndrawers;

	unsigned int	bit32:1,
			bit64:1;
};

struct lscpu_cxt {
	const char *prefix;	 /* path to /sys and /proc snapshot or NULL */

	struct path_cxt	*syscpu; /* _PATH_SYS_CPU path handler */
	struct path_cxt *procfs; /* /proc path handler */

	size_t ncputypes;
	struct lscpu_cputype **cputypes;
};

struct lscpu_cputype *lscpu_new_cputype(void);
void lscpu_ref_cputype(struct lscpu_cputype *ct);
void lscpu_unref_cputype(struct lscpu_cputype *ct);
int lscpu_read_cputypes(struct lscpu_cxt *cxt);

struct lscpu_cxt *lscpu_new_context(void);
static void lscpu_free_context(struct lscpu_cxt *cxt);

/*** endof-TODO ***/

static void lscpu_init_debug(void)
{
	__UL_INIT_DEBUG_FROM_ENV(lscpu, LSCPU_DEBUG_, 0, LSCPU_DEBUG);
}

static void context_init_paths(struct lscpu_cxt *cxt)
{
	DBG(MISC, ul_debugobj(cxt, "initialize paths"));
	ul_path_init_debug();

	/* /sys/devices/system/cpu */
	cxt->syscpu = ul_new_path(_PATH_SYS_CPU);
	if (!cxt->syscpu)
		err(EXIT_FAILURE, _("failed to initialize CPUs sysfs handler"));
	if (cxt->prefix)
		ul_path_set_prefix(cxt->syscpu, cxt->prefix);

	/* /proc */
	cxt->procfs = ul_new_path("/proc");
	if (!cxt->procfs)
		err(EXIT_FAILURE, _("failed to initialize procfs handler"));
	if (cxt->prefix)
		ul_path_set_prefix(cxt->procfs, cxt->prefix);
}


struct lscpu_cputype *lscpu_new_cputype(void)
{
	struct lscpu_cputype *ct;

	ct = xcalloc(1, sizeof(struct lscpu_cputype));
	ct->refcount = 1;

	DBG(TYPE, ul_debugobj(ct, "alloc"));
	return ct;
}

void lscpu_ref_cputype(struct lscpu_cputype *ct)
{
	if (ct)
		ct->refcount++;
}

void lscpu_unref_cputype(struct lscpu_cputype *ct)
{
	if (!ct)
		return;

	if (--ct->refcount <= 0) {
		DBG(TYPE, ul_debugobj(ct, " freeing"));
		free(ct);
	}
}

struct cpuinfo_patern {
	const char *pattern;
	size_t	offset;
};

#define DEF_PATTERN(_str, _member) \
	{ \
		.pattern = (_str), \
		.offset = offsetof(struct lscpu_cputype, _member) \
	}

static const struct cpuinfo_patern patterns[] =
{
	DEF_PATTERN("BogoMIPS", bogomips),		/* aarch64 */
	DEF_PATTERN("CPU implementer", vendor),		/* ARM and aarch64 */
	DEF_PATTERN("CPU part", model),			/* ARM and aarch64 */
	DEF_PATTERN("CPU revision", revision),		/* aarch64 */
	DEF_PATTERN("CPU variant", stepping),		/* aarch64 */
	DEF_PATTERN("Features", flags),			/* aarch64 */
	DEF_PATTERN("address sizes", addrsz),		/* x86 */
	DEF_PATTERN("bogomips per cpu", bogomips),	/* s390 */
	DEF_PATTERN("bogomips", bogomips),
	DEF_PATTERN("cpu MHz dynamic", dynamic_mhz),	/* s390 */
	DEF_PATTERN("cpu MHz static", static_mhz),	/* s390 */
	DEF_PATTERN("cpu MHz", mhz),
	DEF_PATTERN("cpu family", family),
	DEF_PATTERN("cpu", cpu),
	DEF_PATTERN("family", family),
	DEF_PATTERN("features", flags),			/* s390 */
	DEF_PATTERN("flags", flags),			/* x86 */
	DEF_PATTERN("max thread id", mtid),		/* s390 */
	DEF_PATTERN("model name", modelname),
	DEF_PATTERN("model", model),
	DEF_PATTERN("revision", revision),
	DEF_PATTERN("stepping", stepping),
	DEF_PATTERN("type", flags),			/* sparc64 */
	DEF_PATTERN("vendor", vendor),
	DEF_PATTERN("vendor_id", vendor),
};

#define CPUTYPE_PATTERN_BUFSZ	32

static int cmp_pattern(const void *a0, const void *b0)
{
	const struct cpuinfo_patern
		*a = (const struct cpuinfo_patern *) a0,
		*b = (const struct cpuinfo_patern *) b0;
	return strcmp(a->pattern, b->pattern);
}

static int cputype_parse(struct lscpu_cputype *ct, const char *str)
{
	struct cpuinfo_patern key, *pat;
	const char *p, *v;
	char buf[CPUTYPE_PATTERN_BUFSZ] = { 0 }, **data;

	DBG(TYPE, ul_debugobj(ct, "parse \"%s\"", str));

	if (!str || !*str)
		return -EINVAL;
	p = skip_blank(str);
	if (!p || !*p)
		return -EINVAL;

	v = strchr(p, ':');
	if (!v || !*v)
		return -EINVAL;

	/* prepare value name */
	xstrncpy(buf, p, sizeof(buf));
	buf[v - p] = '\0';
	v++;

	rtrim_whitespace((unsigned char *)buf);

	/* search by name */
	key.pattern = buf;
	pat = bsearch(&key, patterns, ARRAY_SIZE(patterns),
			sizeof(struct cpuinfo_patern),
			cmp_pattern);
	if (!pat) {
		DBG(TYPE, ul_debugobj(ct, "'%s' not found", buf));
		return 1;	/* not found */
	}

	/* copy value to struct lscpu_cputype */
	v = skip_space(v);
	if (!v || !*v)
		return -EINVAL;
	strdup_to_offset(ct, pat->offset, v);

	/* cleanuup white chars */
	data = (char **) ((char *) ct + pat->offset);
	rtrim_whitespace((unsigned char *) *data);

	return 0;
}

static void print_cputype(struct lscpu_cputype *ct, FILE *f)
{
	fprintf(f, "vendor=\"%s\"\n", ct->vendor);
	fprintf(f, "model=\"%s\"\n", ct->model);
	fprintf(f, "flags=\"%s\"\n", ct->flags);
}

struct lscpu_cxt *lscpu_new_context(void)
{
	return xcalloc(1, sizeof(struct lscpu_cxt));
}

static void lscpu_free_context(struct lscpu_cxt *cxt)
{
	size_t i;

	if (!cxt)
		return;

	DBG(MISC, ul_debugobj(cxt, "freeing context"));

	DBG(MISC, ul_debugobj(cxt, " de-initialize paths"));
	ul_unref_path(cxt->syscpu);
	ul_unref_path(cxt->procfs);

	DBG(MISC, ul_debugobj(cxt, " freeing types"));
	for (i = 0; i < cxt->ncputypes; i++)
		lscpu_unref_cputype(cxt->cputypes[i]);

	free(cxt->cputypes);
	free(cxt);
}

int lscpu_read_cputypes(struct lscpu_cxt *cxt)
{
	struct lscpu_cputype *ct = NULL;
	FILE *fp;
	char buf[BUFSIZ];

	DBG(GATHER, ul_debugobj(cxt, "reading types"));

	fp = ul_path_fopen(cxt->procfs, "r", "cpuinfo");
	if (!fp)
		err(EXIT_FAILURE, _("cannot open %s"), "/proc/cpuinfo");

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		const char *p = skip_space(buf);

		if (*buf && !*p) {
			if (ct) {
				ON_DBG(GATHER, print_cputype(ct, stdout));
				//lscpu_add_cputype(&cxt->cputypes, &cxt->ncputypes, ct);
			}
			lscpu_unref_cputype(ct);
			ct = NULL;
			continue;
		}
		if (!ct)
			ct = lscpu_new_cputype();

		cputype_parse(ct, p);

		/* TODO: else lscpu_parse_cache(cxt, buf); */
	}

	if (ct) {
		//lscpu_add_cputype(&cxt->cputypes, &cxt->ncputypes, ct);
		lscpu_unref_cputype(ct);
	}

	fclose(fp);
	return 0;
	return 0;
}

#ifdef TEST_PROGRAM_CPUTYPE
int main(int argc, char **argv)
{
	struct lscpu_cxt *cxt;

	cxt = lscpu_new_context();

	if (argc == 3 && strcmp(argv[1], "--prefix") == 0)
		cxt->prefix = argv[2];

	lscpu_init_debug();
	context_init_paths(cxt);

	lscpu_read_cputypes(cxt);

	lscpu_free_context(cxt);

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_CPUTYPES */
