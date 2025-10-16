/*
 * chmem - Memory configuration tool
 *
 * Copyright IBM Corp. 2016
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org/licenses/>.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <dirent.h>

#include "c.h"
#include "cctype.h"
#include "nls.h"
#include "path.h"
#include "strutils.h"
#include "strv.h"
#include "optutils.h"
#include "closestream.h"
#include "xalloc.h"

/* partial success, otherwise we return regular EXIT_{SUCCESS,FAILURE} */
#define CHMEM_EXIT_SOMEOK		64

#define _PATH_SYS_MEMORY		"/sys/devices/system/memory"
#define _PATH_SYS_MEMCONFIG		"/sys/firmware/memory"

struct chmem_desc {
	struct path_cxt	*sysmem;	/* _PATH_SYS_MEMORY handler */
	struct path_cxt *sysmemconfig;	/* _PATH_SYS_MEMCONFIG directory handler */
	struct dirent	**dirs;
	struct dirent   **memconfig_dirs;
	int		ndirs;
	int		memconfig_ndirs;
	int		memmap_on_memory;
	uint64_t	block_size;
	uint64_t	start;
	uint64_t	end;
	uint64_t	size;
	unsigned int	use_blocks : 1;
	unsigned int	is_size	   : 1;
	unsigned int	verbose	   : 1;
	unsigned int	have_zones : 1;
	unsigned int	have_memconfig : 1;
};

enum {
	CMD_MEMORY_ENABLE = 0,
	CMD_MEMORY_DISABLE,
	CMD_MEMORY_CONFIGURE,
	CMD_MEMORY_DECONFIGURE,
	CMD_NONE
};

enum zone_id {
	ZONE_DMA = 0,
	ZONE_DMA32,
	ZONE_NORMAL,
	ZONE_HIGHMEM,
	ZONE_MOVABLE,
	ZONE_DEVICE,
};

static const char *const zone_names[] = {
	[ZONE_DMA]	= "DMA",
	[ZONE_DMA32]	= "DMA32",
	[ZONE_NORMAL]	= "Normal",
	[ZONE_HIGHMEM]	= "Highmem",
	[ZONE_MOVABLE]	= "Movable",
	[ZONE_DEVICE]	= "Device",
};

/*
 * name must be null-terminated
 */
static int zone_name_to_id(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(zone_names); i++) {
		if (!c_strcasecmp(name, zone_names[i]))
			return i;
	}
	return -1;
}

static void idxtostr(struct chmem_desc *desc, uint64_t idx, char *buf, size_t bufsz)
{
	uint64_t start, end;

	start = idx * desc->block_size;
	end = start + desc->block_size - 1;
	snprintf(buf, bufsz,
		 _("Memory Block %"PRIu64" (0x%016"PRIx64"-0x%016"PRIx64")"),
		 idx, start, end);
}

static bool chmem_memmap_enabled(struct chmem_desc *desc)
{
	if (desc->memmap_on_memory == 0 || desc->memmap_on_memory == 1)
		return true;
	else
		return false;
}

static int chmem_set_memmap_on_memory(struct chmem_desc *desc, char *name)
{
	int rc, index;

	index = strtou64_or_err(name + 6, _("Failed to parse index"));
	rc = ul_path_writef_u64(desc->sysmemconfig, desc->memmap_on_memory,
				"%s/memmap_on_memory", name);
	if (rc) {
		char str[64];
		idxtostr(desc, index, str, sizeof(str));
		warn(_("%s memmap-on-memory failed"), str);
	}
	return rc;
}

static int chmem_config(struct chmem_desc *desc, char *name, int configure)
{
	int mblock_configured, memmap, rc, index;
	char str[BUFSIZ], state[BUFSIZ];

	index = strtou64_or_err(name + 6, _("Failed to parse index"));
	idxtostr(desc, index, str, sizeof(str));
	rc = ul_path_readf_s32(desc->sysmemconfig, &mblock_configured, "%s/config", name);
	if (rc)
		goto out;
	rc = ul_path_readf_s32(desc->sysmemconfig, &memmap, "%s/memmap_on_memory", name);
	if (rc)
		goto out;
	if (mblock_configured) {
		if (configure) {
			if (chmem_memmap_enabled(desc) &&
					memmap != desc->memmap_on_memory) {
				if (!desc->is_size || desc->verbose)
					fprintf(stdout,
						_("%s must be deconfigured before using -m option\n"), str);
				rc = -1;
			} else if (desc->is_size) {
				/*
				 * Allow chmem_onoff_size() to proceed with
				 * configuring different memory blocks when the
				 * current block is already configured.
				 */
				rc = -1;
			} else if (desc->verbose) {
				fprintf(stdout, _("%s already configured\n"), str);
			}
			goto out;
		} else if (ul_path_readf_buffer(desc->sysmem, state,
						sizeof(state), "%s/state", name) > 0 &&
			   strncmp("online", state, 6) == 0) {
			if (!desc->is_size || desc->verbose)
				fprintf(stdout, _("%s must be offline before deconfiguration\n"), str);
			rc = -1;
			goto out;
		}
	} else {
		/*
		 * If memory block is currently in deconfigured state, set
		 * memmap-on-memory if -m option is enabled.
		 */
		if (chmem_memmap_enabled(desc)) {
			rc = chmem_set_memmap_on_memory(desc, name);
			if (rc)
				goto out;
		} else if (!configure) {
			/*
			 * Allow chmem_onoff_size() to proceed with
			 * deconfiguring different memory blocks when the
			 * current block is already deconfigured.
			 */
			if (desc->is_size)
				rc = -1;
			else if (desc->verbose)
				fprintf(stdout, _("%s already deconfigured\n"), str);
			goto out;
		}
	}
	rc = ul_path_writef_u64(desc->sysmemconfig, configure ? 1 : 0, "%s/config", name);
	if (rc) {
		if (!desc->is_size) {
			warn(configure ? _("%s configure failed") :
					 _("%s deconfigure failed"), str);
		} else if (desc->verbose) {
			if (configure)
				fprintf(stdout, _("%s configure failed\n"), str);
			else
				fprintf(stdout, _("%s deconfigure failed\n"), str);
		}
	} else if (desc->verbose) {
		if (configure)
			fprintf(stdout, _("%s configured\n"), str);
		else
			fprintf(stdout, _("%s deconfigured\n"), str);
	}
out:
	return rc;
}

static int chmem_configured(struct chmem_desc *desc, char *name)
{
	int mblock_configured = 0;

	ul_path_readf_s32(desc->sysmemconfig, &mblock_configured, "%s/config", name);
	return mblock_configured;
}

static int chmem_onoff_size(struct chmem_desc *desc, int enable, int zone_id)
{
	char *name, *onoff, line[BUFSIZ], str[BUFSIZ];
	uint64_t size, index;
	int i, rc = 0, ndirs;
	const char *zn;

	size = desc->size;
	onoff = enable ? "online" : "offline";

	if (enable && zone_id >= 0) {
		if (zone_id == ZONE_MOVABLE)
			onoff = "online_movable";
		else
			onoff = "online_kernel";
	}
	ndirs = desc->have_memconfig ? desc->memconfig_ndirs : desc->ndirs;
	i = enable ? 0 : ndirs - 1;
	for (; i >= 0 && i < ndirs && size; i += enable ? 1 : -1) {
		if (desc->have_memconfig)
			name = desc->memconfig_dirs[i]->d_name;
		else
			name = desc->dirs[i]->d_name;
		index = strtou64_or_err(name + 6, _("Failed to parse index"));
		if (enable && desc->have_memconfig && !chmem_configured(desc, name)) {
			/* Configure memory block */
			rc = chmem_config(desc, name, enable);
			if (rc)
				continue;
		} else if (ul_path_readf_buffer(desc->sysmem, line, sizeof(line), "%s/state", name) > 0) {
			if (strncmp(onoff, line, 6) == 0)
				continue;
		} else if (!enable) {
			/*
			 * If /sys/devices/system/memory/memoryX is
			 * unavailable, memory block is offline and
			 * deconfigured.
			 */
			continue;
		}
		if (desc->have_zones) {
			ul_path_readf_buffer(desc->sysmem, line, sizeof(line), "%s/valid_zones", name);
			if (zone_id >= 0) {
				zn = zone_names[zone_id];
				if (enable && !strcasestr(line, zn))
					continue;
				if (!enable && c_strncasecmp(line, zn, strlen(zn)) != 0)
					continue;
			} else if (enable) {
				/* By default, use zone Movable for online, if valid */
				if (strcasestr(line, zone_names[ZONE_MOVABLE]))
					onoff = "online_movable";
				else
					onoff = "online";
			}
		}
		idxtostr(desc, index, str, sizeof(str));
		rc = ul_path_writef_string(desc->sysmem, onoff, "%s/state", name);
		if (rc != 0 && desc->verbose) {
			if (enable)
				fprintf(stdout, _("%s enable failed\n"), str);
			else
				fprintf(stdout, _("%s disable failed\n"), str);
		} else if (rc == 0 && desc->verbose) {
			if (enable)
				fprintf(stdout, _("%s enabled\n"), str);
			else
				fprintf(stdout, _("%s disabled\n"), str);
		}
		if (!rc && !enable && desc->have_memconfig) {
			/* Deconfigure memory block */
			rc = chmem_config(desc, name, enable);
			if (rc)
				continue;
		}
		if (rc == 0)
			size--;
	}
	if (size) {
		uint64_t bytes;
		char *sizestr;

		bytes = (desc->size - size) * desc->block_size;
		sizestr = size_to_human_string(SIZE_SUFFIX_1LETTER, bytes);
		if (enable)
			warnx(_("Could only enable %s of memory"), sizestr);
		else
			warnx(_("Could only disable %s of memory"), sizestr);
		free(sizestr);
	}
	return size == 0 ? 0 : size == desc->size ? -1 : 1;
}

static int chmem_config_size(struct chmem_desc *desc, int configure)
{
	uint64_t size;
	char *name;
	int i, rc;

	if (!desc->have_memconfig) {
		if (configure)
			fprintf(stdout,
				_("Skip configuration — use chmem -e instead\n"));
		else
			fprintf(stdout,
				_("Skip deconfiguration - use chmem -d instead\n"));
		return -1;
	}
	size = desc->size;
	i = configure ? 0 : desc->memconfig_ndirs - 1;
	for (; i >= 0 && i < desc->memconfig_ndirs && size; i += configure ? 1 : -1) {
		name = desc->memconfig_dirs[i]->d_name;
		rc = chmem_config(desc, name, configure);
		if (rc == 0)
			size--;
	}
	if (size) {
		uint64_t bytes;
		char *sizestr;

		bytes = (desc->size - size) * desc->block_size;
		sizestr = size_to_human_string(SIZE_SUFFIX_1LETTER, bytes);
		if (configure)
			fprintf(stdout, _("Could only configure %s of memory\n"), sizestr);
		else
			fprintf(stdout, _("Could only deconfigure %s of memory\n"), sizestr);
		free(sizestr);
	}
	return size == 0 ? 0 : size == desc->size ? -1 : 1;
}

static int chmem_config_range(struct chmem_desc *desc, int configure)
{
	uint64_t index, todo;
	char *name;
	int rc, i;

	if (!desc->have_memconfig) {
		if (configure)
			fprintf(stdout,
				_("Skip configuration — use chmem -e instead\n"));
		else
			fprintf(stdout,
				_("Skip deconfiguration - use chmem -d instead\n"));
		return -1;
	}
	todo = desc->end - desc->start + 1;
	for (i = 0; i < desc->memconfig_ndirs; i++) {
		name = desc->memconfig_dirs[i]->d_name;
		index = strtou64_or_err(name + 6, _("Failed to parse index"));
		if (index < desc->start)
			continue;
		if (index > desc->end)
			break;
		rc = chmem_config(desc, name, configure);
		if (rc == 0)
			todo--;
	}
	return todo == 0 ? 0 : todo == desc->end - desc->start + 1 ? -1 : 1;
}

static int chmem_onoff_range(struct chmem_desc *desc, int enable, int zone_id)
{
	char *name, *onoff, line[BUFSIZ], str[BUFSIZ];
	uint64_t index, todo;
	int i, rc, ndirs;
	const char *zn;

	todo = desc->end - desc->start + 1;
	onoff = enable ? "online" : "offline";

	if (enable && zone_id >= 0) {
		if (zone_id == ZONE_MOVABLE)
			onoff = "online_movable";
		else
			onoff = "online_kernel";
	}

	ndirs = desc->have_memconfig ? desc->memconfig_ndirs : desc->ndirs;
	for (i = 0; i < ndirs; i++) {
		name = desc->have_memconfig ? desc->memconfig_dirs[i]->d_name :
					      desc->dirs[i]->d_name;
		index = strtou64_or_err(name + 6, _("Failed to parse index"));
		if (index < desc->start)
			continue;
		if (index > desc->end)
			break;
		if (enable && desc->have_memconfig && !chmem_configured(desc, name)) {
			/* Configure memory block */
			rc = chmem_config(desc, name, enable);
			if (rc)
				continue;
		}
		idxtostr(desc, index, str, sizeof(str));
		if (ul_path_readf_buffer(desc->sysmem, line, sizeof(line), "%s/state", name) > 0) {
			if (strncmp(onoff, line, 6) == 0) {
				if (desc->verbose && enable)
					fprintf(stdout, _("%s already enabled\n"), str);
				else if (desc->verbose && !enable)
					fprintf(stdout, _("%s already disabled\n"), str);
				todo--;
				continue;
			}
		} else {
			/*
			 * If /sys/devices/system/memory/memoryX is
			 * unavailable, memory block is offline and
			 * deconfigured.
			 */
			if (!enable) {
				if (desc->verbose)
					fprintf(stdout, _("%s already disabled\n"), str);
				todo--;
				continue;
			}
		}

		if (desc->have_zones) {
			ul_path_readf_buffer(desc->sysmem, line, sizeof(line), "%s/valid_zones", name);
			if (zone_id >= 0) {
				zn = zone_names[zone_id];
				if (enable && !strcasestr(line, zn)) {
					warnx(_("%s enable failed: Zone mismatch"), str);
					continue;
				}
				if (!enable && c_strncasecmp(line, zn, strlen(zn)) != 0) {
					warnx(_("%s disable failed: Zone mismatch"), str);
					continue;
				}
			} else if (enable) {
				/* By default, use zone Movable for online, if valid */
				if (strcasestr(line, zone_names[ZONE_MOVABLE]))
					onoff = "online_movable";
				else
					onoff = "online";
			}
		}

		rc = ul_path_writef_string(desc->sysmem, onoff, "%s/state", name);
		if (rc != 0) {
			if (enable)
				warn(_("%s enable failed"), str);
			else
				warn(_("%s disable failed"), str);
		} else if (desc->verbose) {
			if (enable)
				fprintf(stdout, _("%s enabled\n"), str);
			else
				fprintf(stdout, _("%s disabled\n"), str);
		}
		if (!rc && !enable && desc->have_memconfig) {
			/* Deconfigure memory block */
			rc = chmem_config(desc, name, enable);
			if (rc)
				continue;
		}
		if (rc == 0)
			todo--;
	}
	return todo == 0 ? 0 : todo == desc->end - desc->start + 1 ? -1 : 1;
}

static int filter(const struct dirent *de)
{
	if (strncmp("memory", de->d_name, 6) != 0)
		return 0;
	return isdigit_string(de->d_name + 6);
}

static void read_conf(struct chmem_desc *desc)
{
	if (!desc->have_memconfig)
		return;
	desc->memconfig_ndirs = scandir(_PATH_SYS_MEMCONFIG, &desc->memconfig_dirs,
					filter, versionsort);
	if (desc->memconfig_ndirs <= 0)
		err(EXIT_FAILURE, _("Failed to read %s"), _PATH_SYS_MEMCONFIG);
}

static void read_info(struct chmem_desc *desc)
{
	char line[128];

	desc->ndirs = scandir(_PATH_SYS_MEMORY, &desc->dirs, filter, versionsort);
	if (desc->ndirs <= 0)
		goto fail;
	ul_path_read_buffer(desc->sysmem, line, sizeof(line), "block_size_bytes");

	errno = 0;
	desc->block_size = strtoumax(line, NULL, 16);
	if (errno)
		goto fail;
	read_conf(desc);
	return;
fail:
	err(EXIT_FAILURE, _("Failed to read %s"), _PATH_SYS_MEMORY);
}

static void parse_single_param(struct chmem_desc *desc, char *str)
{
	if (desc->use_blocks) {
		desc->start = strtou64_or_err(str, _("Failed to parse block number"));
		desc->end = desc->start;
		return;
	}
	desc->is_size = 1;
	desc->size = strtosize_or_err(str, _("Failed to parse size"));
	if (isdigit(str[strlen(str) - 1]))
		desc->size *= 1024*1024;
	if (desc->size % desc->block_size) {
		errx(EXIT_FAILURE, _("Size must be aligned to memory block size (%s)"),
		     size_to_human_string(SIZE_SUFFIX_1LETTER, desc->block_size));
	}
	desc->size /= desc->block_size;
}

static void parse_range_param(struct chmem_desc *desc, char *start, char *end)
{
	if (desc->use_blocks) {
		desc->start = strtou64_or_err(start, _("Failed to parse start"));
		desc->end = strtou64_or_err(end, _("Failed to parse end"));
		return;
	}
	if (strlen(start) < 2 || start[1] != 'x')
		errx(EXIT_FAILURE, _("Invalid start address format: %s"), start);
	if (strlen(end) < 2 || end[1] != 'x')
		errx(EXIT_FAILURE, _("Invalid end address format: %s"), end);
	desc->start = strtox64_or_err(start, _("Failed to parse start address"));
	desc->end = strtox64_or_err(end, _("Failed to parse end address"));
	if (desc->start % desc->block_size || (desc->end + 1) % desc->block_size) {
		errx(EXIT_FAILURE,
		     _("Start address and (end address + 1) must be aligned to "
		       "memory block size (%s)"),
		     size_to_human_string(SIZE_SUFFIX_1LETTER, desc->block_size));
	}
	desc->start /= desc->block_size;
	desc->end /= desc->block_size;
}

static void parse_parameter(struct chmem_desc *desc, char *param)
{
	char **split;

	split = ul_strv_split(param, "-");
	if (ul_strv_length(split) > 2)
		errx(EXIT_FAILURE, _("Invalid parameter: %s"), param);
	if (ul_strv_length(split) == 1)
		parse_single_param(desc, split[0]);
	else
		parse_range_param(desc, split[0], split[1]);
	ul_strv_free(split);
	if (desc->start > desc->end)
		errx(EXIT_FAILURE, _("Invalid range: %s"), param);
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [SIZE|RANGE|BLOCKRANGE]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Set a particular size or range of memory online or offline.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -e, --enable       enable memory\n"), out);
	fputs(_(" -d, --disable      disable memory\n"), out);
	fputs(_(" -b, --blocks       use memory blocks\n"), out);
	fputs(_(" -z, --zone <name>  select memory zone (see below)\n"), out);
	fputs(_(" -c, --configure    configure range\n"), out);
	fputs(_(" -g, --deconfigure  deconfigure range\n"), out);
	fputs(_(" -m, --memmap-on-memory select memmap-on-memory\n"), out);
	fputs(_(" -v, --verbose      verbose output\n"), out);
	fprintf(out, USAGE_HELP_OPTIONS(20));

	fputs(_("\nSupported zones:\n"), out);
	for (i = 0; i < ARRAY_SIZE(zone_names); i++)
		fprintf(out, " %s\n", zone_names[i]);

	fprintf(out, USAGE_MAN_TAIL("chmem(8)"));

	exit(EXIT_SUCCESS);
}

static int chmem_range(struct chmem_desc *desc, int cmd, int zone_id)
{
	int rc = -1;

	switch (cmd) {
	case CMD_MEMORY_ENABLE:
		rc = chmem_onoff_range(desc, 1, zone_id);
		break;
	case CMD_MEMORY_DISABLE:
		rc = chmem_onoff_range(desc, 0, zone_id);
		break;
	case CMD_MEMORY_CONFIGURE:
		rc = chmem_config_range(desc, 1);
		break;
	case CMD_MEMORY_DECONFIGURE:
		rc = chmem_config_range(desc, 0);
		break;
	default:
		break;
	}
	return rc;
}

static int chmem_size(struct chmem_desc *desc, int cmd, int zone_id)
{
	int rc = -1;

	switch (cmd) {
	case CMD_MEMORY_ENABLE:
		rc = chmem_onoff_size(desc, 1, zone_id);
		break;
	case CMD_MEMORY_DISABLE:
		rc = chmem_onoff_size(desc, 0, zone_id);
		break;
	case CMD_MEMORY_CONFIGURE:
		rc = chmem_config_size(desc, 1);
		break;
	case CMD_MEMORY_DECONFIGURE:
		rc = chmem_config_size(desc, 0);
		break;
	default:
		break;
	}
	return rc;
}

int main(int argc, char **argv)
{
	struct chmem_desc _desc = { 0 }, *desc = &_desc;
	int cmd = CMD_NONE, zone_id = -1;
	char *zone = NULL;
	int c, rc;

	static const struct option longopts[] = {
		{"blocks",	no_argument,		NULL, 'b'},
		{"disable",	no_argument,		NULL, 'd'},
		{"enable",	no_argument,		NULL, 'e'},
		{"help",	no_argument,		NULL, 'h'},
		{"verbose",	no_argument,		NULL, 'v'},
		{"version",	no_argument,		NULL, 'V'},
		{"zone",	required_argument,	NULL, 'z'},
		{"configure",	no_argument,		NULL, 'c'},
		{"deconfigure", no_argument,		NULL, 'g'},
		{"memmap-on-memory", required_argument,	NULL, 'm'},
		{NULL,		0,			NULL, 0}
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'd', 'e', 'g', 'm' },
		{ 'c', 'd', 'e', 'g' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	ul_path_init_debug();
	desc->memmap_on_memory = -1;
	desc->sysmem = ul_new_path(_PATH_SYS_MEMORY);
	if (!desc->sysmem)
		err(EXIT_FAILURE, _("failed to initialize %s handler"), _PATH_SYS_MEMORY);
	desc->sysmemconfig = ul_new_path(_PATH_SYS_MEMCONFIG);
	if (!desc->sysmemconfig)
		err(EXIT_FAILURE, _("failed to initialize %s handler"), _PATH_SYS_MEMCONFIG);
	if (ul_path_access(desc->sysmemconfig, F_OK, "memory0") == 0)
		desc->have_memconfig = 1;
	read_info(desc);

	while ((c = getopt_long(argc, argv, "bcdeghm:vVz:", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'd':
			cmd = CMD_MEMORY_DISABLE;
			break;
		case 'e':
			cmd = CMD_MEMORY_ENABLE;
			break;
		case 'b':
			desc->use_blocks = 1;
			break;
		case 'v':
			desc->verbose = 1;
			break;
		case 'z':
			zone = xstrdup(optarg);
			break;
		case 'c':
			cmd = CMD_MEMORY_CONFIGURE;
			break;
		case 'g':
			cmd = CMD_MEMORY_DECONFIGURE;
			break;
		case 'm':
			desc->memmap_on_memory = atoi(optarg);
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if ((argc == 1) || (argc != optind + 1) || (cmd == CMD_NONE)) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	parse_parameter(desc, argv[optind]);


	/* The valid_zones sysfs attribute was introduced with kernel 3.18 */
	if (ul_path_access(desc->sysmem, F_OK, "memory0/valid_zones") == 0)
		desc->have_zones = 1;
	else if (zone)
		warnx(_("zone ignored, no valid_zones sysfs attribute present"));

	if (zone && desc->have_zones) {
		zone_id = zone_name_to_id(zone);
		if (zone_id == -1) {
			warnx(_("unknown memory zone: %s"), zone);
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (desc->is_size)
		rc = chmem_size(desc, cmd, zone_id);
	else
		rc = chmem_range(desc, cmd, zone_id);

	ul_unref_path(desc->sysmem);

	return rc == 0 ? EXIT_SUCCESS :
		rc < 0 ? EXIT_FAILURE : CHMEM_EXIT_SOMEOK;
}
