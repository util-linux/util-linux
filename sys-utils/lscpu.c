/*
 * lscpu - CPU architecture information helper
 *
 * Copyright (C) 2008 Cai Qian <qcai@redhat.com>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>

#if (defined(__x86_64__) || defined(__i386__))
# if !defined( __SANITIZE_ADDRESS__)
#  define INCLUDE_VMWARE_BDOOR
# else
#  warning VMWARE detection disabled by __SANITIZE_ADDRESS__
# endif
#endif

#ifdef INCLUDE_VMWARE_BDOOR
# include <stdint.h>
# include <signal.h>
# include <strings.h>
# include <setjmp.h>
# ifdef HAVE_SYS_IO_H
#  include <sys/io.h>
# endif
#endif

#if defined(HAVE_LIBRTAS)
#include <librtas.h>
#endif

#include <libsmartcols.h>

#include "closestream.h"
#include "optutils.h"

#include "lscpu.h"

#define CACHE_MAX 100

/* /sys paths */
#define _PATH_SYS_SYSTEM	"/sys/devices/system"
#define _PATH_SYS_HYP_FEATURES	"/sys/hypervisor/properties/features"
#define _PATH_SYS_CPU		_PATH_SYS_SYSTEM "/cpu"
#define _PATH_SYS_NODE		_PATH_SYS_SYSTEM "/node"

/* Xen Domain feature flag used for /sys/hypervisor/properties/features */
#define XENFEAT_supervisor_mode_kernel		3
#define XENFEAT_mmu_pt_update_preserve_ad	5
#define XENFEAT_hvm_callback_vector			8

#define XEN_FEATURES_PV_MASK	(1U << XENFEAT_mmu_pt_update_preserve_ad)
#define XEN_FEATURES_PVH_MASK	( (1U << XENFEAT_supervisor_mode_kernel) \
								| (1U << XENFEAT_hvm_callback_vector) )

static const char *virt_types[] = {
	[VIRT_NONE]	= N_("none"),
	[VIRT_PARA]	= N_("para"),
	[VIRT_FULL]	= N_("full"),
	[VIRT_CONT]	= N_("container"),
};

static const char *hv_vendors[] = {
	[HYPER_NONE]	= NULL,
	[HYPER_XEN]	= "Xen",
	[HYPER_KVM]	= "KVM",
	[HYPER_MSHV]	= "Microsoft",
	[HYPER_VMWARE]  = "VMware",
	[HYPER_IBM]	= "IBM",
	[HYPER_VSERVER]	= "Linux-VServer",
	[HYPER_UML]	= "User-mode Linux",
	[HYPER_INNOTEK]	= "Innotek GmbH",
	[HYPER_HITACHI]	= "Hitachi",
	[HYPER_PARALLELS] = "Parallels",
	[HYPER_VBOX]	= "Oracle",
	[HYPER_OS400]	= "OS/400",
	[HYPER_PHYP]	= "pHyp",
	[HYPER_SPAR]	= "Unisys s-Par",
	[HYPER_WSL]	= "Windows Subsystem for Linux"
};

static const int hv_vendor_pci[] = {
	[HYPER_NONE]	= 0x0000,
	[HYPER_XEN]	= 0x5853,
	[HYPER_KVM]	= 0x0000,
	[HYPER_MSHV]	= 0x1414,
	[HYPER_VMWARE]	= 0x15ad,
	[HYPER_VBOX]	= 0x80ee,
};

static const int hv_graphics_pci[] = {
	[HYPER_NONE]	= 0x0000,
	[HYPER_XEN]	= 0x0001,
	[HYPER_KVM]	= 0x0000,
	[HYPER_MSHV]	= 0x5353,
	[HYPER_VMWARE]	= 0x0710,
	[HYPER_VBOX]	= 0xbeef,
};


/* dispatching modes */
static const char *disp_modes[] = {
	[DISP_HORIZONTAL]	= N_("horizontal"),
	[DISP_VERTICAL]		= N_("vertical")
};

static struct polarization_modes polar_modes[] = {
	[POLAR_UNKNOWN]	   = {"U",  "-"},
	[POLAR_VLOW]	   = {"VL", "vert-low"},
	[POLAR_VMEDIUM]	   = {"VM", "vert-medium"},
	[POLAR_VHIGH]	   = {"VH", "vert-high"},
	[POLAR_HORIZONTAL] = {"H",  "horizontal"},
};

static int maxcpus;		/* size in bits of kernel cpu mask */

#define is_cpu_online(_d, _cpu) \
	((_d) && (_d)->online ? \
		CPU_ISSET_S((_cpu), CPU_ALLOC_SIZE(maxcpus), (_d)->online) : 0)
#define is_cpu_present(_d, _cpu) \
	((_d) && (_d)->present ? \
		CPU_ISSET_S((_cpu), CPU_ALLOC_SIZE(maxcpus), (_d)->present) : 0)

#define real_cpu_num(_d, _i)	((_d)->idx2cpunum[(_i)])

/*
 * IDs
 */
enum {
	COL_CPU,
	COL_CORE,
	COL_SOCKET,
	COL_NODE,
	COL_BOOK,
	COL_DRAWER,
	COL_CACHE,
	COL_POLARIZATION,
	COL_ADDRESS,
	COL_CONFIGURED,
	COL_ONLINE,
	COL_MAXMHZ,
	COL_MINMHZ,
};

/* column description
 */
struct lscpu_coldesc {
	const char *name;
	const char *help;

	unsigned int  is_abbr:1;	/* name is abbreviation */
};

static struct lscpu_coldesc coldescs[] =
{
	[COL_CPU]          = { "CPU", N_("logical CPU number"), 1 },
	[COL_CORE]         = { "CORE", N_("logical core number") },
	[COL_SOCKET]       = { "SOCKET", N_("logical socket number") },
	[COL_NODE]         = { "NODE", N_("logical NUMA node number") },
	[COL_BOOK]         = { "BOOK", N_("logical book number") },
	[COL_DRAWER]       = { "DRAWER", N_("logical drawer number") },
	[COL_CACHE]        = { "CACHE", N_("shows how caches are shared between CPUs") },
	[COL_POLARIZATION] = { "POLARIZATION", N_("CPU dispatching mode on virtual hardware") },
	[COL_ADDRESS]      = { "ADDRESS", N_("physical address of a CPU") },
	[COL_CONFIGURED]   = { "CONFIGURED", N_("shows if the hypervisor has allocated the CPU") },
	[COL_ONLINE]       = { "ONLINE", N_("shows if Linux currently makes use of the CPU") },
	[COL_MAXMHZ]	   = { "MAXMHZ", N_("shows the maximum MHz of the CPU") },
	[COL_MINMHZ]	   = { "MINMHZ", N_("shows the minimum MHz of the CPU") }
};

static int
column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(coldescs); i++) {
		const char *cn = coldescs[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

/* Lookup a pattern and get the value from cpuinfo.
 * Format is:
 *
 *	"<pattern>   : <key>"
 */
static int
lookup(char *line, char *pattern, char **value)
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

/* Parse extra cache lines contained within /proc/cpuinfo but which are not
 * part of the cache topology information within the sysfs filesystem.
 * This is true for all shared caches on e.g. s390. When there are layers of
 * hypervisors in between it is not knows which CPUs share which caches.
 * Therefore information about shared caches is only available in
 * /proc/cpuinfo.
 * Format is:
 * "cache<nr> : level=<lvl> type=<type> scope=<scope> size=<size> line_size=<lsz> associativity=<as>"
 */
static int
lookup_cache(char *line, struct lscpu_desc *desc)
{
	struct cpu_cache *cache;
	long long size;
	char *p, type;
	int level;

	/* Make sure line starts with "cache<nr> :" */
	if (strncmp(line, "cache", 5))
		return 0;
	for (p = line + 5; isdigit(*p); p++);
	for (; isspace(*p); p++);
	if (*p != ':')
		return 0;

	p = strstr(line, "scope=") + 6;
	/* Skip private caches, also present in sysfs */
	if (!p || strncmp(p, "Private", 7) == 0)
		return 0;
	p = strstr(line, "level=");
	if (!p || sscanf(p, "level=%d", &level) != 1)
		return 0;
	p = strstr(line, "type=") + 5;
	if (!p || !*p)
		return 0;
	type = 0;
	if (strncmp(p, "Data", 4) == 0)
		type = 'd';
	if (strncmp(p, "Instruction", 11) == 0)
		type = 'i';
	p = strstr(line, "size=");
	if (!p || sscanf(p, "size=%lld", &size) != 1)
	       return 0;

	desc->necaches++;
	desc->ecaches = xrealloc(desc->ecaches,
				 desc->necaches * sizeof(struct cpu_cache));
	cache = &desc->ecaches[desc->necaches - 1];
	memset(cache, 0 , sizeof(*cache));
	if (type)
		xasprintf(&cache->name, "L%d%c", level, type);
	else
		xasprintf(&cache->name, "L%d", level);
	xasprintf(&cache->size, "%lldK", size);
	return 1;
}

/* Don't init the mode for platforms where we are not able to
 * detect that CPU supports 64-bit mode.
 */
static int
init_mode(struct lscpu_modifier *mod)
{
	int m = 0;

	if (mod->system == SYSTEM_SNAPSHOT)
		/* reading info from any /{sys,proc} dump, don't mix it with
		 * information about our real CPU */
		return 0;

#if defined(__alpha__) || defined(__ia64__)
	m |= MODE_64BIT;	/* 64bit platforms only */
#endif
	/* platforms with 64bit flag in /proc/cpuinfo, define
	 * 32bit default here */
#if defined(__i386__) || defined(__x86_64__) || \
    defined(__s390x__) || defined(__s390__) || defined(__sparc_v9__)
	m |= MODE_32BIT;
#endif
	return m;
}

#if defined(HAVE_LIBRTAS)
#define PROCESSOR_MODULE_INFO	43
static int strbe16toh(const char *buf, int offset)
{
	return (buf[offset] << 8) + buf[offset+1];
}

static void read_physical_info_powerpc(struct lscpu_desc *desc)
{
	char buf[BUFSIZ];
	int rc, len, ntypes;

	desc->physsockets = desc->physchips = desc->physcoresperchip = 0;

	rc = rtas_get_sysparm(PROCESSOR_MODULE_INFO, sizeof(buf), buf);
	if (rc < 0)
		return;

	len = strbe16toh(buf, 0);
	if (len < 8)
		return;

	ntypes = strbe16toh(buf, 2);

	assert(ntypes <= 1);
	if (!ntypes)
		return;

	desc->physsockets = strbe16toh(buf, 4);
	desc->physchips = strbe16toh(buf, 6);
	desc->physcoresperchip = strbe16toh(buf, 8);
}
#else
static void read_physical_info_powerpc(
		struct lscpu_desc *desc __attribute__((__unused__)))
{
}
#endif


static void
read_basicinfo(struct lscpu_desc *desc, struct lscpu_modifier *mod)
{
	FILE *fp;
	char buf[BUFSIZ];
	struct utsname utsbuf;
	size_t setsize;
	cpu_set_t *cpuset = NULL;

	/* architecture */
	if (uname(&utsbuf) == -1)
		err(EXIT_FAILURE, _("error: uname failed"));

	fp = ul_path_fopen(desc->procfs, "r", "cpuinfo");
	if (!fp)
		err(EXIT_FAILURE, _("cannot open %s"), "/proc/cpuinfo");
	desc->arch = xstrdup(utsbuf.machine);

	/* details */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (lookup(buf, "vendor", &desc->vendor)) ;
		else if (lookup(buf, "vendor_id", &desc->vendor)) ;
		else if (lookup(buf, "CPU implementer", &desc->vendor)) ; /* ARM and aarch64 */
		else if (lookup(buf, "family", &desc->family)) ;
		else if (lookup(buf, "cpu family", &desc->family)) ;
		else if (lookup(buf, "model", &desc->model)) ;
		else if (lookup(buf, "CPU part", &desc->model)) ; /* ARM and aarch64 */
		else if (lookup(buf, "model name", &desc->modelname)) ;
		else if (lookup(buf, "stepping", &desc->stepping)) ;
		else if (lookup(buf, "CPU variant", &desc->stepping)) ; /* aarch64 */
		else if (lookup(buf, "cpu MHz", &desc->mhz)) ;
		else if (lookup(buf, "cpu MHz dynamic", &desc->dynamic_mhz)) ; /* s390 */
		else if (lookup(buf, "cpu MHz static", &desc->static_mhz)) ;   /* s390 */
		else if (lookup(buf, "flags", &desc->flags)) ;		/* x86 */
		else if (lookup(buf, "features", &desc->flags)) ;	/* s390 */
		else if (lookup(buf, "Features", &desc->flags)) ;	/* aarch64 */
		else if (lookup(buf, "type", &desc->flags)) ;		/* sparc64 */
		else if (lookup(buf, "bogomips", &desc->bogomips)) ;
		else if (lookup(buf, "BogoMIPS", &desc->bogomips)) ;	/* aarch64 */
		else if (lookup(buf, "bogomips per cpu", &desc->bogomips)) ; /* s390 */
		else if (lookup(buf, "cpu", &desc->cpu)) ;
		else if (lookup(buf, "revision", &desc->revision)) ;
		else if (lookup(buf, "CPU revision", &desc->revision)) ; /* aarch64 */
		else if (lookup(buf, "max thread id", &desc->mtid)) ; /* s390 */
		else if (lookup(buf, "address sizes", &desc->addrsz)) ; /* x86 */
		else if (lookup_cache(buf, desc)) ;
		else
			continue;
	}

	desc->mode = init_mode(mod);

	if (desc->flags) {
		snprintf(buf, sizeof(buf), " %s ", desc->flags);
		if (strstr(buf, " svm "))
			desc->virtflag = xstrdup("svm");
		else if (strstr(buf, " vmx "))
			desc->virtflag = xstrdup("vmx");
		if (strstr(buf, " lm "))
			desc->mode |= MODE_32BIT | MODE_64BIT;		/* x86_64 */
		if (strstr(buf, " zarch "))
			desc->mode |= MODE_32BIT | MODE_64BIT;		/* s390x */
		if (strstr(buf, " sun4v ") || strstr(buf, " sun4u "))
			desc->mode |= MODE_32BIT | MODE_64BIT;		/* sparc64 */
	}

	if (desc->arch && mod->system != SYSTEM_SNAPSHOT) {
		if (strcmp(desc->arch, "ppc64") == 0)
			desc->mode |= MODE_32BIT | MODE_64BIT;
		else if (strcmp(desc->arch, "ppc") == 0)
			desc->mode |= MODE_32BIT;
	}

	fclose(fp);

	if (ul_path_read_s32(desc->syscpu, &maxcpus, "kernel_max") == 0)
		/* note that kernel_max is maximum index [NR_CPUS-1] */
		maxcpus += 1;

	else if (mod->system == SYSTEM_LIVE)
		/* the root is '/' so we are working with data from the current kernel */
		maxcpus = get_max_number_of_cpus();

	if (maxcpus <= 0)
		/* error or we are reading some /sys snapshot instead of the
		 * real /sys, let's use any crazy number... */
		maxcpus = 2048;

	setsize = CPU_ALLOC_SIZE(maxcpus);

	if (ul_path_readf_cpulist(desc->syscpu, &cpuset, maxcpus, "possible") == 0) {
		int num, idx;

		desc->ncpuspos = CPU_COUNT_S(setsize, cpuset);
		desc->idx2cpunum = xcalloc(desc->ncpuspos, sizeof(int));

		for (num = 0, idx = 0; num < maxcpus; num++) {
			if (CPU_ISSET_S(num, setsize, cpuset))
				desc->idx2cpunum[idx++] = num;
		}
		cpuset_free(cpuset);
		cpuset = NULL;
	} else
		err(EXIT_FAILURE, _("failed to determine number of CPUs: %s"),
				_PATH_SYS_CPU "/possible");


	/* get mask for present CPUs */
	if (ul_path_readf_cpulist(desc->syscpu, &desc->present, maxcpus, "present") == 0)
		desc->ncpus = CPU_COUNT_S(setsize, desc->present);

	/* get mask for online CPUs */
	if (ul_path_readf_cpulist(desc->syscpu, &desc->online, maxcpus, "online") == 0)
		desc->nthreads = CPU_COUNT_S(setsize, desc->online);

	/* get dispatching mode */
	if (ul_path_read_s32(desc->syscpu, &desc->dispatching, "dispatching") != 0)
		desc->dispatching = -1;

	if (mod->system == SYSTEM_LIVE)
		read_physical_info_powerpc(desc);

	if ((fp = ul_path_fopen(desc->procfs, "r", "sysinfo"))) {
		while (fgets(buf, sizeof(buf), fp) != NULL && !desc->machinetype)
			lookup(buf, "Type", &desc->machinetype);
		fclose(fp);
	}
}

static int
has_pci_device(struct lscpu_desc *desc, unsigned int vendor, unsigned int device)
{
	FILE *f;
	unsigned int num, fn, ven, dev;
	int res = 1;

	f = ul_path_fopen(desc->procfs, "r", "bus/pci/devices");
	if (!f)
		return 0;

	 /* for more details about bus/pci/devices format see
	  * drivers/pci/proc.c in linux kernel
	  */
	while(fscanf(f, "%02x%02x\t%04x%04x\t%*[^\n]",
			&num, &fn, &ven, &dev) == 4) {

		if (ven == vendor && dev == device)
			goto found;
	}

	res = 0;
found:
	fclose(f);
	return res;
}

#if defined(__x86_64__) || defined(__i386__)

/*
 * This CPUID leaf returns the information about the hypervisor.
 * EAX : maximum input value for CPUID supported by the hypervisor.
 * EBX, ECX, EDX : Hypervisor vendor ID signature. E.g. VMwareVMware.
 */
#define HYPERVISOR_INFO_LEAF   0x40000000

static inline void
cpuid(unsigned int op, unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	__asm__(
#if defined(__PIC__) && defined(__i386__)
		/* x86 PIC cannot clobber ebx -- gcc bitches */
		"xchg %%ebx, %%esi;"
		"cpuid;"
		"xchg %%esi, %%ebx;"
		: "=S" (*ebx),
#else
		"cpuid;"
		: "=b" (*ebx),
#endif
		  "=a" (*eax),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "1" (op), "c"(0));
}

static void
read_hypervisor_cpuid(struct lscpu_desc *desc)
{
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	char hyper_vendor_id[13];

	memset(hyper_vendor_id, 0, sizeof(hyper_vendor_id));

	cpuid(HYPERVISOR_INFO_LEAF, &eax, &ebx, &ecx, &edx);
	memcpy(hyper_vendor_id + 0, &ebx, 4);
	memcpy(hyper_vendor_id + 4, &ecx, 4);
	memcpy(hyper_vendor_id + 8, &edx, 4);
	hyper_vendor_id[12] = '\0';

	if (!hyper_vendor_id[0])
		return;

	if (!strncmp("XenVMMXenVMM", hyper_vendor_id, 12))
		desc->hyper = HYPER_XEN;
	else if (!strncmp("KVMKVMKVM", hyper_vendor_id, 9))
		desc->hyper = HYPER_KVM;
	else if (!strncmp("Microsoft Hv", hyper_vendor_id, 12))
		desc->hyper = HYPER_MSHV;
	else if (!strncmp("VMwareVMware", hyper_vendor_id, 12))
		desc->hyper = HYPER_VMWARE;
	else if (!strncmp("UnisysSpar64", hyper_vendor_id, 12))
		desc->hyper = HYPER_SPAR;
}

#else /* ! (__x86_64__ || __i386__) */
static void
read_hypervisor_cpuid(struct lscpu_desc *desc __attribute__((__unused__)))
{
}
#endif

static int is_devtree_compatible(struct lscpu_desc *desc, const char *str)
{
	FILE *fd = ul_path_fopen(desc->procfs, "r", "device-tree/compatible");

	if (fd) {
		char buf[256];
		size_t i, len;

		memset(buf, 0, sizeof(buf));
		len = fread(buf, 1, sizeof(buf) - 1, fd);
		fclose(fd);

		for (i = 0; i < len;) {
			if (!strcmp(&buf[i], str))
				return 1;
			i += strlen(&buf[i]);
			i++;
		}
	}

	return 0;
}

static int
read_hypervisor_powerpc(struct lscpu_desc *desc)
{
	assert(!desc->hyper);

	 /* IBM iSeries: legacy, para-virtualized on top of OS/400 */
	if (ul_path_access(desc->procfs, F_OK, "iSeries") == 0) {
		desc->hyper = HYPER_OS400;
		desc->virtype = VIRT_PARA;

	/* PowerNV (POWER Non-Virtualized, bare-metal) */
	} else if (is_devtree_compatible(desc, "ibm,powernv")) {
		desc->hyper = HYPER_NONE;
		desc->virtype = VIRT_NONE;

	/* PowerVM (IBM's proprietary hypervisor, aka pHyp) */
	} else if (ul_path_access(desc->procfs, F_OK, "device-tree/ibm,partition-name") == 0
		   && ul_path_access(desc->procfs, F_OK, "device-tree/hmc-managed?") == 0
		   && ul_path_access(desc->procfs, F_OK, "device-tree/chosen/qemu,graphic-width") != 0) {

		FILE *fd;
		desc->hyper = HYPER_PHYP;
		desc->virtype = VIRT_PARA;

		fd = ul_path_fopen(desc->procfs, "r", "device-tree/ibm,partition-name");
		if (fd) {
			char buf[256];
			if (fscanf(fd, "%255s", buf) == 1 && !strcmp(buf, "full"))
				desc->virtype = VIRT_NONE;
			fclose(fd);
		}

	/* Qemu */
	} else if (is_devtree_compatible(desc, "qemu,pseries")) {
		desc->hyper = HYPER_KVM;
		desc->virtype = VIRT_PARA;
	}
	return desc->hyper;
}

#ifdef INCLUDE_VMWARE_BDOOR

#define VMWARE_BDOOR_MAGIC          0x564D5868
#define VMWARE_BDOOR_PORT           0x5658
#define VMWARE_BDOOR_CMD_GETVERSION 10

static UL_ASAN_BLACKLIST
void vmware_bdoor(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	__asm__(
#if defined(__PIC__) && defined(__i386__)
		/* x86 PIC cannot clobber ebx -- gcc bitches */
		"xchg %%ebx, %%esi;"
		"inl (%%dx), %%eax;"
		"xchg %%esi, %%ebx;"
		: "=S" (*ebx),
#else
		"inl (%%dx), %%eax;"
		: "=b" (*ebx),
#endif
		  "=a" (*eax),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (VMWARE_BDOOR_MAGIC),
		  "1" (VMWARE_BDOOR_CMD_GETVERSION),
		  "2" (VMWARE_BDOOR_PORT),
		  "3" (0)
		: "memory");
}

static jmp_buf segv_handler_env;

static void
segv_handler(__attribute__((__unused__)) int sig,
             __attribute__((__unused__)) siginfo_t *info,
             __attribute__((__unused__)) void *ignored)
{
	siglongjmp(segv_handler_env, 1);
}

static int
is_vmware_platform(void)
{
	uint32_t eax, ebx, ecx, edx;
	struct sigaction act, oact;

	/*
	 * FIXME: Not reliable for non-root users. Note it works as expected if
	 * vmware_bdoor() is not optimized for PIE, but then it fails to build
	 * on 32bit x86 systems. See lscpu git log for more details (commit
	 * 7845b91dbc7690064a2be6df690e4aaba728fb04).     kzak [3-Nov-2016]
	 */
	if (getuid() != 0)
		return 0;

	/*
	 * The assembly routine for vmware detection works
	 * fine under vmware, even if ran as regular user. But
	 * on real HW or under other hypervisors, it segfaults (which is
	 * expected). So we temporarily install SIGSEGV handler to catch
	 * the signal. All this magic is needed because lscpu
	 * isn't supposed to require root privileges.
	 */
	if (sigsetjmp(segv_handler_env, 1))
		return 0;

	memset(&act, 0, sizeof(act));
	act.sa_sigaction = segv_handler;
	act.sa_flags = SA_SIGINFO;

	if (sigaction(SIGSEGV, &act, &oact))
		err(EXIT_FAILURE, _("cannot set signal handler"));

	vmware_bdoor(&eax, &ebx, &ecx, &edx);

	if (sigaction(SIGSEGV, &oact, NULL))
		err(EXIT_FAILURE, _("cannot restore signal handler"));

	return eax != (uint32_t)-1 && ebx == VMWARE_BDOOR_MAGIC;
}

#else /* ! INCLUDE_VMWARE_BDOOR */

static int
is_vmware_platform(void)
{
	return 0;
}

#endif /* INCLUDE_VMWARE_BDOOR */

static void
read_hypervisor(struct lscpu_desc *desc, struct lscpu_modifier *mod)
{
	FILE *fd;

	/* We have to detect WSL first. is_vmware_platform() crashes on Windows 10. */

	if ((fd = ul_path_fopen(desc->procfs, "r", "sys/kernel/osrelease"))) {
		char buf[256];

		if (fgets(buf, sizeof(buf), fd) != NULL) {
			if (strstr(buf, "Microsoft")) {
				desc->hyper = HYPER_WSL;
				desc->virtype = VIRT_CONT;
			}
		}
		fclose(fd);
		if (desc->virtype)
			return;
	}

	if (mod->system != SYSTEM_SNAPSHOT) {
		read_hypervisor_cpuid(desc);
		if (!desc->hyper)
			desc->hyper = read_hypervisor_dmi();
		if (!desc->hyper && is_vmware_platform())
			desc->hyper = HYPER_VMWARE;
	}

	if (desc->hyper) {
		desc->virtype = VIRT_FULL;

		if (desc->hyper == HYPER_XEN) {
			uint32_t features;

			fd = ul_prefix_fopen(desc->prefix, "r", _PATH_SYS_HYP_FEATURES);

			if (fd && fscanf(fd, "%x", &features) == 1) {
				/* Xen PV domain */
				if (features & XEN_FEATURES_PV_MASK)
					desc->virtype = VIRT_PARA;
				/* Xen PVH domain */
				else if ((features & XEN_FEATURES_PVH_MASK)
								== XEN_FEATURES_PVH_MASK)
					desc->virtype = VIRT_PARA;
			}
			if (fd)
				fclose(fd);
		}
	} else if (read_hypervisor_powerpc(desc) > 0) {}

	/* Xen para-virt or dom0 */
	else if (ul_path_access(desc->procfs, F_OK, "xen") == 0) {
		int dom0 = 0;

		fd = ul_path_fopen(desc->procfs, "r", "xen/capabilities");
		if (fd) {
			char buf[256];

			if (fscanf(fd, "%255s", buf) == 1 &&
			    !strcmp(buf, "control_d"))
				dom0 = 1;
			fclose(fd);
		}
		desc->virtype = dom0 ? VIRT_NONE : VIRT_PARA;
		desc->hyper = HYPER_XEN;

	/* Xen full-virt on non-x86_64 */
	} else if (has_pci_device(desc, hv_vendor_pci[HYPER_XEN], hv_graphics_pci[HYPER_XEN])) {
		desc->hyper = HYPER_XEN;
		desc->virtype = VIRT_FULL;
	} else if (has_pci_device(desc, hv_vendor_pci[HYPER_VMWARE], hv_graphics_pci[HYPER_VMWARE])) {
		desc->hyper = HYPER_VMWARE;
		desc->virtype = VIRT_FULL;
	} else if (has_pci_device(desc, hv_vendor_pci[HYPER_VBOX], hv_graphics_pci[HYPER_VBOX])) {
		desc->hyper = HYPER_VBOX;
		desc->virtype = VIRT_FULL;

	/* IBM PR/SM */
	} else if ((fd = ul_path_fopen(desc->procfs, "r", "sysinfo"))) {
		char buf[BUFSIZ];

		desc->hyper = HYPER_IBM;
		desc->hypervisor = "PR/SM";
		desc->virtype = VIRT_FULL;
		while (fgets(buf, sizeof(buf), fd) != NULL) {
			char *str, *p;

			if (!strstr(buf, "Control Program:"))
				continue;
			if (!strstr(buf, "KVM"))
				desc->hyper = HYPER_IBM;
			else
				desc->hyper = HYPER_KVM;
			p = strchr(buf, ':');
			if (!p)
				continue;
			xasprintf(&str, "%s", p + 1);

			/* remove leading, trailing and repeating whitespace */
			while (*str == ' ')
				str++;
			desc->hypervisor = str;
			str += strlen(str) - 1;
			while ((*str == '\n') || (*str == ' '))
				*(str--) = '\0';
			while ((str = strstr(desc->hypervisor, "  ")))
				memmove(str, str + 1, strlen(str));
			break;
		}
		fclose(fd);
	}

	/* OpenVZ/Virtuozzo - /proc/vz dir should exist
	 *		      /proc/bc should not */
	else if (ul_path_access(desc->procfs, F_OK, "vz") == 0 &&
		 ul_path_access(desc->procfs, F_OK, "bc") != 0) {
		desc->hyper = HYPER_PARALLELS;
		desc->virtype = VIRT_CONT;

	/* IBM */
	} else if (desc->vendor &&
		 (strcmp(desc->vendor, "PowerVM Lx86") == 0 ||
		  strcmp(desc->vendor, "IBM/S390") == 0)) {
		desc->hyper = HYPER_IBM;
		desc->virtype = VIRT_FULL;

	/* User-mode-linux */
	} else if (desc->modelname && strstr(desc->modelname, "UML")) {
		desc->hyper = HYPER_UML;
		desc->virtype = VIRT_PARA;

	/* Linux-VServer */
	} else if ((fd = ul_path_fopen(desc->procfs, "r", "self/status"))) {
		char buf[BUFSIZ];
		char *val = NULL;

		while (fgets(buf, sizeof(buf), fd) != NULL) {
			if (lookup(buf, "VxID", &val))
				break;
		}
		fclose(fd);

		if (val) {
			char *org = val;

			while (isdigit(*val))
				++val;
			if (!*val) {
				desc->hyper = HYPER_VSERVER;
				desc->virtype = VIRT_CONT;
			}
			free(org);
		}
	}
}

/* add @set to the @ary, unnecessary set is deallocated. */
static int add_cpuset_to_array(cpu_set_t **ary, int *items, cpu_set_t *set)
{
	int i;
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);

	if (!ary)
		return -1;

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

static void
read_topology(struct lscpu_desc *desc, int idx)
{
	cpu_set_t *thread_siblings, *core_siblings;
	cpu_set_t *book_siblings, *drawer_siblings;
	int coreid, socketid, bookid, drawerid;
	int i, num = real_cpu_num(desc, idx);

	if (ul_path_accessf(desc->syscpu, F_OK, "cpu%d/topology/thread_siblings", num) != 0)
		return;

	ul_path_readf_cpuset(desc->syscpu, &thread_siblings, maxcpus,
					"cpu%d/topology/thread_siblings", num);
	ul_path_readf_cpuset(desc->syscpu, &core_siblings, maxcpus,
					"cpu%d/topology/core_siblings", num);
	ul_path_readf_cpuset(desc->syscpu, &book_siblings, maxcpus,
					"cpu%d/topology/book_siblings", num);
	ul_path_readf_cpuset(desc->syscpu, &drawer_siblings, maxcpus,
					"cpu%d/topology/drawer_siblings", num);

	if (ul_path_readf_s32(desc->syscpu, &coreid, "cpu%d/topology/core_id", num) != 0)
		coreid = -1;

	if (ul_path_readf_s32(desc->syscpu, &socketid, "cpu%d/topology/physical_package_id", num) != 0)
		socketid = -1;

	if (ul_path_readf_s32(desc->syscpu, &bookid, "cpu%d/topology/book_id", num) != 0)
		bookid = -1;

	if (ul_path_readf_s32(desc->syscpu, &drawerid, "cpu%d/topology/drawer_id", num) != 0)
		drawerid = -1;

	if (!desc->coremaps) {
		int ndrawers, nbooks, nsockets, ncores, nthreads;
		size_t setsize = CPU_ALLOC_SIZE(maxcpus);

		/* threads within one core */
		nthreads = CPU_COUNT_S(setsize, thread_siblings);
		if (!nthreads)
			nthreads = 1;

		/* cores within one socket */
		ncores = CPU_COUNT_S(setsize, core_siblings) / nthreads;
		if (!ncores)
			ncores = 1;

		/* number of sockets within one book.  Because of odd /
		 * non-present cpu maps and to keep calculation easy we make
		 * sure that nsockets and nbooks is at least 1.
		 */
		nsockets = desc->ncpus / nthreads / ncores;
		if (!nsockets)
			nsockets = 1;

		/* number of books */
		nbooks = desc->ncpus / nthreads / ncores / nsockets;
		if (!nbooks)
			nbooks = 1;

		/* number of drawers */
		ndrawers = desc->ncpus / nbooks / nthreads / ncores / nsockets;
		if (!ndrawers)
			ndrawers = 1;

		/* all threads, see also read_basicinfo()
		 * -- fallback for kernels without
		 *    /sys/devices/system/cpu/online.
		 */
		if (!desc->nthreads)
			desc->nthreads = ndrawers * nbooks * nsockets * ncores * nthreads;

		/* For each map we make sure that it can have up to ncpuspos
		 * entries. This is because we cannot reliably calculate the
		 * number of cores, sockets and books on all architectures.
		 * E.g. completely virtualized architectures like s390 may
		 * have multiple sockets of different sizes.
		 */
		desc->coremaps = xcalloc(desc->ncpuspos, sizeof(cpu_set_t *));
		desc->socketmaps = xcalloc(desc->ncpuspos, sizeof(cpu_set_t *));
		desc->coreids = xcalloc(desc->ncpuspos, sizeof(*desc->drawerids));
		desc->socketids = xcalloc(desc->ncpuspos, sizeof(*desc->drawerids));
		for (i = 0; i < desc->ncpuspos; i++)
			desc->coreids[i] = desc->socketids[i] = -1;
		if (book_siblings) {
			desc->bookmaps = xcalloc(desc->ncpuspos, sizeof(cpu_set_t *));
			desc->bookids = xcalloc(desc->ncpuspos, sizeof(*desc->drawerids));
			for (i = 0; i < desc->ncpuspos; i++)
				desc->bookids[i] = -1;
		}
		if (drawer_siblings) {
			desc->drawermaps = xcalloc(desc->ncpuspos, sizeof(cpu_set_t *));
			desc->drawerids = xcalloc(desc->ncpuspos, sizeof(*desc->drawerids));
			for (i = 0; i < desc->ncpuspos; i++)
				desc->drawerids[i] = -1;
		}
	}

	add_cpuset_to_array(desc->socketmaps, &desc->nsockets, core_siblings);
	desc->coreids[idx] = coreid;
	add_cpuset_to_array(desc->coremaps, &desc->ncores, thread_siblings);
	desc->socketids[idx] = socketid;
	if (book_siblings) {
		add_cpuset_to_array(desc->bookmaps, &desc->nbooks, book_siblings);
		desc->bookids[idx] = bookid;
	}
	if (drawer_siblings) {
		add_cpuset_to_array(desc->drawermaps, &desc->ndrawers, drawer_siblings);
		desc->drawerids[idx] = drawerid;
	}
}

static void
read_polarization(struct lscpu_desc *desc, int idx)
{
	char mode[64];
	int num = real_cpu_num(desc, idx);

	if (desc->dispatching < 0)
		return;
	if (ul_path_accessf(desc->syscpu, F_OK, "cpu%d/polarization", num) != 0)
		return;
	if (!desc->polarization)
		desc->polarization = xcalloc(desc->ncpuspos, sizeof(int));

	ul_path_readf_buffer(desc->syscpu, mode, sizeof(mode), "cpu%d/polarization", num);

	if (strncmp(mode, "vertical:low", sizeof(mode)) == 0)
		desc->polarization[idx] = POLAR_VLOW;
	else if (strncmp(mode, "vertical:medium", sizeof(mode)) == 0)
		desc->polarization[idx] = POLAR_VMEDIUM;
	else if (strncmp(mode, "vertical:high", sizeof(mode)) == 0)
		desc->polarization[idx] = POLAR_VHIGH;
	else if (strncmp(mode, "horizontal", sizeof(mode)) == 0)
		desc->polarization[idx] = POLAR_HORIZONTAL;
	else
		desc->polarization[idx] = POLAR_UNKNOWN;
}

static void
read_address(struct lscpu_desc *desc, int idx)
{
	int num = real_cpu_num(desc, idx);

	if (ul_path_accessf(desc->syscpu, F_OK, "cpu%d/address", num) != 0)
		return;
	if (!desc->addresses)
		desc->addresses = xcalloc(desc->ncpuspos, sizeof(int));
	ul_path_readf_s32(desc->syscpu, &desc->addresses[idx], "cpu%d/address", num);
}

static void
read_configured(struct lscpu_desc *desc, int idx)
{
	int num = real_cpu_num(desc, idx);

	if (ul_path_accessf(desc->syscpu, F_OK, "cpu%d/configure", num) != 0)
		return;
	if (!desc->configured)
		desc->configured = xcalloc(desc->ncpuspos, sizeof(int));
	ul_path_readf_s32(desc->syscpu, &desc->configured[idx], "cpu%d/configure", num);
}

/* Read overall maximum frequency of cpu */
static char *
cpu_max_mhz(struct lscpu_desc *desc, char *buf, size_t bufsz)
{
	int i;
	float cpu_freq = 0.0;
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);

	if (desc->present) {
		for (i = 0; i < desc->ncpuspos; i++) {
			if (CPU_ISSET_S(real_cpu_num(desc, i), setsize, desc->present)
			    && desc->maxmhz[i]) {
				float freq = atof(desc->maxmhz[i]);

				if (freq > cpu_freq)
					cpu_freq = freq;
			}
		}
	}
	snprintf(buf, bufsz, "%.4f", cpu_freq);
	return buf;
}

/* Read overall minimum frequency of cpu */
static char *
cpu_min_mhz(struct lscpu_desc *desc, char *buf, size_t bufsz)
{
	int i;
	float cpu_freq = -1.0;
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);

	if (desc->present) {
		for (i = 0; i < desc->ncpuspos; i++) {
			if (CPU_ISSET_S(real_cpu_num(desc, i), setsize, desc->present)
			    && desc->minmhz[i]) {
				float freq = atof(desc->minmhz[i]);

				if (cpu_freq < 0.0 || freq < cpu_freq)
					cpu_freq = freq;
			}
		}
	}
        snprintf(buf, bufsz, "%.4f", cpu_freq);
	return buf;
}


static void
read_max_mhz(struct lscpu_desc *desc, int idx)
{
	int num = real_cpu_num(desc, idx);
	int mhz;

	if (ul_path_readf_s32(desc->syscpu, &mhz, "cpu%d/cpufreq/cpuinfo_max_freq", num) != 0)
		return;
	if (!desc->maxmhz)
		desc->maxmhz = xcalloc(desc->ncpuspos, sizeof(char *));
	xasprintf(&desc->maxmhz[idx], "%.4f", (float) mhz / 1000);
}

static void
read_min_mhz(struct lscpu_desc *desc, int idx)
{
	int num = real_cpu_num(desc, idx);
	int mhz;

	if (ul_path_readf_s32(desc->syscpu, &mhz, "cpu%d/cpufreq/cpuinfo_min_freq", num) != 0)
		return;
	if (!desc->minmhz)
		desc->minmhz = xcalloc(desc->ncpuspos, sizeof(char *));
	xasprintf(&desc->minmhz[idx], "%.4f", (float) mhz / 1000);
}

static int
cachecmp(const void *a, const void *b)
{
	struct cpu_cache *c1 = (struct cpu_cache *) a;
	struct cpu_cache *c2 = (struct cpu_cache *) b;

	return strcmp(c2->name, c1->name);
}

static void
read_cache(struct lscpu_desc *desc, int idx)
{
	char buf[256];
	int i;
	int num = real_cpu_num(desc, idx);

	if (!desc->ncaches) {
		while (ul_path_accessf(desc->syscpu, F_OK,
					"cpu%d/cache/index%d",
					num, desc->ncaches) == 0)
			desc->ncaches++;

		if (!desc->ncaches)
			return;
		desc->caches = xcalloc(desc->ncaches, sizeof(*desc->caches));
	}
	for (i = 0; i < desc->ncaches; i++) {
		struct cpu_cache *ca = &desc->caches[i];
		cpu_set_t *map;

		if (ul_path_accessf(desc->syscpu, F_OK,
					"cpu%d/cache/index%d", num, i) != 0)
			continue;
		if (!ca->name) {
			int type = 0, level;

			/* cache type */
			if (ul_path_readf_buffer(desc->syscpu, buf, sizeof(buf),
					"cpu%d/cache/index%d/type", num, i) > 0) {
				if (!strcmp(buf, "Data"))
					type = 'd';
				else if (!strcmp(buf, "Instruction"))
					type = 'i';
			}

			/* cache level */
			ul_path_readf_s32(desc->syscpu, &level,
					"cpu%d/cache/index%d/level", num, i);
			if (type)
				snprintf(buf, sizeof(buf), "L%d%c", level, type);
			else
				snprintf(buf, sizeof(buf), "L%d", level);

			ca->name = xstrdup(buf);

			/* cache size */
			if (ul_path_readf_string(desc->syscpu, &ca->size,
					"cpu%d/cache/index%d/size", num, i) < 0)
				ca->size = xstrdup("unknown size");
		}

		/* information about how CPUs share different caches */
		ul_path_readf_cpuset(desc->syscpu, &map, maxcpus,
				  "cpu%d/cache/index%d/shared_cpu_map", num, i);

		if (!ca->sharedmaps)
			ca->sharedmaps = xcalloc(desc->ncpuspos, sizeof(cpu_set_t *));
		add_cpuset_to_array(ca->sharedmaps, &ca->nsharedmaps, map);
	}
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

static int
nodecmp(const void *ap, const void *bp)
{
	int *a = (int *) ap, *b = (int *) bp;
	return *a - *b;
}

static void
read_nodes(struct lscpu_desc *desc)
{
	int i = 0;
	DIR *dir;
	struct dirent *d;
	struct path_cxt *sysnode;

	desc->nnodes = 0;

	sysnode = ul_new_path(_PATH_SYS_NODE);
	if (!sysnode)
		err(EXIT_FAILURE, _("failed to initialize %s handler"), _PATH_SYS_NODE);
	ul_path_set_prefix(sysnode, desc->prefix);

	dir = ul_path_opendir(sysnode, NULL);
	if (!dir)
		goto done;

	while ((d = readdir(dir))) {
		if (is_node_dirent(d))
			desc->nnodes++;
	}

	if (!desc->nnodes) {
		closedir(dir);
		goto done;
	}

	desc->nodemaps = xcalloc(desc->nnodes, sizeof(cpu_set_t *));
	desc->idx2nodenum = xmalloc(desc->nnodes * sizeof(int));

	rewinddir(dir);
	while ((d = readdir(dir)) && i < desc->nnodes) {
		if (is_node_dirent(d))
			desc->idx2nodenum[i++] = strtol_or_err(((d->d_name) + 4),
						_("Failed to extract the node number"));
	}
	closedir(dir);
	qsort(desc->idx2nodenum, desc->nnodes, sizeof(int), nodecmp);

	/* information about how nodes share different CPUs */
	for (i = 0; i < desc->nnodes; i++)
		ul_path_readf_cpuset(sysnode, &desc->nodemaps[i], maxcpus,
				"node%d/cpumap", desc->idx2nodenum[i]);
done:
	ul_unref_path(sysnode);
}

static char *
get_cell_data(struct lscpu_desc *desc, int idx, int col,
	      struct lscpu_modifier *mod,
	      char *buf, size_t bufsz)
{
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);
	size_t i;
	int cpu = real_cpu_num(desc, idx);

	*buf = '\0';

	switch (col) {
	case COL_CPU:
		snprintf(buf, bufsz, "%d", cpu);
		break;
	case COL_CORE:
		if (mod->physical) {
			if (desc->coreids[idx] == -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->coreids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->coremaps,
					     desc->ncores, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_SOCKET:
		if (mod->physical) {
			if (desc->socketids[idx] ==  -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->socketids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->socketmaps,
					     desc->nsockets, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_NODE:
		if (cpuset_ary_isset(cpu, desc->nodemaps,
				     desc->nnodes, setsize, &i) == 0)
			snprintf(buf, bufsz, "%d", desc->idx2nodenum[i]);
		break;
	case COL_DRAWER:
		if (mod->physical) {
			if (desc->drawerids[idx] == -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->drawerids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->drawermaps,
					     desc->ndrawers, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_BOOK:
		if (mod->physical) {
			if (desc->bookids[idx] == -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->bookids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->bookmaps,
					     desc->nbooks, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_CACHE:
	{
		char *p = buf;
		size_t sz = bufsz;
		int j;

		for (j = desc->ncaches - 1; j >= 0; j--) {
			struct cpu_cache *ca = &desc->caches[j];

			if (cpuset_ary_isset(cpu, ca->sharedmaps,
					     ca->nsharedmaps, setsize, &i) == 0) {
				int x = snprintf(p, sz, "%zu", i);
				if (x < 0 || (size_t) x >= sz)
					return NULL;
				p += x;
				sz -= x;
			}
			if (j != 0) {
				if (sz < 2)
					return NULL;
				*p++ = mod->compat ? ',' : ':';
				*p = '\0';
				sz--;
			}
		}
		break;
	}
	case COL_POLARIZATION:
		if (desc->polarization) {
			int x = desc->polarization[idx];

			snprintf(buf, bufsz, "%s",
				 mod->mode == OUTPUT_PARSABLE ?
						polar_modes[x].parsable :
						polar_modes[x].readable);
		}
		break;
	case COL_ADDRESS:
		if (desc->addresses)
			snprintf(buf, bufsz, "%d", desc->addresses[idx]);
		break;
	case COL_CONFIGURED:
		if (!desc->configured)
			break;
		if (mod->mode == OUTPUT_PARSABLE)
			snprintf(buf, bufsz, "%s",
				 desc->configured[idx] ? _("Y") : _("N"));
		else
			snprintf(buf, bufsz, "%s",
				 desc->configured[idx] ? _("yes") : _("no"));
		break;
	case COL_ONLINE:
		if (!desc->online)
			break;
		if (mod->mode == OUTPUT_PARSABLE)
			snprintf(buf, bufsz, "%s",
				 is_cpu_online(desc, cpu) ? _("Y") : _("N"));
		else
			snprintf(buf, bufsz, "%s",
				 is_cpu_online(desc, cpu) ? _("yes") : _("no"));
		break;
	case COL_MAXMHZ:
		if (desc->maxmhz && desc->maxmhz[idx])
			xstrncpy(buf, desc->maxmhz[idx], bufsz);
		break;
	case COL_MINMHZ:
		if (desc->minmhz && desc->minmhz[idx])
			xstrncpy(buf, desc->minmhz[idx], bufsz);
		break;
	}
	return buf;
}

static char *
get_cell_header(struct lscpu_desc *desc, int col,
		struct lscpu_modifier *mod,
	        char *buf, size_t bufsz)
{
	*buf = '\0';

	if (col == COL_CACHE) {
		char *p = buf;
		size_t sz = bufsz;
		int i;

		for (i = desc->ncaches - 1; i >= 0; i--) {
			int x = snprintf(p, sz, "%s", desc->caches[i].name);
			if (x < 0 || (size_t) x >= sz)
				return NULL;
			sz -= x;
			p += x;
			if (i > 0) {
				if (sz < 2)
					return NULL;
				*p++ = mod->compat ? ',' : ':';
				*p = '\0';
				sz--;
			}
		}
		if (desc->ncaches)
			return buf;
	}
	snprintf(buf, bufsz, "%s", coldescs[col].name);
	return buf;
}

/*
 * [-p] backend, we support two parsable formats:
 *
 * 1) "compatible" -- this format is compatible with the original lscpu(1)
 * output and it contains fixed set of the columns. The CACHE columns are at
 * the end of the line and the CACHE is not printed if the number of the caches
 * is zero. The CACHE columns are separated by two commas, for example:
 *
 *    $ lscpu --parse
 *    # CPU,Core,Socket,Node,,L1d,L1i,L2
 *    0,0,0,0,,0,0,0
 *    1,1,0,0,,1,1,0
 *
 * 2) "user defined output" -- this format prints always all columns without
 * special prefix for CACHE column. If there are not CACHEs then the column is
 * empty and the header "Cache" is printed rather than a real name of the cache.
 * The CACHE columns are separated by ':'.
 *
 *	$ lscpu --parse=CPU,CORE,SOCKET,NODE,CACHE
 *	# CPU,Core,Socket,Node,L1d:L1i:L2
 *	0,0,0,0,0:0:0
 *	1,1,0,0,1:1:0
 */
static void
print_parsable(struct lscpu_desc *desc, int cols[], int ncols,
	       struct lscpu_modifier *mod)
{
	char buf[BUFSIZ], *data;
	int i;

	/*
	 * Header
	 */
	printf(_(
	"# The following is the parsable format, which can be fed to other\n"
	"# programs. Each different item in every column has an unique ID\n"
	"# starting from zero.\n"));

	fputs("# ", stdout);
	for (i = 0; i < ncols; i++) {
		int col = cols[i];

		if (col == COL_CACHE) {
			if (mod->compat && !desc->ncaches)
				continue;
			if (mod->compat && i != 0)
				putchar(',');
		}
		if (i > 0)
			putchar(',');

		data = get_cell_header(desc, col, mod, buf, sizeof(buf));

		if (data && * data && col != COL_CACHE &&
		    !coldescs[col].is_abbr) {
			/*
			 * For normal column names use mixed case (e.g. "Socket")
			 */
			char *p = data + 1;

			while (p && *p != '\0') {
				*p = tolower((unsigned int) *p);
				p++;
			}
		}
		fputs(data && *data ? data : "", stdout);
	}
	putchar('\n');

	/*
	 * Data
	 */
	for (i = 0; i < desc->ncpuspos; i++) {
		int c;
		int cpu = real_cpu_num(desc, i);

		if (!mod->offline && desc->online && !is_cpu_online(desc, cpu))
			continue;
		if (!mod->online && desc->online && is_cpu_online(desc, cpu))
			continue;
		if (desc->present && !is_cpu_present(desc, cpu))
			continue;
		for (c = 0; c < ncols; c++) {
			if (mod->compat && cols[c] == COL_CACHE) {
				if (!desc->ncaches)
					continue;
				if (c > 0)
					putchar(',');
			}
			if (c > 0)
				putchar(',');

			data = get_cell_data(desc, i, cols[c], mod,
					     buf, sizeof(buf));
			fputs(data && *data ? data : "", stdout);
		}
		putchar('\n');
	}
}

/*
 * [-e] backend
 */
static void
print_readable(struct lscpu_desc *desc, int cols[], int ncols,
	       struct lscpu_modifier *mod)
{
	int i;
	char buf[BUFSIZ];
	const char *data;
	struct libscols_table *table;

	scols_init_debug(0);

	table = scols_new_table();
	if (!table)
		 err(EXIT_FAILURE, _("failed to allocate output table"));
	if (mod->json) {
		scols_table_enable_json(table, 1);
		scols_table_set_name(table, "cpus");
	}

	for (i = 0; i < ncols; i++) {
		data = get_cell_header(desc, cols[i], mod, buf, sizeof(buf));
		if (!scols_table_new_column(table, data, 0, 0))
			err(EXIT_FAILURE, _("failed to allocate output column"));
	}

	for (i = 0; i < desc->ncpuspos; i++) {
		int c;
		struct libscols_line *line;
		int cpu = real_cpu_num(desc, i);

		if (!mod->offline && desc->online && !is_cpu_online(desc, cpu))
			continue;
		if (!mod->online && desc->online && is_cpu_online(desc, cpu))
			continue;
		if (desc->present && !is_cpu_present(desc, cpu))
			continue;

		line = scols_table_new_line(table, NULL);
		if (!line)
			err(EXIT_FAILURE, _("failed to allocate output line"));

		for (c = 0; c < ncols; c++) {
			data = get_cell_data(desc, i, cols[c], mod,
					     buf, sizeof(buf));
			if (!data || !*data)
				data = "-";
			if (scols_line_set_data(line, c, data))
				err(EXIT_FAILURE, _("failed to add output data"));
		}
	}

	scols_print_table(table);
	scols_unref_table(table);
}


static void __attribute__ ((__format__(printf, 3, 4)))
	add_summary_sprint(struct libscols_table *tb,
			const char *txt,
			const char *fmt,
			...)
{
	struct libscols_line *ln = scols_table_new_line(tb, NULL);
	char *data;
	va_list args;

	if (!ln)
		err(EXIT_FAILURE, _("failed to allocate output line"));

	/* description column */
	scols_line_set_data(ln, 0, txt);

	/* data column */
	va_start(args, fmt);
	xvasprintf(&data, fmt, args);
	va_end(args);

	if (data && scols_line_refer_data(ln, 1, data))
		 err(EXIT_FAILURE, _("failed to add output data"));
}

#define add_summary_n(tb, txt, num)	add_summary_sprint(tb, txt, "%d", num)
#define add_summary_s(tb, txt, str)	add_summary_sprint(tb, txt, "%s", str)

static void
print_cpuset(struct libscols_table *tb,
	     const char *key, cpu_set_t *set, int hex)
{
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);
	size_t setbuflen = 7 * maxcpus;
	char setbuf[setbuflen], *p;

	if (hex) {
		p = cpumask_create(setbuf, setbuflen, set, setsize);
		add_summary_s(tb, key, p);
	} else {
		p = cpulist_create(setbuf, setbuflen, set, setsize);
		add_summary_s(tb, key, p);
	}
}

/*
 * default output
 */
static void
print_summary(struct lscpu_desc *desc, struct lscpu_modifier *mod)
{
	char buf[BUFSIZ];
	int i = 0;
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);
	struct libscols_table *tb;

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, _("failed to allocate output table"));

	scols_table_enable_noheadings(tb, 1);
	if (mod->json) {
		scols_table_enable_json(tb, 1);
		scols_table_set_name(tb, "lscpu");
	}

	if (scols_table_new_column(tb, "field", 0, 0) == NULL ||
	    scols_table_new_column(tb, "data", 0, SCOLS_FL_NOEXTREMES) == NULL)
		err(EXIT_FAILURE, _("failed to initialize output column"));

	add_summary_s(tb, _("Architecture:"), desc->arch);
	if (desc->mode) {
		char *p = buf;

		if (desc->mode & MODE_32BIT) {
			strcpy(p, "32-bit, ");
			p += 8;
		}
		if (desc->mode & MODE_64BIT) {
			strcpy(p, "64-bit, ");
			p += 8;
		}
		*(p - 2) = '\0';
		add_summary_s(tb, _("CPU op-mode(s):"), buf);
	}
#if !defined(WORDS_BIGENDIAN)
	add_summary_s(tb, _("Byte Order:"), "Little Endian");
#else
	add_summary_s(tb, _("Byte Order:"), "Big Endian");
#endif

	if (desc->addrsz)
		add_summary_s(tb, _("Address sizes:"), desc->addrsz);

	add_summary_n(tb, _("CPU(s):"), desc->ncpus);

	if (desc->online)
		print_cpuset(tb, mod->hex ? _("On-line CPU(s) mask:") :
					    _("On-line CPU(s) list:"),
				desc->online, mod->hex);

	if (desc->online && CPU_COUNT_S(setsize, desc->online) != desc->ncpus) {
		cpu_set_t *set;

		/* Linux kernel provides cpuset of off-line CPUs that contains
		 * all configured CPUs (see /sys/devices/system/cpu/offline),
		 * but want to print real (present in system) off-line CPUs only.
		 */
		set = cpuset_alloc(maxcpus, NULL, NULL);
		if (!set)
			err(EXIT_FAILURE, _("failed to callocate cpu set"));
		CPU_ZERO_S(setsize, set);
		for (i = 0; i < desc->ncpuspos; i++) {
			int cpu = real_cpu_num(desc, i);
			if (!is_cpu_online(desc, cpu) && is_cpu_present(desc, cpu))
				CPU_SET_S(cpu, setsize, set);
		}
		print_cpuset(tb, mod->hex ? _("Off-line CPU(s) mask:") :
					    _("Off-line CPU(s) list:"),
			     set, mod->hex);
		cpuset_free(set);
	}

	if (desc->nsockets) {
		int threads_per_core, cores_per_socket, sockets_per_book;
		int books_per_drawer, drawers;
		FILE *fd;

		threads_per_core = cores_per_socket = sockets_per_book = 0;
		books_per_drawer = drawers = 0;
		/* s390 detects its cpu topology via /proc/sysinfo, if present.
		 * Using simply the cpu topology masks in sysfs will not give
		 * usable results since everything is virtualized. E.g.
		 * virtual core 0 may have only 1 cpu, but virtual core 2 may
		 * five cpus.
		 * If the cpu topology is not exported (e.g. 2nd level guest)
		 * fall back to old calculation scheme.
		 */
		if ((fd = ul_path_fopen(desc->procfs, "r", "sysinfo"))) {
			int t0, t1;

			while (fd && fgets(buf, sizeof(buf), fd) != NULL) {
				if (sscanf(buf, "CPU Topology SW:%d%d%d%d%d%d",
					   &t0, &t1, &drawers, &books_per_drawer,
					   &sockets_per_book,
					   &cores_per_socket) == 6)
					break;
			}
			if (fd)
				fclose(fd);
		}
		if (desc->mtid)
			threads_per_core = atoi(desc->mtid) + 1;
		add_summary_n(tb, _("Thread(s) per core:"),
			threads_per_core ?: desc->nthreads / desc->ncores);
		add_summary_n(tb, _("Core(s) per socket:"),
			cores_per_socket ?: desc->ncores / desc->nsockets);
		if (desc->nbooks) {
			add_summary_n(tb, _("Socket(s) per book:"),
				sockets_per_book ?: desc->nsockets / desc->nbooks);
			if (desc->ndrawers) {
				add_summary_n(tb, _("Book(s) per drawer:"),
					books_per_drawer ?: desc->nbooks / desc->ndrawers);
				add_summary_n(tb, _("Drawer(s):"), drawers ?: desc->ndrawers);
			} else {
				add_summary_n(tb, _("Book(s):"), books_per_drawer ?: desc->nbooks);
			}
		} else {
			add_summary_n(tb, _("Socket(s):"), sockets_per_book ?: desc->nsockets);
		}
	}
	if (desc->nnodes)
		add_summary_n(tb, _("NUMA node(s):"), desc->nnodes);
	if (desc->vendor)
		add_summary_s(tb, _("Vendor ID:"), desc->vendor);
	if (desc->machinetype)
		add_summary_s(tb, _("Machine type:"), desc->machinetype);
	if (desc->family)
		add_summary_s(tb, _("CPU family:"), desc->family);
	if (desc->model || desc->revision)
		add_summary_s(tb, _("Model:"), desc->revision ? desc->revision : desc->model);
	if (desc->modelname || desc->cpu)
		add_summary_s(tb, _("Model name:"), desc->cpu ? desc->cpu : desc->modelname);
	if (desc->stepping)
		add_summary_s(tb, _("Stepping:"), desc->stepping);
	if (desc->mhz)
		add_summary_s(tb, _("CPU MHz:"), desc->mhz);
	if (desc->dynamic_mhz)
		add_summary_s(tb, _("CPU dynamic MHz:"), desc->dynamic_mhz);
	if (desc->static_mhz)
		add_summary_s(tb, _("CPU static MHz:"), desc->static_mhz);
	if (desc->maxmhz)
		add_summary_s(tb, _("CPU max MHz:"), cpu_max_mhz(desc, buf, sizeof(buf)));
	if (desc->minmhz)
		add_summary_s(tb, _("CPU min MHz:"), cpu_min_mhz(desc, buf, sizeof(buf)));
	if (desc->bogomips)
		add_summary_s(tb, _("BogoMIPS:"), desc->bogomips);
	if (desc->virtflag) {
		if (!strcmp(desc->virtflag, "svm"))
			add_summary_s(tb, _("Virtualization:"), "AMD-V");
		else if (!strcmp(desc->virtflag, "vmx"))
			add_summary_s(tb, _("Virtualization:"), "VT-x");
	}
	if (desc->hypervisor)
		add_summary_s(tb, _("Hypervisor:"), desc->hypervisor);
	if (desc->hyper) {
		add_summary_s(tb, _("Hypervisor vendor:"), hv_vendors[desc->hyper]);
		add_summary_s(tb, _("Virtualization type:"), _(virt_types[desc->virtype]));
	}
	if (desc->dispatching >= 0)
		add_summary_s(tb, _("Dispatching mode:"), _(disp_modes[desc->dispatching]));
	if (desc->ncaches) {
		for (i = desc->ncaches - 1; i >= 0; i--) {
			snprintf(buf, sizeof(buf),
					_("%s cache:"), desc->caches[i].name);
			add_summary_s(tb, buf, desc->caches[i].size);
		}
	}
	if (desc->necaches) {
		for (i = desc->necaches - 1; i >= 0; i--) {
			snprintf(buf, sizeof(buf),
					_("%s cache:"), desc->ecaches[i].name);
			add_summary_s(tb, buf, desc->ecaches[i].size);
		}
	}

	for (i = 0; i < desc->nnodes; i++) {
		snprintf(buf, sizeof(buf), _("NUMA node%d CPU(s):"), desc->idx2nodenum[i]);
		print_cpuset(tb, buf, desc->nodemaps[i], mod->hex);
	}

	if (desc->physsockets) {
		add_summary_n(tb, _("Physical sockets:"), desc->physsockets);
		add_summary_n(tb, _("Physical chips:"), desc->physchips);
		add_summary_n(tb, _("Physical cores/chip:"), desc->physcoresperchip);
	}

	if (desc->flags)
		add_summary_s(tb, _("Flags:"), desc->flags);

	scols_print_table(tb);
	scols_unref_table(tb);
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Display information about the CPU architecture.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all               print both online and offline CPUs (default for -e)\n"), out);
	fputs(_(" -b, --online            print online CPUs only (default for -p)\n"), out);
	fputs(_(" -c, --offline           print offline CPUs only\n"), out);
	fputs(_(" -J, --json              use JSON for default or extended format\n"), out);
	fputs(_(" -e, --extended[=<list>] print out an extended readable format\n"), out);
	fputs(_(" -p, --parse[=<list>]    print out a parsable format\n"), out);
	fputs(_(" -s, --sysroot <dir>     use specified directory as system root\n"), out);
	fputs(_(" -x, --hex               print hexadecimal masks rather than lists of CPUs\n"), out);
	fputs(_(" -y, --physical          print physical instead of logical IDs\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(25));

	fputs(USAGE_COLUMNS, out);
	for (i = 0; i < ARRAY_SIZE(coldescs); i++)
		fprintf(out, " %13s  %s\n", coldescs[i].name, _(coldescs[i].help));

	printf(USAGE_MAN_TAIL("lscpu(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	struct lscpu_modifier _mod = { .mode = OUTPUT_SUMMARY }, *mod = &_mod;
	struct lscpu_desc _desc = { .flags = NULL }, *desc = &_desc;
	int c, i;
	int columns[ARRAY_SIZE(coldescs)], ncolumns = 0;
	int cpu_modifier_specified = 0;
	size_t setsize;

	enum {
		OPT_OUTPUT_ALL = CHAR_MAX + 1,
	};
	static const struct option longopts[] = {
		{ "all",        no_argument,       NULL, 'a' },
		{ "online",     no_argument,       NULL, 'b' },
		{ "offline",    no_argument,       NULL, 'c' },
		{ "help",	no_argument,       NULL, 'h' },
		{ "extended",	optional_argument, NULL, 'e' },
		{ "json",       no_argument,       NULL, 'J' },
		{ "parse",	optional_argument, NULL, 'p' },
		{ "sysroot",	required_argument, NULL, 's' },
		{ "physical",	no_argument,	   NULL, 'y' },
		{ "hex",	no_argument,	   NULL, 'x' },
		{ "version",	no_argument,	   NULL, 'V' },
		{ "output-all",	no_argument,	   NULL, OPT_OUTPUT_ALL },
		{ NULL,		0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'a','b','c' },
		{ 'e','p' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "abce::hJp::s:xyV", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			mod->online = mod->offline = 1;
			cpu_modifier_specified = 1;
			break;
		case 'b':
			mod->online = 1;
			cpu_modifier_specified = 1;
			break;
		case 'c':
			mod->offline = 1;
			cpu_modifier_specified = 1;
			break;
		case 'h':
			usage();
		case 'J':
			mod->json = 1;
			break;
		case 'p':
		case 'e':
			if (optarg) {
				if (*optarg == '=')
					optarg++;
				ncolumns = string_to_idarray(optarg,
						columns, ARRAY_SIZE(columns),
						column_name_to_id);
				if (ncolumns < 0)
					return EXIT_FAILURE;
			}
			mod->mode = c == 'p' ? OUTPUT_PARSABLE : OUTPUT_READABLE;
			break;
		case 's':
			desc->prefix = optarg;
			mod->system = SYSTEM_SNAPSHOT;
			break;
		case 'x':
			mod->hex = 1;
			break;
		case 'y':
			mod->physical = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case OPT_OUTPUT_ALL:
		{
			size_t sz;
			for (sz = 0; sz < ARRAY_SIZE(coldescs); sz++)
				columns[sz] = 1;
			break;
		}
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (cpu_modifier_specified && mod->mode == OUTPUT_SUMMARY) {
		fprintf(stderr,
			_("%s: options --all, --online and --offline may only "
			  "be used with options --extended or --parse.\n"),
			program_invocation_short_name);
		return EXIT_FAILURE;
	}

	if (argc != optind) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	/* set default cpu display mode if none was specified */
	if (!mod->online && !mod->offline) {
		mod->online = 1;
		mod->offline = mod->mode == OUTPUT_READABLE ? 1 : 0;
	}

	ul_path_init_debug();

	/* /sys/devices/system/cpu */
	desc->syscpu = ul_new_path(_PATH_SYS_CPU);
	if (!desc->syscpu)
		err(EXIT_FAILURE, _("failed to initialize CPUs sysfs handler"));
	if (desc->prefix)
		ul_path_set_prefix(desc->syscpu, desc->prefix);

	/* /proc */
	desc->procfs = ul_new_path("/proc");
	if (!desc->procfs)
		err(EXIT_FAILURE, _("failed to initialize procfs handler"));
	if (desc->prefix)
		ul_path_set_prefix(desc->procfs, desc->prefix);

	read_basicinfo(desc, mod);

	setsize = CPU_ALLOC_SIZE(maxcpus);

	for (i = 0; i < desc->ncpuspos; i++) {
		/* only consider present CPUs */
		if (desc->present &&
		    !CPU_ISSET_S(real_cpu_num(desc, i), setsize, desc->present))
			continue;
		read_topology(desc, i);
		read_cache(desc, i);
		read_polarization(desc, i);
		read_address(desc, i);
		read_configured(desc, i);
		read_max_mhz(desc, i);
		read_min_mhz(desc, i);
	}

	if (desc->caches)
		qsort(desc->caches, desc->ncaches,
				sizeof(struct cpu_cache), cachecmp);

	if (desc->ecaches)
		qsort(desc->ecaches, desc->necaches,
				sizeof(struct cpu_cache), cachecmp);

	read_nodes(desc);
	read_hypervisor(desc, mod);
	arm_cpu_decode(desc);

	switch(mod->mode) {
	case OUTPUT_SUMMARY:
		print_summary(desc, mod);
		break;
	case OUTPUT_PARSABLE:
		if (!ncolumns) {
			columns[ncolumns++] = COL_CPU;
			columns[ncolumns++] = COL_CORE;
			columns[ncolumns++] = COL_SOCKET;
			columns[ncolumns++] = COL_NODE;
			columns[ncolumns++] = COL_CACHE;
			mod->compat = 1;
		}
		print_parsable(desc, columns, ncolumns, mod);
		break;
	case OUTPUT_READABLE:
		if (!ncolumns) {
			/* No list was given. Just print whatever is there. */
			columns[ncolumns++] = COL_CPU;
			if (desc->nodemaps)
				columns[ncolumns++] = COL_NODE;
			if (desc->drawermaps)
				columns[ncolumns++] = COL_DRAWER;
			if (desc->bookmaps)
				columns[ncolumns++] = COL_BOOK;
			if (desc->socketmaps)
				columns[ncolumns++] = COL_SOCKET;
			if (desc->coremaps)
				columns[ncolumns++] = COL_CORE;
			if (desc->caches)
				columns[ncolumns++] = COL_CACHE;
			if (desc->online)
				columns[ncolumns++] = COL_ONLINE;
			if (desc->configured)
				columns[ncolumns++] = COL_CONFIGURED;
			if (desc->polarization)
				columns[ncolumns++] = COL_POLARIZATION;
			if (desc->addresses)
				columns[ncolumns++] = COL_ADDRESS;
			if (desc->maxmhz)
				columns[ncolumns++] = COL_MAXMHZ;
			if (desc->minmhz)
				columns[ncolumns++] = COL_MINMHZ;
		}
		print_readable(desc, columns, ncolumns, mod);
		break;
	}

	ul_unref_path(desc->syscpu);
	ul_unref_path(desc->procfs);
	return EXIT_SUCCESS;
}
