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
#include "debug.h"

#define LSCPU_DEBUG_INIT	(1 << 1)
#define LSCPU_DEBUG_MISC	(1 << 2)
#define LSCPU_DEBUG_GATHER	(1 << 3)
#define LSCPU_DEBUG_TYPE	(1 << 4)
#define LSCPU_DEBUG_CPU		(1 << 5)
#define LSCPU_DEBUG_VIRT	(1 << 6)
#define LSBLK_DEBUG_ALL		0xFFFF

UL_DEBUG_DECLARE_MASK(lscpu);
#define DBG(m, x)       __UL_DBG(lscpu, LSCPU_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(lscpu, LSCPU_DEBUG_, m, x)

#define UL_DEBUG_CURRENT_MASK	UL_DEBUG_MASK(lscpu)
#include "debugobj.h"

#define _PATH_SYS_SYSTEM	"/sys/devices/system"
#define _PATH_SYS_HYP_FEATURES	"/sys/hypervisor/properties/features"
#define _PATH_SYS_CPU		_PATH_SYS_SYSTEM "/cpu"
#define _PATH_SYS_NODE		_PATH_SYS_SYSTEM "/node"
#define _PATH_SYS_DMI		"/sys/firmware/dmi/tables/DMI"
#define _PATH_SYS_DMI_TYPE4	"/sys/firmware/dmi/entries/4-0/raw"

struct lscpu_cache {
	int		nth;		/* cache<number> from cpuinfo */
	char		*name;
	char		*type;
	char		*allocation_policy;
	char		*write_policy;

	int		level;
	uint64_t	size;

	unsigned int	ways_of_associativity;
	unsigned int	physical_line_partition;
	unsigned int	number_of_sets;
	unsigned int	coherency_line_size;

	int		nsharedmaps;
	cpu_set_t	**sharedmaps;
};

struct lscpu_cputype {
	int	refcount;

	char	*vendor;
	char	*machinetype;	/* s390 */
	char	*family;
	char	*model;
	char	*modelname;
	char	*revision;	/* alternative for model (ppc) */
	char	*stepping;
	char    *bogomips;
	char	*flags;
	char	*mtid;		/* maximum thread id (s390) */
	char	*addrsz;	/* address sizes */
	int	dispatching;	/* -1 if not evailable, DIST_* */
	int	freqboost;	/* -1 if not evailable */

	int	physsockets;	/* Physical sockets (modules) */
	int	physchips;	/* Physical chips */
	int	physcoresperchip;	/* Physical cores per chip */

	int	nthreads_per_core;
	int	ncores_per_socket;
	int	nsockets_per_book;
	int	nbooks_per_drawer;
	int	ndrawers_per_system;

	struct lscpu_cache *caches;
	size_t ncaches;

	/* siblings maps */
	int		ncores;
	cpu_set_t	**coremaps;
	int		nsockets;
	cpu_set_t       **socketmaps;
	int		nbooks;
	cpu_set_t	**bookmaps;
	int		ndrawers;
	cpu_set_t	**drawermaps;
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

struct lscpu_cpu {
	int refcount;
	struct lscpu_cputype *type;

	int logical_id;

	char	*mhz;		/* max freq from cpuinfo */
	char	*dynamic_mhz;   /* from cpuinf for s390 */
	char	*static_mhz;	/* from cpuinf for s390 */
	float	mhz_max_freq;	/* realtime freq from /sys/.../cpuinfo_max_freq */
	float	mhz_min_freq;	/* realtime freq from /sys/.../cpuinfo_min_freq */

	int	coreid;
	int	socketid;
	int	bookid;
	int	drawerid;

	int	polarization;	/* POLAR_* */
	int	address;	/* physical cpu address */
	int	configured;	/* cpu configured */
};

struct lscpu_arch {
	char	*name;		/* uname() .machine */

	unsigned int	bit32:1,
			bit64:1;
};

struct lscpu_vulnerability {
	char	*name;
	char	*text;
};

/* virtualization types */
enum {
	VIRT_TYPE_NONE	= 0,
	VIRT_TYPE_PARA,
	VIRT_TYPE_FULL,
	VIRT_TYPE_CONTAINER
};

/* hypervisor vendors */
enum {
	VIRT_VENDOR_NONE	= 0,
	VIRT_VENDOR_XEN,
	VIRT_VENDOR_KVM,
	VIRT_VENDOR_MSHV,
	VIRT_VENDOR_VMWARE,
	VIRT_VENDOR_IBM,		/* sys-z powervm */
	VIRT_VENDOR_VSERVER,
	VIRT_VENDOR_UML,
	VIRT_VENDOR_INNOTEK,		/* VBOX */
	VIRT_VENDOR_HITACHI,
	VIRT_VENDOR_PARALLELS,	/* OpenVZ/VIrtuozzo */
	VIRT_VENDOR_VBOX,
	VIRT_VENDOR_OS400,
	VIRT_VENDOR_PHYP,
	VIRT_VENDOR_SPAR,
	VIRT_VENDOR_WSL,
};

struct lscpu_virt {
	char	*cpuflag;	/* virtualization flag (vmx, svm) */
	char	*hypervisor;	/* hypervisor software */
	int	vendor;		/* VIRT_VENDOR_* */
	int	type;		/* VIRT_TYPE_* ? */

};

struct lscpu_cxt {
	int maxcpus;		/* size in bits of kernel cpu mask */
	const char *prefix;	/* path to /sys and /proc snapshot or NULL */

	struct path_cxt	*syscpu; /* _PATH_SYS_CPU path handler */
	struct path_cxt *procfs; /* /proc path handler */

	size_t ncputypes;
	struct lscpu_cputype **cputypes;

	size_t npossibles;	/* number of possible CPUs */
	struct lscpu_cpu **cpus; /* possible CPUs, contains gaps (cups[n]=NULL) */

	size_t npresents;
	cpu_set_t *present;	/* mask with present CPUs */

	size_t nonlines;	/* aka number of trhreads */
	cpu_set_t *online;	/* mask with online CPUs */

	struct lscpu_arch *arch;
	struct lscpu_virt *virt;

	struct lscpu_vulnerability *vuls;	/* array of CPU vulnerabilities */
	size_t  nvuls;				/* number of CPU vulnerabilities */


	struct lscpu_cache *ecaches;
	size_t necaches;		/* extra caches (s390) from /proc/cpuinfo */

	size_t nnodes;		/* number of NUMA modes */
	int *idx2nodenum;	/* Support for discontinuous nodes */
	cpu_set_t **nodemaps;	/* array with NUMA nodes */

	unsigned int noalive : 1;
};

struct lscpu_cputype *lscpu_new_cputype(void);
void lscpu_ref_cputype(struct lscpu_cputype *ct);
void lscpu_unref_cputype(struct lscpu_cputype *ct);
struct lscpu_cputype *lscpu_add_cputype(struct lscpu_cxt *cxt, struct lscpu_cputype *ct);
struct lscpu_cputype *lscpu_cputype_get_default(struct lscpu_cxt *cxt);

int lscpu_read_cpuinfo(struct lscpu_cxt *cxt);
int lscpu_read_cpulists(struct lscpu_cxt *cxt);
int lscpu_read_archext(struct lscpu_cxt *cxt);
int lscpu_read_vulnerabilities(struct lscpu_cxt *cxt);
int lscpu_read_numas(struct lscpu_cxt *cxt);

void lscpu_free_caches(struct lscpu_cache *caches, size_t n);
void lscpu_sort_caches(struct lscpu_cache *caches, size_t n);

int lscpu_read_topology(struct lscpu_cxt *cxt);
void lscpu_cputype_free_topology(struct lscpu_cputype *ct);

struct lscpu_arch *lscpu_read_architecture(struct lscpu_cxt *cxt);
void lscpu_free_architecture(struct lscpu_arch *ar);

struct lscpu_virt *lscpu_read_virtualization(struct lscpu_cxt *cxt);
void lscpu_free_virtualization(struct lscpu_virt *virt);

struct lscpu_cpu *lscpu_new_cpu(int id);
void lscpu_ref_cpu(struct lscpu_cpu *cpu);
void lscpu_unref_cpu(struct lscpu_cpu *cpu);
struct lscpu_cpu *lscpu_get_cpu(struct lscpu_cxt *cxt, int logical_id);
int lscpu_cpu_set_type(struct lscpu_cpu *cpu, struct lscpu_cputype *type);
int lscpu_create_cpus(struct lscpu_cxt *cxt, cpu_set_t *cpuset, size_t setsize);
struct lscpu_cpu *lscpu_cpus_loopup_by_type(struct lscpu_cxt *cxt, struct lscpu_cputype *ct);

void lscpu_decode_arm(struct lscpu_cxt *cxt);

struct lscpu_cxt *lscpu_new_context(void);
void lscpu_free_context(struct lscpu_cxt *cxt);

int lookup(char *line, char *pattern, char **value);

struct lscpu_dmi_header
{
	uint8_t type;
	uint8_t length;
	uint16_t handle;
	uint8_t *data;
};

static inline void to_dmi_header(struct lscpu_dmi_header *h, uint8_t *data)
{
	h->type = data[0];
	h->length = data[1];
	memcpy(&h->handle, data + 2, sizeof(h->handle));
	h->data = data;
}

static inline char *dmi_string(const struct lscpu_dmi_header *dm, uint8_t s)
{
	char *bp = (char *)dm->data;

	if (!s || !bp)
		return NULL;

	bp += dm->length;
	while (s > 1 && *bp) {
		bp += strlen(bp);
		bp++;
		s--;
	}

	return !*bp ? NULL : bp;
}
#endif /* LSCPU_H */
