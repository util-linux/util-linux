
#include <sys/utsname.h>
#include <sys/personality.h>

#include "lscpu-api.h"

#include "fileutils.h"

UL_DEBUG_DEFINE_MASK(lscpu);
UL_DEBUG_DEFINE_MASKNAMES(lscpu) = UL_DEBUG_EMPTY_MASKNAMES;

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


/* Lookup a pattern and get the value for format  "<pattern> : <key>"
 */
int lookup(char *line, char *pattern, char **value)
{
	char *p, *v;
	int len = strlen(pattern);

	/* don't re-fill already found tags, first one wins */
	if (!*line || *value)
		return 0;
	/* pattern */
	if (strncmp(line, pattern, len))
		return 0;
	/* white spaces */
	for (p = line + len; isspace(*p); p++);

	/* separator */
	if (*p != ':')
		return 0;
	/* white spaces */
	for (++p; isspace(*p); p++);

	/* value */
	if (!*p)
		return 0;
	v = p;

	/* end of value */
	len = strlen(line) - 1;
	for (p = line + len; isspace(*(p-1)); p--);
	*p = '\0';

	*value = xstrdup(v);
	return 1;
}

/* add @set to the @ary, unnecessary set is deallocated. */
static int add_cpuset_to_array(cpu_set_t **ary, int *items, cpu_set_t *set, size_t setsize)
{
	int i;

	if (!ary)
		return -EINVAL;

	for (i = 0; i < *items; i++) {
		if (CPU_EQUAL_S(setsize, set, ary[i]))
			break;
	}
	if (i == *items) {
		ary[*items] = set;
		++*items;
		return 0;
	}
	CPU_FREE(set);
	return 1;
}

static void free_cpuset_array(cpu_set_t **ary, int items)
{
	int i;

	if (!ary)
		return;
	for (i = 0; i < items; i++)
		free(ary[i]);
	free(ary);
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
		DBG(TYPE, ul_debugobj(ct, "  freeing"));
		free(ct->vendor);
		free(ct->machinetype);	/* s390 */
		free(ct->family);
		free(ct->model);
		free(ct->modelname);
		free(ct->revision);	/* alternative for model (ppc) */
		free(ct->stepping);
		free(ct->bogomips);
		free(ct->flags);
		free(ct->mtid);		/* maximum thread id (s390) */
		free(ct->addrsz);	/* address sizes */
		free_cpuset_array(ct->coremaps, ct->ncores);
		free_cpuset_array(ct->socketmaps, ct->nsockets);
		free_cpuset_array(ct->bookmaps, ct->nbooks);
		free_cpuset_array(ct->drawermaps, ct->ndrawers);
		free(ct);
	}
}

struct lscpu_cputype *lscpu_cputype_get_default(struct lscpu_cxt *cxt)
{
	return cxt->cputypes ? cxt->cputypes[0] : NULL;
}

#define match(astr, bstr) \
		((!astr && !bstr) || (astr && bstr && strcmp(astr, bstr) == 0))

struct lscpu_cputype *lscpu_add_cputype(struct lscpu_cxt *cxt, struct lscpu_cputype *ct)
{
	size_t i;

	/* ignore if already in the context */
	for (i = 0; i < cxt->ncputypes; i++) {
		struct lscpu_cputype *x = cxt->cputypes[i];

		if (match(x->vendor, ct->vendor) &&
		    match(x->model, ct->model) &&
		    match(x->modelname, ct->modelname) &&
		    match(x->stepping, ct->stepping)) {

			DBG(TYPE, ul_debugobj(x, "reuse"));
			return x;
		}
	}

	DBG(TYPE, ul_debugobj(ct, "add new"));
	cxt->cputypes = xrealloc(cxt->cputypes, (cxt->ncputypes + 1)
				* sizeof(struct lscpu_cputype *));
	cxt->cputypes[cxt->ncputypes] = ct;
	cxt->ncputypes++;
	lscpu_ref_cputype(ct);

	/* first type -- use it for all CPUs */
	if (cxt->ncputypes == 1)
		lscpu_cpus_apply_type(cxt, ct);

	return ct;
}

static void lscpu_merge_cputype(struct lscpu_cputype *a, struct lscpu_cputype *b)
{
	if (!a->vendor && b->vendor)
		a->vendor = xstrdup(b->vendor);
	if (!a->machinetype && b->machinetype)
		a->machinetype = xstrdup(b->machinetype);
	if (!a->family && b->family)
		a->family = xstrdup(b->family);
	if (!a->model && b->model)
		a->model = xstrdup(b->model);
	if (!a->modelname && b->modelname)
		a->modelname = xstrdup(b->modelname);
	if (!a->revision && b->revision)
		a->revision = xstrdup(b->revision);
	if (!a->stepping && b->stepping)
		a->stepping = xstrdup(b->stepping);
	if (!a->bogomips && b->bogomips)
		a->bogomips = xstrdup(b->bogomips);
	if (!a->flags && b->flags)
		a->flags = xstrdup(b->flags);
	if (!a->mtid && b->mtid)
		a->mtid = xstrdup(b->mtid);
	if (!a->addrsz && b->addrsz)
		a->addrsz = xstrdup(b->addrsz);
}

/* Read topology for specified type */
static int cputype_read_topology(struct lscpu_cxt *cxt, struct lscpu_cputype *ct)
{
	size_t i, setsize, npos;
	struct path_cxt *sys;
	int nthreads = 0;

	sys = cxt->syscpu;				/* /sys/devices/system/cpu/ */
	setsize = CPU_ALLOC_SIZE(cxt->maxcpus);		/* CPU set size */
	npos = cxt->ncpuspos;				/* possible CPUs */

	for (i = 0; i < cxt->ncpus; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];
		cpu_set_t *thread_siblings = NULL, *core_siblings = NULL;
		cpu_set_t *book_siblings = NULL, *drawer_siblings = NULL;
		int num, n;

		if (cpu->type != ct)
			continue;

		num = cpu->logical_id;
		if (ul_path_accessf(sys, F_OK,
					"cpu%d/topology/thread_siblings", num) != 0)
			continue;

		DBG(TYPE, ul_debugobj(ct, "#%d reading topology", num));

		/* read topology maps */
		ul_path_readf_cpuset(sys, &thread_siblings, cxt->maxcpus,
					"cpu%d/topology/thread_siblings", num);
		ul_path_readf_cpuset(sys, &core_siblings, cxt->maxcpus,
					"cpu%d/topology/core_siblings", num);
		ul_path_readf_cpuset(sys, &book_siblings, cxt->maxcpus,
					"cpu%d/topology/book_siblings", num);
		ul_path_readf_cpuset(sys, &drawer_siblings, cxt->maxcpus,
					"cpu%d/topology/drawer_siblings", num);

		n = CPU_COUNT_S(setsize, thread_siblings);
		if (!n)
			n = 1;
		if (n > nthreads)
			nthreads = n;

		/* Allocate arrays for topology maps.
		 *
		 * For each map we make sure that it can have up to ncpuspos
		 * entries. This is because we cannot reliably calculate the
		 * number of cores, sockets and books on all architectures.
		 * E.g. completely virtualized architectures like s390 may
		 * have multiple sockets of different sizes.
		 */
		if (!ct->coremaps)
			ct->coremaps = xcalloc(npos, sizeof(cpu_set_t *));
		if (!ct->socketmaps)
			ct->socketmaps = xcalloc(npos, sizeof(cpu_set_t *));
		if (!ct->bookmaps && book_siblings)
			ct->bookmaps = xcalloc(npos, sizeof(cpu_set_t *));
		if (!ct->drawermaps && drawer_siblings)
			ct->drawermaps = xcalloc(npos, sizeof(cpu_set_t *));

		/* add to topology maps */
		add_cpuset_to_array(ct->coremaps, &ct->ncores, thread_siblings, setsize);
		add_cpuset_to_array(ct->socketmaps, &ct->nsockets, core_siblings, setsize);

		if (book_siblings)
			add_cpuset_to_array(ct->bookmaps, &ct->nbooks, book_siblings, setsize);
		if (drawer_siblings)
			add_cpuset_to_array(ct->drawermaps, &ct->ndrawers, drawer_siblings, setsize);

	}

	ct->nthreads =	(ct->ndrawers ?: 1) *
			(ct->nbooks   ?: 1) *
			(ct->nsockets ?: 1) *
			(ct->ncores   ?: 1) * nthreads;

	DBG(TYPE, ul_debugobj(ct, " nthreads: %d", ct->nthreads));
	DBG(TYPE, ul_debugobj(ct, "   ncores: %d", ct->ncores));
	DBG(TYPE, ul_debugobj(ct, " nsockets: %d", ct->nsockets));
	DBG(TYPE, ul_debugobj(ct, "   nbooks: %d", ct->nbooks));
	DBG(TYPE, ul_debugobj(ct, " ndrawers: %d", ct->ndrawers));

	return 0;
}

int lscpu_read_topology(struct lscpu_cxt *cxt)
{
	size_t i;
	int rc = 0;

	for (i = 0; i < cxt->ncputypes; i++)
		rc += cputype_read_topology(cxt, cxt->cputypes[i]);

	return rc;
}


/* Describes /proc/cpuinfo fields */
struct cpuinfo_pattern {
	int id;			/* field ID */
	const char *pattern;	/* field name as used in /proc/cpuinfo */
	size_t	offset;		/* offset in lscpu_cputype or lscpu_cpu struct */
};

/* field identifiers (field name may be different on different archs) */
enum {
	PAT_ADDRESS_SIZES,
	PAT_BOGOMIPS,
	PAT_CPU,
	PAT_FAMILY,
	PAT_FEATURES,
	PAT_FLAGS,
	PAT_IMPLEMENTER,
	PAT_MAX_THREAD_ID,
	PAT_MHZ,
	PAT_MHZ_DYNAMIC,
	PAT_MHZ_STATIC,
	PAT_MODEL,
	PAT_MODEL_NAME,
	PAT_PART,
	PAT_PROCESSOR,
	PAT_REVISION,
	PAT_STEPPING,
	PAT_TYPE,
	PAT_VARIANT,
	PAT_VENDOR,
};

/*
 * /proc/cpuinfo to lscpu_cputype conversion
 */
#define DEF_PAT_CPUTYPE(_str, _id, _member) \
	{ \
		.id = (_id), \
		.pattern = (_str), \
		.offset = offsetof(struct lscpu_cputype, _member), \
	}

static const struct cpuinfo_pattern type_patterns[] =
{
	/* Sort by fields name! */
	DEF_PAT_CPUTYPE( "BogoMIPS",		PAT_BOGOMIPS,	bogomips),	/* aarch64 */
	DEF_PAT_CPUTYPE( "CPU implementer",	PAT_IMPLEMENTER,vendor),	/* ARM and aarch64 */
	DEF_PAT_CPUTYPE( "CPU part",		PAT_PART,	model),		/* ARM and aarch64 */
	DEF_PAT_CPUTYPE( "CPU revision",	PAT_REVISION,	revision),	/* aarch64 */
	DEF_PAT_CPUTYPE( "CPU variant",		PAT_VARIANT,	stepping),	/* aarch64 */
	DEF_PAT_CPUTYPE( "Features",		PAT_FEATURES,	flags),		/* aarch64 */
	DEF_PAT_CPUTYPE( "address sizes",	PAT_ADDRESS_SIZES,	addrsz),/* x86 */
	DEF_PAT_CPUTYPE( "bogomips",		PAT_BOGOMIPS,	bogomips),
	DEF_PAT_CPUTYPE( "bogomips per cpu",	PAT_BOGOMIPS,	bogomips),	/* s390 */
	DEF_PAT_CPUTYPE( "cpu family",		PAT_FAMILY,	family),
	DEF_PAT_CPUTYPE( "cpu",			PAT_CPU,	modelname),	/* ppc, sparc */
	DEF_PAT_CPUTYPE( "family",		PAT_FAMILY,	family),
	DEF_PAT_CPUTYPE( "features",		PAT_FEATURES,	flags),		/* s390 */
	DEF_PAT_CPUTYPE( "flags",		PAT_FLAGS,	flags),		/* x86 */
	DEF_PAT_CPUTYPE( "max thread id",	PAT_MAX_THREAD_ID, mtid),	/* s390 */
	DEF_PAT_CPUTYPE( "model name",		PAT_MODEL_NAME,	modelname),
	DEF_PAT_CPUTYPE( "model",		PAT_MODEL,	model),
	DEF_PAT_CPUTYPE( "revision",		PAT_REVISION,	revision),
	DEF_PAT_CPUTYPE( "stepping",		PAT_STEPPING,	stepping),
	DEF_PAT_CPUTYPE( "type",		PAT_TYPE,	flags),		/* sparc64 */
	DEF_PAT_CPUTYPE( "vendor",		PAT_VENDOR,	vendor),
	DEF_PAT_CPUTYPE( "vendor_id",		PAT_VENDOR,	vendor),	/* s390 */
};

/*
 * /proc/cpuinfo to lscpu_cpu conversion
 */
#define DEF_PAT_CPU(_str, _id, _member) \
	{ \
		.id = (_id), \
		.pattern = (_str), \
		.offset = offsetof(struct lscpu_cpu, _member), \
	}

static const struct cpuinfo_pattern cpu_patterns[] =
{
	/* Sort by fields name! */
	DEF_PAT_CPU( "cpu MHz dynamic",	PAT_MHZ_DYNAMIC,dynamic_mhz),	/* s390 */
	DEF_PAT_CPU( "cpu MHz static",	PAT_MHZ_STATIC,	static_mhz),	/* s390 */
	DEF_PAT_CPU( "cpu MHz",		PAT_MHZ,	mhz),
	DEF_PAT_CPU( "cpu number",	PAT_PROCESSOR,  logical_id),	/* s390 */
        DEF_PAT_CPU( "processor",	PAT_PROCESSOR,	logical_id),
};

#define CPUTYPE_PATTERN_BUFSZ	32

static int cmp_pattern(const void *a0, const void *b0)
{
	const struct cpuinfo_pattern
		*a = (const struct cpuinfo_pattern *) a0,
		*b = (const struct cpuinfo_pattern *) b0;
	return strcmp(a->pattern, b->pattern);
}

static int cpuinfo_parse_line(	struct lscpu_cputype **ct,
				struct lscpu_cpu **cpu,
				const char *str)
{
	struct cpuinfo_pattern key, *pat;
	const char *p, *v;
	char buf[CPUTYPE_PATTERN_BUFSZ] = { 0 }, **data;
	void *stru = NULL;

	DBG(GATHER, ul_debug("parse \"%s\"", str));

	if (!str || !*str)
		return -EINVAL;
	p = skip_blank(str);
	if (!p || !*p)
		return -EINVAL;

	v = strchr(p, ':');
	if (!v || !*v)
		return -EINVAL;

	/* prepare name of the field */
	xstrncpy(buf, p, sizeof(buf));
	buf[v - p] = '\0';
	v++;

	rtrim_whitespace((unsigned char *)buf);

	/* search in cpu-types patterns */
	key.pattern = buf;
	pat = bsearch(&key, type_patterns,
			ARRAY_SIZE(type_patterns),
			sizeof(struct cpuinfo_pattern),
			cmp_pattern);
	if (pat) {
		/* CPU type */
		if (!*ct)
			*ct = lscpu_new_cputype();
		stru = *ct;
	} else {
		/* search in cpu patterns */
		pat = bsearch(&key, cpu_patterns,
			ARRAY_SIZE(cpu_patterns),
			sizeof(struct cpuinfo_pattern),
			cmp_pattern);
		if (pat) {
			if (!*cpu)
				*cpu = lscpu_new_cpu();
			stru = *cpu;
		}
	}

	if (!stru) {
		DBG(GATHER, ul_debug("'%s' not found", buf));
		return 1;
	}

	/* prepare value */
	v = skip_space(v);
	if (!v || !*v)
		return -EINVAL;

	/* copy value to struct */
	switch (pat->id) {
	case PAT_PROCESSOR:
		(*cpu)->logical_id = atoi(v);
		break;
	default:
		/* set value as a string and cleanup */
		strdup_to_offset(stru, pat->offset, v);
		data = (char **) ((char *) stru + pat->offset);
		rtrim_whitespace((unsigned char *) *data);
		break;
	}

	return 0;
}

int lscpu_read_cpuinfo(struct lscpu_cxt *cxt)
{
	struct lscpu_cputype *type = NULL;
	struct lscpu_cpu *cpu = NULL;
	FILE *fp;
	char buf[BUFSIZ];

	DBG(GATHER, ul_debugobj(cxt, "reading cpuinfo"));

	fp = ul_path_fopen(cxt->procfs, "r", "cpuinfo");
	if (!fp)
		err(EXIT_FAILURE, _("cannot open %s"), "/proc/cpuinfo");

	do {
		const char *p = NULL;

		if (fgets(buf, sizeof(buf), fp) != NULL)
			p = skip_space(buf);

		if (p == NULL || (*buf && !*p)) {
			if (cpu)
				lscpu_add_cpu(cxt, cpu, type);
			else if (type) {
				/* Generic non-cpu data. For some architectures
				 * cpuinfo contains description block (at the
				 * beginning of the file (IBM s390) or at the
				 * end of the file (IBM POWER). The block is
				 * global for all CPUs.
				 */
				if (cxt->ncputypes == 1) {
					/* The type already exist, merge it. For example on POWER
					 * CPU list contains "cpu:" line with architecture and
					 * global information at the end of the file */
					struct lscpu_cputype *dflt = lscpu_cputype_get_default(cxt);
					if (dflt)
						lscpu_merge_cputype(dflt, type);
				} else
					lscpu_add_cputype(cxt, type);
			}

			lscpu_unref_cpu(cpu);
			lscpu_unref_cputype(type);
			cpu = NULL, type = NULL;

			if (p == NULL)
				break;	/* fgets() returns nothing; EOF */
		} else {
			rtrim_whitespace((unsigned char *) buf);
			cpuinfo_parse_line(&type, &cpu, p);
		}
	} while (1);

	lscpu_unref_cpu(cpu);
	lscpu_unref_cputype(type);
	fclose(fp);
	return 0;
}

struct lscpu_arch *lscpu_read_architecture(struct lscpu_cxt *cxt)
{
	struct utsname utsbuf;
	struct lscpu_arch *ar;
	struct lscpu_cputype *ct;

	assert(cxt);

	DBG(GATHER, ul_debug("reading architecture"));

	if (uname(&utsbuf) == -1)
		err(EXIT_FAILURE, _("error: uname failed"));

	ar = xcalloc(1, sizeof(*cxt->arch));
	ar->name = xstrdup(utsbuf.machine);

	if (cxt->noalive)
		/* reading info from any /{sys,proc} dump, don't mix it with
		 * information about our real CPU */
		;
	else {
#if defined(__alpha__) || defined(__ia64__)
		ar->bit64 = 1;	/* 64bit platforms only */
#endif
		/* platforms with 64bit flag in /proc/cpuinfo, define
		 * 32bit default here */
#if defined(__i386__) || defined(__x86_64__) || \
    defined(__s390x__) || defined(__s390__) || defined(__sparc_v9__)
		ar->bit32 = 1;
#endif

#if defined(__aarch64__)
		{
			/* personality() is the most reliable way (since 4.7)
			 * to determine aarch32 support */
			int pers = personality(PER_LINUX32);
			if (pers != -1) {
				personality(pers);
				ar->bit32 = 1;
			}
			ar->bit64 = 1;
		}
#endif
	}

	ct = lscpu_cputype_get_default(cxt);
	if (ct && ct->flags) {
		char buf[BUFSIZ];

		snprintf(buf, sizeof(buf), " %s ", ct->flags);
		if (strstr(buf, " lm "))
			ar->bit32 = 1, ar->bit64 = 1;			/* x86_64 */
		if (strstr(buf, " zarch "))
			ar->bit32 = 1, ar->bit64 = 1;			/* s390x */
		if (strstr(buf, " sun4v ") || strstr(buf, " sun4u "))
			ar->bit32 = 1, ar->bit64 = 1;			/* sparc64 */
	}

	if (ar->name && !cxt->noalive) {
		if (strcmp(ar->name, "ppc64") == 0)
			ar->bit32 = 1, ar->bit64 = 1;
		else if (strcmp(ar->name, "ppc") == 0)
			ar->bit32 = 1;
	}

	DBG(GATHER, ul_debugobj(ar, "arch: name=%s %s %s",
				ar->name,
				ar->bit64 ? "64-bit" : "",
				ar->bit64 ? "32-bit" : ""));
	return ar;
}

void lscpu_free_architecture(struct lscpu_arch *ar)
{
	if (!ar)
		return;
	free(ar->name);
	free(ar);
}

int lscpu_read_cpulists(struct lscpu_cxt *cxt)
{
	size_t maxn;
	size_t setsize;
	cpu_set_t *cpuset = NULL;

	assert(cxt);
	DBG(GATHER, ul_debugobj(cxt, "reading cpulists"));

	if (ul_path_read_s32(cxt->syscpu, &cxt->maxcpus, "kernel_max") == 0)
		/* note that kernel_max is maximum index [NR_CPUS-1] */
		cxt->maxcpus += 1;

	else if (!cxt->noalive)
		/* the root is '/' so we are working with data from the current kernel */
		cxt->maxcpus = get_max_number_of_cpus();

	if (cxt->maxcpus <= 0)
		/* error or we are reading some /sys snapshot instead of the
		 * real /sys, let's use any crazy number... */
		cxt->maxcpus = 2048;

	maxn = cxt->maxcpus;
	setsize = CPU_ALLOC_SIZE(maxn);

	if (ul_path_readf_cpulist(cxt->syscpu, &cpuset, maxn, "possible") == 0) {
		size_t num, idx;

		cxt->ncpuspos = CPU_COUNT_S(setsize, cpuset);
		cxt->idx2cpunum = xcalloc(cxt->ncpuspos, sizeof(int));

		for (num = 0, idx = 0; num < maxn; num++) {
			if (CPU_ISSET_S(num, setsize, cpuset))
				cxt->idx2cpunum[idx++] = num;
		}
		cpuset_free(cpuset);
		cpuset = NULL;
	} else
		err(EXIT_FAILURE, _("failed to determine number of CPUs: %s"),
				_PATH_SYS_CPU "/possible");


	/* get mask for present CPUs */
	if (ul_path_readf_cpulist(cxt->syscpu, &cxt->present, maxn, "present") == 0)
		cxt->npresents = CPU_COUNT_S(setsize, cxt->present);

	/* get mask for online CPUs */
	if (ul_path_readf_cpulist(cxt->syscpu, &cxt->online, maxn, "online") == 0)
		cxt->nonlines = CPU_COUNT_S(setsize, cxt->online);

	return 0;
}

#if defined(HAVE_LIBRTAS)
# define PROCESSOR_MODULE_INFO	43
static int strbe16toh(const char *buf, int offset)
{
	return (buf[offset] << 8) + buf[offset+1];
}
#endif

/* some extra information for the default CPU type */
int lscpu_read_archext(struct lscpu_cxt *cxt)
{
	FILE *f;
	char buf[BUFSIZ];
	struct lscpu_cputype *ct;

	assert(cxt);
	ct = lscpu_cputype_get_default(cxt);
	if (!ct)
		return -EINVAL;

	/* get dispatching mode */
	if (ul_path_read_s32(cxt->syscpu, &ct->dispatching, "dispatching") != 0)
		ct->dispatching = -1;

	/* get cpufreq boost mode */
	if (ul_path_read_s32(cxt->syscpu, &ct->freqboost, "cpufreq/boost") != 0)
		ct->freqboost = -1;

	if ((f = ul_path_fopen(cxt->procfs, "r", "sysinfo"))) {
		while (fgets(buf, sizeof(buf), f) != NULL) {
			if (lookup(buf, "Type", &ct->machinetype))
				break;
		}
		fclose(f);
	}

#if defined(HAVE_LIBRTAS)
	/* Get PowerPC speficic info */
	if (!cxt->noalive) {
		int rc, len, ntypes;

		ct->physsockets = ct->physchips = ct->physcoresperchip = 0;

		rc = rtas_get_sysparm(PROCESSOR_MODULE_INFO, sizeof(buf), buf);
		if (rc < 0)
			goto nortas;

		len = strbe16toh(buf, 0);
		if (len < 8)
			goto nortas;

		ntypes = strbe16toh(buf, 2);
		assert(ntypes <= 1);
		if (!ntypes)
			goto nortas;

		ct->physsockets = strbe16toh(buf, 4);
		ct->physchips = strbe16toh(buf, 6);
		ct->physcoresperchip = strbe16toh(buf, 8);
nortas:
	}
#endif
	return 0;
}

static int cmp_vulnerability_name(const void *a0, const void *b0)
{
	const struct lscpu_vulnerability
			*a = (const struct lscpu_vulnerability *) a0,
			*b = (const struct lscpu_vulnerability *) b0;
	return strcmp(a->name, b->name);
}

int lscpu_read_vulnerabilities(struct lscpu_cxt *cxt)
{
	struct dirent *d;
	DIR *dir;
	size_t n = 0;

	assert(cxt);

	dir = ul_path_opendir(cxt->syscpu, "vulnerabilities");
	if (!dir)
		return 0;

	cxt->nvuls = n = 0;
	while (xreaddir(dir))
		n++;
	if (!n)
		return 0;

	rewinddir(dir);
	cxt->vuls = xcalloc(n, sizeof(struct lscpu_vulnerability));

	while (cxt->nvuls < n && (d = xreaddir(dir))) {
		char *str, *p;
		struct lscpu_vulnerability *vu;

#ifdef _DIRENT_HAVE_D_TYPE
		if (d->d_type == DT_DIR || d->d_type == DT_UNKNOWN)
			continue;
#endif
		if (ul_path_readf_string(cxt->syscpu, &str,
					"vulnerabilities/%s", d->d_name) <= 0)
			continue;

		vu = &cxt->vuls[cxt->nvuls++];

		/* Name */
		vu->name = xstrdup(d->d_name);
		*vu->name = toupper(*vu->name);
		strrep(vu->name, '_', ' ');

		/* Description */
		vu->text = str;
		p = (char *) startswith(vu->text, "Mitigation");
		if (p) {
			*p = ';';
			strrem(vu->text, ':');
		}
	}
	closedir(dir);

	qsort(cxt->vuls, cxt->nvuls,
	      sizeof(struct lscpu_vulnerability), cmp_vulnerability_name);

	return 0;
}

static inline int is_node_dirent(struct dirent *d)
{
	return
		d &&
#ifdef _DIRENT_HAVE_D_TYPE
		(d->d_type == DT_DIR || d->d_type == DT_UNKNOWN) &&
#endif
		strncmp(d->d_name, "node", 4) == 0 &&
		isdigit_string(d->d_name + 4);
}

static int nodecmp(const void *ap, const void *bp)
{
	int *a = (int *) ap, *b = (int *) bp;
	return *a - *b;
}

int lscpu_read_numas(struct lscpu_cxt *cxt)
{
	size_t i = 0;
	DIR *dir;
	struct dirent *d;
	struct path_cxt *sys;

	assert(!cxt->nnodes);

	sys = ul_new_path(_PATH_SYS_NODE);
	if (!sys)
		err(EXIT_FAILURE, _("failed to initialize %s handler"), _PATH_SYS_NODE);

	ul_path_set_prefix(sys, cxt->prefix);

	dir = ul_path_opendir(sys, NULL);
	if (!dir)
		goto done;

	while ((d = readdir(dir))) {
		if (is_node_dirent(d))
			cxt->nnodes++;
	}

	if (!cxt->nnodes) {
		closedir(dir);
		goto done;
	}

	cxt->nodemaps = xcalloc(cxt->nnodes, sizeof(cpu_set_t *));
	cxt->idx2nodenum = xmalloc(cxt->nnodes * sizeof(int));

	rewinddir(dir);
	for (i = 0; (d = readdir(dir)) && i < cxt->nnodes; i++) {
		if (is_node_dirent(d))
			cxt->idx2nodenum[i] = strtol_or_err(((d->d_name) + 4),
						_("Failed to extract the node number"));
	}
	closedir(dir);
	qsort(cxt->idx2nodenum, cxt->nnodes, sizeof(int), nodecmp);

	/* information about how nodes share different CPUs */
	for (i = 0; i < cxt->nnodes; i++)
		ul_path_readf_cpuset(sys, &cxt->nodemaps[i], cxt->maxcpus,
				"node%d/cpumap", cxt->idx2nodenum[i]);
done:
	ul_unref_path(sys);
	return 0;
}

#ifdef TEST_PROGRAM_CPUTYPE
/* TODO: move to lscpu.c */
struct lscpu_cxt *lscpu_new_context(void)
{
	return xcalloc(1, sizeof(struct lscpu_cxt));
}

void lscpu_free_context(struct lscpu_cxt *cxt)
{
	size_t i;

	if (!cxt)
		return;

	DBG(MISC, ul_debugobj(cxt, "freeing context"));

	DBG(MISC, ul_debugobj(cxt, " de-initialize paths"));
	ul_unref_path(cxt->syscpu);
	ul_unref_path(cxt->procfs);

	DBG(MISC, ul_debugobj(cxt, " freeing cpus"));
	for (i = 0; i < cxt->ncpus; i++)
		lscpu_unref_cpu(cxt->cpus[i]);

	DBG(MISC, ul_debugobj(cxt, " freeing types"));
	for (i = 0; i < cxt->ncputypes; i++)
		lscpu_unref_cputype(cxt->cputypes[i]);

	free(cxt->idx2cpunum);
	free(cxt->present);
	free(cxt->online);
	free(cxt->cputypes);
	free(cxt->cpus);

	for (i = 0; i < cxt->nvuls; i++) {
		free(cxt->vuls[i].name);
		free(cxt->vuls[i].text);
	}
	free(cxt->vuls);

	for (i = 0; i < cxt->nnodes; i++)
		free(cxt->nodemaps[i]);

	free(cxt->nodemaps);
	free(cxt->idx2nodenum);

	lscpu_free_virtualization(cxt->virt);
	lscpu_free_architecture(cxt->arch);

	free(cxt);
}

int main(int argc, char **argv)
{
	struct lscpu_cxt *cxt;

	cxt = lscpu_new_context();

	if (argc == 3 && strcmp(argv[1], "--prefix") == 0) {
		cxt->prefix = argv[2];
		cxt->noalive = 1;
	}

	lscpu_init_debug();
	context_init_paths(cxt);

	lscpu_read_cpuinfo(cxt);
	cxt->arch = lscpu_read_architecture(cxt);

	lscpu_read_cpulists(cxt);
	lscpu_read_archext(cxt);
	lscpu_read_vulnerabilities(cxt);
	lscpu_read_numas(cxt);
	lscpu_read_topology(cxt);
	lscpu_read_topolgy_ids(cxt);

	lscpu_decode_arm(cxt);

	cxt->virt = lscpu_read_virtualization(cxt);

	lscpu_free_context(cxt);
	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_CPUTYPES */
