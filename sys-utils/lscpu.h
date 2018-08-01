#ifndef LSCPU_H
#define LSCPU_H

#include "c.h"
#include "nls.h"
#include "cpuset.h"
#include "xalloc.h"
#include "strutils.h"
#include "bitops.h"
#include "path.h"
#include "pathnames.h"
#include "all-io.h"

/* virtualization types */
enum {
	VIRT_NONE	= 0,
	VIRT_PARA,
	VIRT_FULL,
	VIRT_CONT
};

/* hypervisor vendors */
enum {
	HYPER_NONE	= 0,
	HYPER_XEN,
	HYPER_KVM,
	HYPER_MSHV,
	HYPER_VMWARE,
	HYPER_IBM,		/* sys-z powervm */
	HYPER_VSERVER,
	HYPER_UML,
	HYPER_INNOTEK,		/* VBOX */
	HYPER_HITACHI,
	HYPER_PARALLELS,	/* OpenVZ/VIrtuozzo */
	HYPER_VBOX,
	HYPER_OS400,
	HYPER_PHYP,
	HYPER_SPAR,
	HYPER_WSL,
};

/* CPU modes */
enum {
	MODE_32BIT	= (1 << 1),
	MODE_64BIT	= (1 << 2)
};

/* cache(s) description */
struct cpu_cache {
	char		*name;
	char		*size;

	int		nsharedmaps;
	cpu_set_t	**sharedmaps;
};

/* dispatching modes */
enum {
	DISP_HORIZONTAL = 0,
	DISP_VERTICAL	= 1
};

/* cpu polarization */
enum {
	POLAR_UNKNOWN	= 0,
	POLAR_VLOW,
	POLAR_VMEDIUM,
	POLAR_VHIGH,
	POLAR_HORIZONTAL
};

struct polarization_modes {
	char *parsable;
	char *readable;
};


/* global description */
struct lscpu_desc {
	const char *prefix;	 /* path to /sys and /proc snapshot or NULL */

	struct path_cxt	*syscpu; /* _PATH_SYS_CPU path handler */
	struct path_cxt *procfs; /* /proc path handler */

	char	*arch;
	char	*vendor;
	char	*machinetype;	/* s390 */
	char	*family;
	char	*model;
	char	*modelname;
	char	*revision;  /* alternative for model (ppc) */
	char	*cpu;       /* alternative for modelname (ppc, sparc) */
	char	*virtflag;	/* virtualization flag (vmx, svm) */
	char	*hypervisor;	/* hypervisor software */
	int	hyper;		/* hypervisor vendor ID */
	int	virtype;	/* VIRT_PARA|FULL|NONE ? */
	char	*mhz;
	char	*dynamic_mhz;	/* dynamic mega hertz (s390) */
	char	*static_mhz;	/* static mega hertz (s390) */
	char	**maxmhz;	/* maximum mega hertz */
	char	**minmhz;	/* minimum mega hertz */
	char	*stepping;
	char    *bogomips;
	char	*flags;
	char	*mtid;		/* maximum thread id (s390) */
	char	*addrsz;	/* address sizes */
	int	dispatching;	/* none, horizontal or vertical */
	int	mode;		/* rm, lm or/and tm */

	int		ncpuspos;	/* maximal possible CPUs */
	int		ncpus;		/* number of present CPUs */
	cpu_set_t	*present;	/* mask with present CPUs */
	cpu_set_t	*online;	/* mask with online CPUs */

	int		nthreads;	/* number of online threads */

	int		ncaches;
	struct cpu_cache *caches;

	int		necaches;	/* extra caches (s390) */
	struct cpu_cache *ecaches;

	/*
	 * All maps are sequentially indexed (0..ncpuspos), the array index
	 * does not have match with cpuX number as presented by kernel. You
	 * have to use real_cpu_num() to get the real cpuX number.
	 *
	 * For example, the possible system CPUs are: 1,3,5, it means that
	 * ncpuspos=3, so all arrays are in range 0..3.
	 */
	int		*idx2cpunum;	/* mapping index to CPU num */

	int		nnodes;		/* number of NUMA modes */
	int		*idx2nodenum;	/* Support for discontinuous nodes */
	cpu_set_t	**nodemaps;	/* array with NUMA nodes */

	/* drawers -- based on drawer_siblings (internal kernel map of cpuX's
	 * hardware threads within the same drawer */
	int		ndrawers;	/* number of all online drawers */
	cpu_set_t	**drawermaps;	/* unique drawer_siblings */
	int		*drawerids;	/* physical drawer ids */

	/* books -- based on book_siblings (internal kernel map of cpuX's
	 * hardware threads within the same book */
	int		nbooks;		/* number of all online books */
	cpu_set_t	**bookmaps;	/* unique book_siblings */
	int		*bookids;	/* physical book ids */

	/* sockets -- based on core_siblings (internal kernel map of cpuX's
	 * hardware threads within the same physical_package_id (socket)) */
	int		nsockets;	/* number of all online sockets */
	cpu_set_t	**socketmaps;	/* unique core_siblings */
	int		*socketids;	/* physical socket ids */

	/* cores -- based on thread_siblings (internal kernel map of cpuX's
	 * hardware threads within the same core as cpuX) */
	int		ncores;		/* number of all online cores */
	cpu_set_t	**coremaps;	/* unique thread_siblings */
	int		*coreids;	/* physical core ids */

	int		*polarization;	/* cpu polarization */
	int		*addresses;	/* physical cpu addresses */
	int		*configured;	/* cpu configured */
	int		physsockets;	/* Physical sockets (modules) */
	int		physchips;	/* Physical chips */
	int		physcoresperchip;	/* Physical cores per chip */
};

enum {
	OUTPUT_SUMMARY	= 0,	/* default */
	OUTPUT_PARSABLE,	/* -p */
	OUTPUT_READABLE,	/* -e */
};

enum {
	SYSTEM_LIVE = 0,	/* analyzing a live system */
	SYSTEM_SNAPSHOT,	/* analyzing a snapshot of a different system */
};

struct lscpu_modifier {
	int		mode;		/* OUTPUT_* */
	int		system;		/* SYSTEM_* */
	unsigned int	hex:1,		/* print CPU masks rather than CPU lists */
			compat:1,	/* use backwardly compatible format */
			online:1,	/* print online CPUs */
			offline:1,	/* print offline CPUs */
			json:1,		/* JSON output format */
			physical:1;	/* use physical numbers */
};

extern int read_hypervisor_dmi(void);
extern void arm_cpu_decode(struct lscpu_desc *desc);

#endif /* LSCPU_H */
