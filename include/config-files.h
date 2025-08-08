/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Evaluting a list of configuration filenames which have to be handled/parsed.
 * The order of this file list has been defined by 
 * https://github.com/uapi-group/specifications/blob/main/specs/configuration_files_specification.md
 */

#ifndef UTIL_LINUX_CONFIG_FILES_H
#define UTIL_LINUX_CONFIG_FILES_H

#include "list.h"

struct file_element {
	struct list_head file_list;
	char *filename;
};

/**
 * config_file_list - Evaluting a list of sorted configuration filenames which have to be handled
 *                    in the correct order.
 *
 * @file_list: List of filenames which have to be parsed in that order
 * @project: name of the project used as subdirectory, can be NULL
 * @etc_subdir: absolute directory path for user changed configuration files, can be NULL (default "/etc").
 * @usr_subdir: absolute directory path of vendor defined settings (often "/usr/lib").
 * @config_name: basename of the configuration file. If it is NULL, drop-ins without a main configuration file will be parsed only. 
 * @config_suffix: suffix of the configuration file. Can also be NULL.
 *
 * Returns the length of the file_list.
 *
 * Example:
 * size_t count = 0;
 * struct list_head *file_list;
 *
 * count = config_file_list (&file_list,
 *                           "foo",
 *                           "/etc",
 *                           "/usr/lib",
 *                           "example",
 *                           "conf");
 *
 * The order of this file list has been defined by
 * https://github.com/uapi-group/specifications/blob/main/specs/configuration_files_specification.md
 *
 */

size_t config_file_list( struct list_head *file_list,
			 const char *project,
			 const char *etc_subdir,
			 const char *usr_subdir,
			 const char *config_name,
			 const char *config_suffix);
#endif
