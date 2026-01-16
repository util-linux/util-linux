/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Evaluting a list of configuration filenames which have to be handled/parsed.
 *
 * The order of this file list has been defined by
 * https://github.com/uapi-group/specifications/blob/main/specs/configuration_files_specification.md
 */

#ifndef UTIL_LINUX_CONFIGS_H
#define UTIL_LINUX_CONFIGS_H

#include "list.h"

/**
 * ul_configs_file_list - Evaluting a list of sorted configuration filenames which have to be handled
 *                        in the correct order.
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
 * count = ul_configs_file_list(&file_list,
 *                              "foo",
 *                              "/etc",
 *                              "/usr/lib",
 *                              "example",
 *                              "conf");
 *
 * The order of this file list has been defined by
 * https://github.com/uapi-group/specifications/blob/main/specs/configuration_files_specification.md
 *
 */
int ul_configs_file_list(struct list_head *file_list,
			 const char *project,
			 const char *etcdir,
			 const char *rundir,
			 const char *usrdir,
			 const char *confname,
			 const char *suffix);


/**
 * ul_configs_free_list - Freeing configuration list.
 *
 * @file_list: List of filenames which has to be freed.
 *
 */
void ul_configs_free_list(struct list_head *file_list);


/**
 * ul_configs_next_filename - Going through the file list which has to be handled/parsed.
 *
 * @file_list: List of filenames which have to be handled.
 * @current_entry: Current list entry. Has to be initialized with NULL for the first call.
 * @name: Returned file name for each call.
 *
 * Returns 0 on success, <0 on error and 1 if the end of the list has been reached.
 *
 * Example:
 * int count = 0;
 * struct list_head *file_list = NULL;
 * struct list_head *current = NULL;
 * char *name = NULL;
 *
 * count = ul_configs_file_list(&file_list,
 *                              "foo",
 *                              "/etc",
 *                              "/usr/lib",
 *                              "example",
 *                              "conf");
 *
 * while (ul_configs_next_filename(&file_list, &current, &name) == 0)
 *       printf("filename: %s\n", name);
 *
 * ul_configs_free_list(&file_list);
 *
 */
int ul_configs_next_filename(struct list_head *file_list,
			     struct list_head **current_entry,
			     char **name);

#endif /* UTIL_LINUX_CONFIGS_H */
