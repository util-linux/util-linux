/*
 * configs_file.c instantiates functions defined and described in configs_file.h
 */
#include <err.h>
#include <errno.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)
#include <dirent.h>
#endif
#include "configs.h"
#include "list.h"
#include "fileutils.h"

#define DEFAULT_ETC_SUBDIR "/etc"

struct file_element {
	struct list_head file_list;
	char *filename;
};

/* Checking for main configuration file 
 * 
 * Returning absolute path or NULL if not found
 * The return value has to be freed by the caller.
 */
static char *main_configs(const char *root,
			  const char *project,
			  const char *config_name,
			  const char *config_suffix)
{
	bool found = false;
	char *path = NULL;
	struct stat st;
	
	if (config_suffix) {
		if (asprintf(&path, "%s/%s/%s.%s", root, project, config_name, config_suffix) < 0)
			return NULL;
		if (stat(path, &st) == 0) {
			found = true;
		} else {
			free(path);
			path = NULL;
		}
	}
	if (!found) {
		/* trying filename without suffix */
		if (asprintf(&path, "%s/%s/%s", root, project, config_name) < 0)
			return NULL;
		if (stat(path, &st) != 0) {
			/* not found */
			free(path);
			path = NULL;
		}
	}
	return path;
}

static struct file_element *new_list_entry(const char *filename)
{
	struct file_element *file_element = calloc(1, sizeof(*file_element));

	if (file_element == NULL)
		return NULL;

	INIT_LIST_HEAD(&file_element->file_list);

	if (filename != NULL) {
		file_element->filename = strdup(filename);
		if (file_element->filename == NULL) {
			free(file_element);
			return NULL;
		}
	} else {
		file_element->filename = NULL;
	}

	return file_element;
}

#if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)

static int filter(const struct dirent *d)
{
#ifdef _DIRENT_HAVE_D_TYPE
	if (d->d_type != DT_UNKNOWN && d->d_type != DT_REG &&
	    d->d_type != DT_LNK)
		return 0;
#endif
	if (*d->d_name == '.')
		return 0;

	/* Accept this */
	return 1;
}

static int read_dir(struct list_head *file_list,
		    const char *project,
		    const char *root,
		    const char *config_name,
		    const char *config_suffix)
{
	bool found = false;
	char *dirname = NULL;
	char *filename = NULL;
	struct stat st;
	int dd, nfiles, i;
	int counter = 0;
	struct dirent **namelist = NULL;
	struct file_element *entry = NULL;

	if (config_suffix) {
		if (asprintf(&dirname, "%s/%s/%s.%s.d",
			     root, project, config_name, config_suffix) < 0)
			return -ENOMEM;
		if (stat(dirname, &st) == 0) {
			found = true;
		} else {
			free(dirname);
			dirname = NULL;
		}
	}
	if (!found) {
		/* trying path without suffix */
		if (asprintf(&dirname, "%s/%s/%s.d", root, project, config_name) < 0)
			return -ENOMEM;
		if (stat(dirname, &st) != 0) {
			/* not found */
			free(dirname);
			dirname = NULL;
		}
	}

	if (dirname==NULL)
		return 0;

	dd = open(dirname, O_RDONLY|O_CLOEXEC|O_DIRECTORY);
	if (dd < 0) {
		free(dirname);
		return 0;
	}

	nfiles = scandirat(dd, ".", &namelist, filter, alphasort);
	if (nfiles <= 0) {
		free(dirname);
		return 0;
	}

	for (i = 0; i < nfiles; i++) {
		struct dirent *d = namelist[i];
		size_t namesz = strlen(d->d_name);
		if (config_suffix && strlen(config_suffix) > 0 &&
		    (!namesz || namesz < strlen(config_suffix) + 1 ||
		     strcmp(d->d_name + (namesz - strlen(config_suffix)), config_suffix) != 0)) {
			/* filename does not have requested suffix */
			continue;
		}

		if (asprintf(&filename, "%s/%s", dirname, d->d_name) < 0) {
			counter = -ENOMEM;
			break;
		}
		entry = new_list_entry(filename);
		free(filename);
		if (entry == NULL) {
			counter = -ENOMEM;
			break;
		}

		list_add_tail(&entry->file_list, file_list);
		counter++;
	}

	for (i = 0; i < nfiles; i++)
		free(namelist[i]);
	free(namelist);
	free(dirname);
	close(dd);
	return counter;
}

#endif

static void free_element(struct file_element *element)
{
	free(element->filename);
	free(element);
}


int ul_configs_file_list(struct list_head *file_list,
			 const char *project,
			 const char *etc_subdir,
			 const char *usr_subdir,
			 const char *config_name,
			 const char *config_suffix)
{
	char *filename = NULL, *usr_basename = NULL, *etc_basename = NULL;
	struct list_head etc_file_list;
	struct list_head usr_file_list;
	struct list_head *etc_entry = NULL, *usr_entry = NULL;
	struct file_element *add_element = NULL, *usr_element = NULL, *etc_element = NULL;
	int counter = 0;
	
	INIT_LIST_HEAD(file_list);

	if (!config_name){
		return -ENOTEMPTY;
	}

	/* Default is /etc */
	if (!etc_subdir)
		etc_subdir = DEFAULT_ETC_SUBDIR;

	if (!usr_subdir)
		usr_subdir = "";

	if (!project)
		project = "";

	/* Evaluating first "main" file which has to be parsed */
	/* in the following order : /etc /run /usr             */
	filename = main_configs(etc_subdir, project, config_name, config_suffix);
	if (filename == NULL)
		filename = main_configs(_PATH_RUNSTATEDIR, project, config_name, config_suffix);
	if (filename == NULL)
		filename = main_configs(usr_subdir, project, config_name, config_suffix);
	if (filename != NULL) {
		add_element = new_list_entry(filename);
		free(filename);
		if (add_element == NULL)
			return -ENOMEM;
		list_add_tail(&add_element->file_list, file_list);
		counter++;
	}

	INIT_LIST_HEAD(&etc_file_list);
	INIT_LIST_HEAD(&usr_file_list);

#if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)
	int ret_usr = 0, ret_etc = 0;
        ret_etc = read_dir(&etc_file_list,
			   project,
			   etc_subdir,
			   config_name,
			   config_suffix);
	ret_usr = read_dir(&usr_file_list,
			   project,
			   usr_subdir,
			   config_name,
			   config_suffix);
	if (ret_etc == -ENOMEM || ret_usr == -ENOMEM) {
		counter = -ENOMEM;
		goto finish;
	}
#endif

	list_for_each(etc_entry, &etc_file_list) {
		etc_element = list_entry(etc_entry, struct file_element, file_list);
		etc_basename = ul_basename(etc_element->filename);
		list_for_each(usr_entry, &usr_file_list) {
			usr_element = list_entry(usr_entry, struct file_element, file_list);
			usr_basename = ul_basename(usr_element->filename);
			if (strcmp(usr_basename, etc_basename) <= 0) {
				if (strcmp(usr_basename, etc_basename) < 0) {
					add_element = new_list_entry(usr_element->filename);
					if (add_element == NULL) {
						counter = -ENOMEM;
						goto finish;
					}
					list_add_tail(&add_element->file_list, file_list);
					counter++;
				}
				list_del(&usr_element->file_list);
			} else {
				break;
			}
		}
		add_element = new_list_entry(etc_element->filename);
		if (add_element == NULL) {
			counter = -ENOMEM;
			goto finish;
		}
		list_add_tail(&add_element->file_list, file_list);
		counter++;
	}

	/* taking the rest of /usr */
	list_for_each(usr_entry, &usr_file_list) {
		usr_element = list_entry(usr_entry, struct file_element, file_list);
		add_element = new_list_entry(usr_element->filename);
		if (add_element == NULL) {
			counter = -ENOMEM;
			goto finish;
		}
		list_add_tail(&add_element->file_list, file_list);
		counter++;
	}

finish:
	ul_configs_free_list(&etc_file_list);
	ul_configs_free_list(&usr_file_list);

	return counter;
}

void ul_configs_free_list(struct list_head *file_list)
{
	list_free(file_list, struct file_element,  file_list, free_element);
}

bool ul_configs_next_filename(struct list_head *file_list,
			      struct list_head **current_entry,
			      char **name)
{
	struct file_element *element = NULL;

	if (*current_entry == file_list)
		return false;

	if (*current_entry == NULL)
		*current_entry = file_list;
	element = list_entry(*current_entry, struct file_element, file_list);
	*name = element->filename;
	*current_entry = (*current_entry)->next;

	return true;
}
