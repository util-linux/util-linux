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
#define _PATH_SYS_SYSTEM	"/sys/devices/system"
#define _PATH_SYS_CPU0		_PATH_SYS_SYSTEM "/cpu/cpu0"
#define _PATH_PROC_XEN		"/proc/xen"
#define _PATH_PROC_XENCAP	_PATH_PROC_XEN "/capabilities"
#define _PATH_PROC_CPUINFO	"/proc/cpuinfo"
#define _PATH_PROC_PCIDEVS	"/proc/bus/pci/devices"

int have_topology;
int have_cache;
int have_node;


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

/* cache(s) description */
struct cpu_cache {
	char	*name;
	char	*size;
	int	map;
};

/* global description */
struct lscpu_desc {
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
	struct cpu_cache	cache[CACHE_MAX];

	/* misc */
	char	*mhz;
	char	*stepping;
	char	*flags;

	int	mode;		/* rm, lm or/and tm */

	/* NUMA */
	int	*nodecpu;
};

static size_t sysrootlen;
static char pathbuf[PATH_MAX];

static const char *path_create(const char *path, ...)
		__attribute__ ((__format__ (__printf__, 1, 2)));
static FILE *path_fopen(const char *mode, int exit_on_err, const char *path, ...)
		__attribute__ ((__format__ (__printf__, 3, 4)));
static void path_getstr(char *result, size_t len, const char *path, ...)
		__attribute__ ((__format__ (__printf__, 3, 4)));
static int path_getnum(const char *path, ...)
		__attribute__ ((__format__ (__printf__, 1, 2)));
static int path_exist(const char *path, ...)
		__attribute__ ((__format__ (__printf__, 1, 2)));
static int path_sibling(const char *path, ...)
		__attribute__ ((__format__ (__printf__, 1, 2)));

static const char *
path_vcreate(const char *path, va_list ap)
{
	if (sysrootlen)
		vsnprintf(pathbuf + sysrootlen,
			  sizeof(pathbuf) - sysrootlen, path, ap);
	else
		vsnprintf(pathbuf, sizeof(pathbuf), path, ap);
	return pathbuf;
}

static const char *
path_create(const char *path, ...)
{
	const char *p;
	va_list ap;

	va_start(ap, path);
	p = path_vcreate(path, ap);
	va_end(ap);

	return p;
}

static FILE *
path_vfopen(const char *mode, int exit_on_error, const char *path, va_list ap)
{
	FILE *f;
	const char *p = path_vcreate(path, ap);

	f = fopen(p, mode);
	if (!f && exit_on_error)
		err(EXIT_FAILURE, _("error: cannot open %s"), p);
	return f;
}

static FILE *
path_fopen(const char *mode, int exit_on_error, const char *path, ...)
{
	FILE *fd;
	va_list ap;

	va_start(ap, path);
	fd = path_vfopen("r", exit_on_error, path, ap);
	va_end(ap);

	return fd;
}

static void
path_getstr(char *result, size_t len, const char *path, ...)
{
	FILE *fd;
	va_list ap;

	va_start(ap, path);
	fd = path_vfopen("r", 1, path, ap);
	va_end(ap);

	if (!fgets(result, len, fd))
		err(EXIT_FAILURE, _("failed to read: %s"), pathbuf);
	fclose(fd);

	len = strlen(result);
	if (result[len - 1] == '\n')
		result[len - 1] = '\0';
}

static int
path_getnum(const char *path, ...)
{
	FILE *fd;
	va_list ap;
	int result;

	va_start(ap, path);
	fd = path_vfopen("r", 1, path, ap);
	va_end(ap);

	if (fscanf(fd, "%d", &result) != 1) {
		if (ferror(fd))
			err(EXIT_FAILURE, _("failed to read: %s"), pathbuf);
		else
			errx(EXIT_FAILURE, _("parse error: %s"), pathbuf);
	}
	fclose(fd);
	return result;
}

static int
path_exist(const char *path, ...)
{
	va_list ap;
	const char *p;

	va_start(ap, path);
	p = path_vcreate(path, ap);
	va_end(ap);

	return access(p, F_OK) == 0;
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
	fp = path_vfopen("r", 1, path, ap);
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
read_basicinfo(struct lscpu_desc *desc)
{
	FILE *fp = path_fopen("r", 1, _PATH_PROC_CPUINFO);
	char buf[BUFSIZ];
	struct utsname utsbuf;

	/* architecture */
	if (uname(&utsbuf) == -1)
		err(EXIT_FAILURE, _("error: uname failed"));
	desc->arch = xstrdup(utsbuf.machine);

	/* count CPU(s) */
	while(path_exist(_PATH_SYS_SYSTEM "/cpu/cpu%d", desc->ct_cpu))
		desc->ct_cpu++;

	/* details */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		/* IA64 */
		if (lookup(buf, "vendor", &desc->vendor)) ;
		else if (lookup(buf, "vendor_id", &desc->vendor)) ;
		/* IA64 */
		else if (lookup(buf, "family", &desc->family)) ;
		else if (lookup(buf, "cpu family", &desc->family)) ;
		else if (lookup(buf, "model", &desc->model)) ;
		else if (lookup(buf, "stepping", &desc->stepping)) ;
		else if (lookup(buf, "cpu MHz", &desc->mhz)) ;
		else if (lookup(buf, "flags", &desc->flags)) ;
		else
			continue;
	}

	if (desc->flags) {
		snprintf(buf, sizeof(buf), " %s ", desc->flags);
		if (strstr(buf, " svm "))
			desc->virtflag = strdup("svm");
		else if (strstr(buf, " vmx "))
			desc->virtflag = strdup("vmx");

		if (strstr(buf, " rm "))
			desc->mode |= MODE_REAL;
		if (strstr(buf, " tm "))
			desc->mode |= MODE_TRANSPARENT;
		if (strstr(buf, " lm "))
			desc->mode |= MODE_LONG;
	}

	fclose(fp);
}

static int
has_pci_device(int vendor, int device)
{
	FILE *f;
	int num, fn, ven, dev;
	int res = 1;

	f = path_fopen("r", 0, _PATH_PROC_PCIDEVS);
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
}

#else	/* ! __x86_64__ */
static void
read_hypervisor_cpuid(struct lscpu_desc *desc)
{
}
#endif

static void
read_hypervisor(struct lscpu_desc *desc)
{
	read_hypervisor_cpuid(desc);

	if (desc->hyper)
		/* hvm */
		desc->virtype = VIRT_FULL;

	else if (path_exist(_PATH_PROC_XEN)) {
		/* Xen para-virt or dom0 */
		FILE *fd = path_fopen("r", 0, _PATH_PROC_XENCAP);
		int dom0 = 0;

		if (fd) {
			char buf[256];

			if (fscanf(fd, "%s", buf) == 1 &&
			    !strcmp(buf, "control_d"))
				dom0 = 1;
			fclose(fd);
		}
		desc->virtype = dom0 ? VIRT_NONE : VIRT_PARA;
		desc->hyper = HYPER_XEN;

	} else if (has_pci_device(0x5853, 0x0001)) {
		/* Xen full-virt on non-x86_64 */
		desc->hyper = HYPER_XEN;
		desc->virtype = VIRT_FULL;
	}
}

static void
read_topology(struct lscpu_desc *desc)
{
	/* number of threads */
	desc->ct_thread = path_sibling(
				_PATH_SYS_CPU0 "/topology/thread_siblings");

	/* number of cores */
	desc->ct_core = path_sibling(
				_PATH_SYS_CPU0 "/topology/core_siblings")
			/ desc->ct_thread;

	/* number of sockets */
	desc->ct_socket = desc->ct_cpu / desc->ct_core / desc->ct_thread;
}

static void
read_cache(struct lscpu_desc *desc)
{
	char buf[256];
	DIR *dp;
	struct dirent *dir;
	int level, type;
	const char *p = path_create(_PATH_SYS_CPU0 "/cache");

	dp = opendir(p);
	if (dp == NULL)
		err(EXIT_FAILURE, _("error: %s"), p);

	while ((dir = readdir(dp)) != NULL) {
		if (!strcmp(dir->d_name, ".")
		    || !strcmp(dir->d_name, ".."))
			continue;

		/* cache type */
		path_getstr(buf, sizeof(buf),
				_PATH_SYS_CPU0 "/cache/%s/type", dir->d_name);
		if (!strcmp(buf, "Data"))
			type = 'd';
		else if (!strcmp(buf, "Instruction"))
			type = 'i';
		else
			type = 0;

		/* cache level */
		level = path_getnum(_PATH_SYS_CPU0 "/cache/%s/level",
				dir->d_name);

		if (type)
			snprintf(buf, sizeof(buf), "L%d%c", level, type);
		else
			snprintf(buf, sizeof(buf), "L%d", level);

		desc->cache[desc->ct_cache].name = xstrdup(buf);

		/* cache size */
		path_getstr(buf, sizeof(buf),
				_PATH_SYS_CPU0 "/cache/%s/size", dir->d_name);
		desc->cache[desc->ct_cache].size = xstrdup(buf);

		/* information about how CPUs share different caches */
		desc->cache[desc->ct_cache].map = path_sibling(
				_PATH_SYS_CPU0 "/cache/%s/shared_cpu_map",
				dir->d_name);
		desc->ct_cache++;
	}
}

static void
read_nodes(struct lscpu_desc *desc)
{
	int i;

	/* number of NUMA node */
	while (path_exist(_PATH_SYS_SYSTEM "/node/node%d", desc->ct_node))
		desc->ct_node++;

	desc->nodecpu = (int *) malloc(desc->ct_node * sizeof(int));
	if (!desc->nodecpu)
		err(EXIT_FAILURE, _("error: malloc failed"));

	/* information about how nodes share different CPUs */
	for (i = 0; i < desc->ct_node; i++)
		desc->nodecpu[i] = path_sibling(
					_PATH_SYS_SYSTEM "/node/node%d/cpumap",
					i);
}

static void
check_system(void)
{
	/* Read through sysfs. */
	if (!path_exist(_PATH_SYS_SYSTEM))
		errx(EXIT_FAILURE,
		     _("error: %s is not accessable."), pathbuf);

	if (path_exist(_PATH_SYS_SYSTEM "/node"))
		have_node = 1;

	if (path_exist(_PATH_SYS_CPU0 "/topology/thread_siblings"))
		have_topology = 1;

	if (path_exist(_PATH_SYS_CPU0 "/cache"))
		have_cache = 1;
}

static void
print_parsable(struct lscpu_desc *desc)
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

		for (i = desc->ct_cache - 1; i >= 0; i--)
			printf(",%s", desc->cache[i].name);
	}
	putchar('\n');

	for (i = 0; i < desc->ct_cpu; i++) {
		printf("%d", i);

		if (have_topology)
			printf(",%d,%d",
				i / desc->ct_thread,
			        i / desc->ct_core / desc->ct_thread);
		else
			printf(",,");

		if (have_node) {
			int c = 0;

			for (j = 0; j < desc->ct_node; j++) {
				c += desc->nodecpu[j];
				if (i < c) {
					printf(",%d", j);
					break;
				}
			}
		} else
			putchar(',');

		if (have_cache) {
			putchar(',');

			for (j = desc->ct_cache - 1; j >= 0; j--) {
				/* If shared_cpu_map is 0, all CPUs share the same
				   cache. */
				if (desc->cache[j].map == 0)
					desc->cache[j].map = desc->ct_core *
							      desc->ct_thread;

				printf(",%d", i / desc->cache[j].map);
			}
		}
		putchar('\n');
	}
}


/* output formats "<key>  <value>"*/
#define print_s(_key, _val)	printf("%-23s%s\n", _key, _val)
#define print_n(_key, _val)	printf("%-23s%d\n", _key, _val)

static void
print_readable(struct lscpu_desc *desc)
{
	print_s("Architecture:", desc->arch);

	if (desc->mode & (MODE_REAL | MODE_TRANSPARENT | MODE_LONG)) {
		char buf[64], *p = buf;

		if (desc->mode & MODE_REAL) {
			strcpy(p, "16-bit, ");
			p += 8;
		}
		if (desc->mode & MODE_TRANSPARENT) {
			strcpy(p, "32-bit, ");
			p += 8;
		}
		if (desc->mode & MODE_LONG) {
			strcpy(p, "64-bit, ");
			p += 8;
		}
		*(p - 2) = '\0';
		print_s(_("CPU op-mode(s):"), buf);
	}

	print_n("CPU(s):", desc->ct_cpu);

	if (have_topology) {
		print_n(_("Thread(s) per core:"), desc->ct_thread);
		print_n(_("Core(s) per socket:"), desc->ct_core);
		print_n(_("CPU socket(s):"), desc->ct_socket);
	}

	if (have_node)
		print_n(_("NUMA node(s):"), desc->ct_node);
	if (desc->vendor)
		print_s(_("Vendor ID:"), desc->vendor);
	if (desc->family)
		print_s(_("CPU family:"), desc->family);
	if (desc->model)
		print_s(_("Model:"), desc->model);
	if (desc->stepping)
		print_s(_("Stepping:"), desc->stepping);
	if (desc->mhz)
		print_s(_("CPU MHz:"), desc->mhz);
	if (desc->virtflag) {
		if (!strcmp(desc->virtflag, "svm"))
			print_s(_("Virtualization:"), "AMD-V");
		else if (!strcmp(desc->virtflag, "vmx"))
			print_s(_("Virtualization:"), "VT-x");
	}
	if (desc->hyper) {
		print_s(_("Hypervisor vendor:"), hv_vendors[desc->hyper]);
		print_s(_("Virtualization type:"), virt_types[desc->virtype]);
	}
	if (have_cache) {
		char buf[512];
		int i;

		for (i = desc->ct_cache - 1; i >= 0; i--) {
			snprintf(buf, sizeof(buf),
					_("%s cache:"), desc->cache[i].name);
			print_s(buf, desc->cache[i].size);
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
	struct cpu_cache *c1 = (struct cpu_cache *) a;
	struct cpu_cache *c2 = (struct cpu_cache *) b;

	return strcmp(c2->name, c1->name);
}

int main(int argc, char *argv[])
{
	struct lscpu_desc _desc, *desc = &_desc;
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
			sysrootlen = strlen(optarg);
			strncpy(pathbuf, optarg, sizeof(pathbuf));
			pathbuf[sizeof(pathbuf) - 1] = '\0';
			break;
		default:
			usage(EXIT_FAILURE);
		}
	}

	memset(desc, 0, sizeof(*desc));

	check_system();

	read_basicinfo(desc);

	if (have_topology)
		read_topology(desc);
	if (have_cache) {
		read_cache(desc);
		qsort(desc->cache, desc->ct_cache, sizeof(struct cpu_cache), ca_compare);
	}
	if (have_node)
		read_nodes(desc);

	read_hypervisor(desc);

	/* Show time! */
	if (parsable)
		print_parsable(desc);
	else
		print_readable(desc);

	return EXIT_SUCCESS;
}
