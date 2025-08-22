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
 * Returns the length of the file_list, or -ENOMEM, or -ENOTEMPTY if config_name is NULL
 *
 * Example:
 * int count = 0;
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
int config_file_list( struct list_head *file_list,
		      const char *project,
		      const char *etc_subdir,
		      const char *usr_subdir,
		      const char *config_name,
		      const char *config_suffix);


/**
 * free_config_file_list - Freeing configuration list.
 *
 * @file_list: List of filenames which has to be freed.
 *
 */
void free_config_file_list(struct list_head *file_list);


/**
 * config_files_next_filename - Going through the file list which has to be handled/parsed.
 *
 * @file_list: List of filenames which have to be handled.
 * @current_entry: Current list entry. Has to be initialized with NULL for the first call.
 * @name: Returned file name for each call.
 *
 * Returns true/false. Call has been successful.
 *
 * Example:
 * int count = 0;
 * struct list_head *file_list = NULL;
 * struct list_head *current = NULL;
 * char *name = NULL;
 *
 * count = config_file_list (&file_list,
 *                           "foo",
 *                           "/etc",
 *                           "/usr/lib",
 *                           "example",
 *                           "conf");
 *
 * while (config_files_next_filename(&file_list, &current, &name))
 *       printf("filename: %s\n", name);
 *
 * free_config_file_list(&file_list);
 *
 */
bool config_files_next_filename(struct list_head *file_list,
				struct list_head **current_entry,
				char **name);

#endif
