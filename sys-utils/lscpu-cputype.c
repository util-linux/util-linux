
#include "c.h"
#include "nls.h"
#include "cpuset.h"
#include "xalloc.h"
#include "pathnames.h"
#include "path.h"
#include "strutils.h"

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

	size_t	ncputypes;
	struct lscpu_cputype *cputypes;
};


static void context_init_paths(struct lscpu_cxt *cxt)
{
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

static void free_context(struct lscpu_cxt *cxt)
{
	if (!cxt)
		return;

	ul_unref_path(cxt->syscpu);
	ul_unref_path(cxt->procfs);
}

static struct lscpu_cputype *lscpu_new_cputype(void)
{
	struct lscpu_cputype *ct;

	ct = xcalloc(1, sizeof(struct lscpu_cputype));
	ct->refcount = 1;

	return ct;
}

int lscpu_read_cputypes(struct lscpu_cxt *cxt)
{
	return 0;
}

#ifdef TEST_PROGRAM_CPUTYPE
int main(int argc, char **argv)
{
	struct lscpu_cxt _cxt = { .prefix = NULL },
			 *cxt = &_cxt;

	if (argc == 3 && strcmp(argv[1], "--prefix") == 0)
		cxt->prefix = argv[2];

	context_init_paths(cxt);

	free_context(cxt);

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_CPUTYPES */
