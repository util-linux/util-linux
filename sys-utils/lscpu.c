/*
 * lscpu - CPU architecture information helper
 *
 * Copyright (C) 2008 Cai Qian <qcai@redhat.com>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdarg.h>

#include "nls.h"

#define CACHE_MAX 100

/* /sys paths */
#define _PATH_SYS_SYSTEM	"sys/devices/system"
#define _PATH_SYS_CPU0		_PATH_SYS_SYSTEM "/cpu/cpu0"
#define _PATH_PROC_XEN		"proc/xen"
#define _PATH_PROC_XENCAP	_PATH_PROC_XEN "/capabilities"
#define _PATH_PROC_CPUINFO	"proc/cpuinfo"
#define _PATH_PROC_PCIDEVS	"proc/bus/pci/devices"

int have_topology;
int have_cache;
int have_node;

/* cache(s) description */
struct ca_desc {
	char	*caname;
	char	*casize;
	int	camap;
};

/* virtualization types */
enum {
	VIRT_NONE	= 0,
	VIRT_PARA,
	VIRT_FULL
};
const char *virt_types[] = {
	[VIRT_NONE]	= N_("none"),
	[VIRT_PARA]	= N_("para"),
	[VIRT_FULL]	= N_("full")
};

/* hypervisor vendors */
enum {
	HYPER_NONE	= 0,
	HYPER_XEN,
	HYPER_KVM,
	HYPER_MSHV
};
const char *hv_vendors[] = {
	[HYPER_NONE]	= NULL,
	[HYPER_XEN]	= "Xen",
	[HYPER_KVM]	= "KVM",
	[HYPER_MSHV]	= "Microsoft"
};

/* CPU modes (bits) */
enum {
	MODE_REAL	= (1 << 1),
	MODE_TRANSPARENT = (1 << 2),
	MODE_LONG	= (1 << 3)
};

/* CPU(s) description */
struct cpu_desc {
	/* counters */
	int	ct_cpu;
	int	ct_thread;
	int	ct_core;
	int	ct_socket;
	int	ct_node;
	int	ct_cache;

	/* who is who */
	char	*arch;
	char	*vendor;
	char	*family;
	char	*model;
	char	*virtflag;	/* virtualization flag (vmx, svm) */
	int	hyper;		/* hypervisor vendor ID */
	int	virtype;	/* VIRT_PARA|FULL|NONE ? */

	/* caches */
	struct ca_desc	cache[CACHE_MAX];

	/* misc */
	char	*mhz;
	char	*stepping;
	char	*flags;

	int	mode;		/* rm, lm or/and tm */

	/* NUMA */
	int	*nodecpu;
};

char pathbuf[PATH_MAX] = "/";

static void path_scanstr(char *result, const char *path, ...)
		__attribute__ ((__format__ (__printf__, 2, 3)));
static int path_exist(const char *path, ...)
		__attribute__ ((__format__ (__printf__, 1, 2)));
static int path_sibling(const char *path, ...)
		__attribute__ ((__format__ (__printf__, 1, 2)));

static FILE *
xfopen(const char *path, const char *mode)
{
	FILE *fd = fopen(path, mode);
	if (!fd)
		err(EXIT_FAILURE, _("error: %s"), path);
	return fd;
}

static FILE *
path_vfopen(const char *mode, const char *path, va_list ap)
{
	vsnprintf(pathbuf, sizeof(pathbuf), path, ap);
	return xfopen(pathbuf, mode);
}

static void
path_scanstr(char *result, const char *path, ...)
{
	FILE *fd;
	va_list ap;

	va_start(ap, path);
	fd = path_vfopen("r", path, ap);
	va_end(ap);

	if (fscanf(fd, "%s", result) != 1) {
		if (ferror(fd))
			err(EXIT_FAILURE, _("error: %s"), pathbuf);
		else
			errx(EXIT_FAILURE, _("error parse: %s"), pathbuf);
	}
	fclose(fd);
}

static int
path_exist(const char *path, ...)
{
	va_list ap;

	va_start(ap, path);
	vsnprintf(pathbuf, sizeof(pathbuf), path, ap);
	va_end(ap);

	return access(pathbuf, F_OK) == 0;
}

char *
xstrdup(const char *str)
{
	char *s = strdup(str);
	if (!s)
		err(EXIT_FAILURE, _("error: strdup failed"));
	return s;
}

/* count the set bit in a mapping file */
static int
path_sibling(const char *path, ...)
{
	int c, n;
	int result = 0;
	char s[2];
	FILE *fp;
	va_list ap;

	va_start(ap, path);
	fp = path_vfopen("r", path, ap);
	va_end(ap);

	while ((c = fgetc(fp)) != EOF) {
		if (isxdigit(c)) {
			s[0] = c;
			s[1] = '\0';
			for (n = strtol(s, NULL, 16); n > 0; n /= 2) {
				if (n % 2)
					result++;
			}
		}
	}
	fclose(fp);

	return result;
}

/* Lookup a pattern and get the value from cpuinfo.
 * Format is:
 *
 *	"<pattern>   : <key>"
 */
int lookup(char *line, char *pattern, char **value)
{
	char *p, *v;
	int len = strlen(pattern);

	if (!*line)
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

static void
read_basicinfo(struct cpu_desc *cpu)
{
	FILE *fp = xfopen(_PATH_PROC_CPUINFO, "r");
	char buf[BUFSIZ];
	struct utsname utsbuf;

	/* architecture */
	if (uname(&utsbuf) == -1)
		err(EXIT_FAILURE, _("error: uname failed"));
	cpu->arch = xstrdup(utsbuf.machine);

	/* count CPU(s) */
	while(path_exist(_PATH_SYS_SYSTEM "/cpu/cpu%d", cpu->ct_cpu))
		cpu->ct_cpu++;

	/* details */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		/* IA64 */
		if (lookup(buf, "vendor", &cpu->vendor)) ;
		else if (lookup(buf, "vendor_id", &cpu->vendor)) ;
		/* IA64 */
		else if (lookup(buf, "family", &cpu->family)) ;
		else if (lookup(buf, "cpu family", &cpu->family)) ;
		else if (lookup(buf, "model", &cpu->model)) ;
		else if (lookup(buf, "stepping", &cpu->stepping)) ;
		else if (lookup(buf, "cpu MHz", &cpu->mhz)) ;
		else if (lookup(buf, "flags", &cpu->flags)) ;
		else
			continue;
	}

	if (cpu->flags) {
		snprintf(buf, sizeof(buf), " %s ", cpu->flags);
		if (strstr(buf, " svm "))
			cpu->virtflag = strdup("svm");
		else if (strstr(buf, " vmx "))
			cpu->virtflag = strdup("vmx");

		if (strstr(buf, " rm "))
			cpu->mode |= MODE_REAL;
		if (strstr(buf, " tm "))
			cpu->mode |= MODE_TRANSPARENT;
		if (strstr(buf, " lm "))
			cpu->mode |= MODE_LONG;
	}

	fclose(fp);
}

static int
has_pci_device(int vendor, int device)
{
	FILE *f;
	int num, fn, ven, dev;
	int res = 1;

	f = fopen(_PATH_PROC_PCIDEVS, "r");
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
		"pushl %%ebx;"
		"cpuid;"
		"movl %%ebx, %%esi;"
		"popl %%ebx;"
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
read_hypervisor_cpuid(struct cpu_desc *cpu)
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
		cpu->hyper = HYPER_XEN;
	else if (!strncmp("KVMKVMKVM", hyper_vendor_id, 9))
		cpu->hyper = HYPER_KVM;
	else if (!strncmp("Microsoft Hv", hyper_vendor_id, 12))
		cpu->hyper = HYPER_MSHV;
}

#else	/* ! __x86_64__ */
static void
read_hypervisor_cpuid(struct cpu_desc *cpu)
{
}
#endif

static void
read_hypervisor(struct cpu_desc *cpu)
{
	read_hypervisor_cpuid(cpu);

	if (cpu->hyper)
		/* hvm */
		cpu->virtype = VIRT_FULL;

	else if (!access(_PATH_PROC_XEN, F_OK)) {
		/* Xen para-virt or dom0 */
		FILE *fd = fopen(_PATH_PROC_XENCAP, "r");
		int dom0 = 0;

		if (fd) {
			char buf[256];

			if (fscanf(fd, "%s", buf) == 1 &&
			    !strcmp(buf, "control_d"))
				dom0 = 1;
			fclose(fd);
		}
		cpu->virtype = dom0 ? VIRT_NONE : VIRT_PARA;
		cpu->hyper = HYPER_XEN;

	} else if (has_pci_device(0x5853, 0x0001)) {
		/* Xen full-virt on non-x86_64 */
		cpu->hyper = HYPER_XEN;
		cpu->virtype = VIRT_FULL;
	}
}

static void
read_topology(struct cpu_desc *cpu)
{
	/* number of threads */
	cpu->ct_thread = path_sibling(
				_PATH_SYS_CPU0 "/topology/thread_siblings");

	/* number of cores */
	cpu->ct_core = path_sibling(
				_PATH_SYS_CPU0 "/topology/core_siblings")
			/ cpu->ct_thread;

	/* number of sockets */
	cpu->ct_socket = cpu->ct_cpu / cpu->ct_core / cpu->ct_thread;
}

static void
read_cache(struct cpu_desc *cpu)
{
	char buf[256];
	DIR *dp;
	struct dirent *dir;
	int level, type;

	dp = opendir(_PATH_SYS_CPU0 "/cache");
	if (dp == NULL)
		err(EXIT_FAILURE, _("error: %s"), _PATH_SYS_CPU0 "/cache");

	while ((dir = readdir(dp)) != NULL) {
		if (!strcmp(dir->d_name, ".")
		    || !strcmp(dir->d_name, ".."))
			continue;

		/* cache type */
		path_scanstr(buf, _PATH_SYS_CPU0 "/cache/%s/type", dir->d_name);
		if (!strcmp(buf, "Data"))
			type = 'd';
		else if (!strcmp(buf, "Instruction"))
			type = 'i';
		else
			type = 0;

		/* cache level */
		path_scanstr(buf, _PATH_SYS_CPU0 "/cache/%s/level", dir->d_name);
		level = atoi(buf);

		if (type)
			snprintf(buf, sizeof(buf), "L%d%c", level, type);
		else
			snprintf(buf, sizeof(buf), "L%d", level);

		cpu->cache[cpu->ct_cache].caname = xstrdup(buf);

		/* cache size */
		path_scanstr(buf, _PATH_SYS_CPU0 "/cache/%s/size", dir->d_name);
		cpu->cache[cpu->ct_cache].casize = xstrdup(buf);

		/* information about how CPUs share different caches */
		cpu->cache[cpu->ct_cache].camap = path_sibling(
				_PATH_SYS_CPU0 "/cache/%s/shared_cpu_map",
				dir->d_name);
		cpu->ct_cache++;
	}
}

static void
read_nodes(struct cpu_desc *cpu)
{
	int i;

	/* number of NUMA node */
	while (path_exist(_PATH_SYS_SYSTEM "/node/node%d", cpu->ct_node))
		cpu->ct_node++;

	cpu->nodecpu = (int *) malloc(cpu->ct_node * sizeof(int));
	if (!cpu->nodecpu)
		err(EXIT_FAILURE, _("error: malloc failed"));

	/* information about how nodes share different CPUs */
	for (i = 0; i < cpu->ct_node; i++)
		cpu->nodecpu[i] = path_sibling(
					_PATH_SYS_SYSTEM "/node/node%d/cpumap",
					i);
}

static void
check_system(void)
{
	/* Read through sysfs. */
	if (access(_PATH_SYS_SYSTEM, F_OK))
		errx(EXIT_FAILURE,
		     _("error: /sys filesystem is not accessable."));

	if (!access(_PATH_SYS_SYSTEM "/node", F_OK))
		have_node = 1;

	if (!access(_PATH_SYS_CPU0 "/topology/thread_siblings", F_OK))
		have_topology = 1;

	if (!access(_PATH_SYS_CPU0 "/cache", F_OK))
		have_cache = 1;
}

static void
print_parsable(struct cpu_desc *cpu)
{
	int i, j;

	printf(_(
	"# The following is the parsable format, which can be fed to other\n"
	"# programs. Each different item in every column has an unique ID\n"
	"# starting from zero.\n"
	"# CPU,Core,Socket,Node"));

	if (have_cache) {
		/* separator between CPU topology and cache information */
		putchar(',');

		for (i = cpu->ct_cache - 1; i >= 0; i--)
			printf(",%s", cpu->cache[i].caname);
	}
	putchar('\n');

	for (i = 0; i < cpu->ct_cpu; i++) {
		printf("%d", i);

		if (have_topology)
			printf(",%d,%d",
				i / cpu->ct_thread,
			        i / cpu->ct_core / cpu->ct_thread);
		else
			printf(",,");

		if (have_node) {
			int c = 0;

			for (j = 0; j < cpu->ct_node; j++) {
				c += cpu->nodecpu[j];
				if (i < c) {
					printf(",%d", j);
					break;
				}
			}
		} else
			putchar(',');

		if (have_cache) {
			putchar(',');

			for (j = cpu->ct_cache - 1; j >= 0; j--) {
				/* If shared_cpu_map is 0, all CPUs share the same
				   cache. */
				if (cpu->cache[j].camap == 0)
					cpu->cache[j].camap = cpu->ct_core *
							      cpu->ct_thread;

				printf(",%d", i / cpu->cache[j].camap);
			}
		}
		putchar('\n');
	}
}


/* output formats "<key>  <value>"*/
#define print_s(_key, _val)	printf("%-23s%s\n", _key, _val)
#define print_n(_key, _val)	printf("%-23s%d\n", _key, _val)

static void
print_readable(struct cpu_desc *cpu)
{
	print_s("Architecture:", cpu->arch);

	if (cpu->mode & (MODE_REAL | MODE_TRANSPARENT | MODE_LONG)) {
		char buf[64], *p = buf;

		if (cpu->mode & MODE_REAL) {
			strcpy(p, "16-bit, ");
			p += 8;
		}
		if (cpu->mode & MODE_TRANSPARENT) {
			strcpy(p, "32-bit, ");
			p += 8;
		}
		if (cpu->mode & MODE_LONG) {
			strcpy(p, "64-bit, ");
			p += 8;
		}
		*(p - 2) = '\0';
		print_s(_("CPU op-mode(s):"), buf);
	}

	print_n("CPU(s):", cpu->ct_cpu);

	if (have_topology) {
		print_n(_("Thread(s) per core:"), cpu->ct_thread);
		print_n(_("Core(s) per socket:"), cpu->ct_core);
		print_n(_("CPU socket(s):"), cpu->ct_socket);
	}

	if (have_node)
		print_n(_("NUMA node(s):"), cpu->ct_node);
	if (cpu->vendor)
		print_s(_("Vendor ID:"), cpu->vendor);
	if (cpu->family)
		print_s(_("CPU family:"), cpu->family);
	if (cpu->model)
		print_s(_("Model:"), cpu->model);
	if (cpu->stepping)
		print_s(_("Stepping:"), cpu->stepping);
	if (cpu->mhz)
		print_s(_("CPU MHz:"), cpu->mhz);
	if (cpu->virtflag) {
		if (!strcmp(cpu->virtflag, "svm"))
			print_s(_("Virtualization:"), "AMD-V");
		else if (!strcmp(cpu->virtflag, "vmx"))
			print_s(_("Virtualization:"), "VT-x");
	}
	if (cpu->hyper) {
		print_s(_("Hypervisor vendor:"), hv_vendors[cpu->hyper]);
		print_s(_("Virtualization type:"), virt_types[cpu->virtype]);
	}
	if (have_cache) {
		char buf[512];
		int i;

		for (i = cpu->ct_cache - 1; i >= 0; i--) {
			snprintf(buf, sizeof(buf),
					_("%s cache:"), cpu->cache[i].caname);
			print_s(buf, cpu->cache[i].casize);
		}
	}
}

void usage(int rc)
{
	printf(_("Usage: %s [option]\n"),
			program_invocation_short_name);

	puts(_(	"CPU architecture information helper\n\n"
		"  -h, --help     usage information\n"
		"  -p, --parse    print out in parsable instead of printable format.\n"
		"  -s, --sysroot  use the directory as a new system root.\n"));
	exit(rc);
}

static int
ca_compare(const void *a, const void *b)
{
	struct ca_desc *cache1 = (struct ca_desc *) a;
	struct ca_desc *cache2 = (struct ca_desc *) b;

	return strcmp(cache2->caname, cache1->caname);
}

int main(int argc, char *argv[])
{
	struct cpu_desc _cpu, *cpu = &_cpu;
	int parsable = 0, c;

	struct option longopts[] = {
		{ "help",	no_argument,       0, 'h' },
		{ "parse",	no_argument,       0, 'p' },
		{ "sysroot",	required_argument, 0, 's' },
		{ NULL,		0, 0, 0 }
	};

	setlocale(LC_MESSAGES, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while((c = getopt_long(argc, argv, "hps:", longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(EXIT_SUCCESS);
		case 'p':
			parsable = 1;
			break;
		case 's':
			strncpy(pathbuf, optarg, sizeof(pathbuf));
			break;
		default:
			usage(EXIT_FAILURE);
		}
	}

	if (chdir(pathbuf) == -1)
		errx(EXIT_FAILURE,
		     _("error: change working directory to %s."), pathbuf);

	memset(cpu, 0, sizeof(*cpu));

	check_system();

	read_basicinfo(cpu);

	if (have_topology)
		read_topology(cpu);
	if (have_cache) {
		read_cache(cpu);
		qsort(cpu->cache, cpu->ct_cache, sizeof(struct ca_desc), ca_compare);
	}
	if (have_node)
		read_nodes(cpu);

	read_hypervisor(cpu);

	/* Show time! */
	if (parsable)
		print_parsable(cpu);
	else
		print_readable(cpu);

	return EXIT_SUCCESS;
}
