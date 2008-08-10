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
#define _PATH_PROC_XENCAP	"proc/xen/capabilities"
#define _PATH_PROC_CPUINFO	"proc/cpuinfo"

int have_topology;
int have_cache;
int have_node;

/* cache(s) description */
struct ca_desc {
	char	*caname;
	char	*casize;
	int	camap;
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

	/* caches */
	struct ca_desc	cache[CACHE_MAX];

	/* misc */
	char	*mhz;
	char	*stepping;
	char	*flags;

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
	fclose(fp);
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
	FILE *fd;
	char buf[256];

	/* Dom0 Kernel gives wrong information. */
	fd = fopen(_PATH_PROC_XENCAP, "r");
	if (fd) {
		if (fscanf(fd, "%s", buf) == 1 && !strcmp(buf, "control_d"))
			errx(EXIT_FAILURE,
			     _("error: Dom0 Kernel is unsupported."));
		fclose(fd);
	}

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
	char buf[BUFSIZ];

	print_s("Architecture:", cpu->arch);
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
	if (cpu->flags) {
		snprintf(buf, sizeof(buf), " %s ", cpu->flags);
		if (strstr(buf, " svm "))
			print_s(_("Virtualization:"), "AMD-V");
		else if (strstr(buf, " vmx "))
			print_s(_("Virtualization:"), "VT-x");
	}

	if (have_cache) {
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

	/* Show time! */
	if (parsable)
		print_parsable(cpu);
	else
		print_readable(cpu);

	return EXIT_FAILURE;
}
