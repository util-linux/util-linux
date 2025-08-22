/*
 * config_files.c instantiates functions defined and described in config_files.h
 */
#include <err.h>
#include <errno.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <stdbool.h>
#if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)
#include <dirent.h>
#endif
#include "config-files.h"
#include "list.h"
#include "xalloc.h"
#include "fileutils.h"

#define DEFAULT_ETC_SUBDIR "/etc"

/* Checking for main configuration file 
 * 
 * Returning absolute path or NULL if not found
 * The return value has to be freed by the caller.
 */
static char *main_config_file(const char *root,
		       const char *project,
		       const char *config_name,
		       const char *config_suffix)
{
	bool found = false;
	char *path = NULL;
	struct stat st;
	
	if (config_suffix) {
		xasprintf(&path, "%s/%s/%s.%s", root, project, config_name, config_suffix);
		if (stat(path, &st) == 0) {
			found = true;
		} else {
			free(path);
			path = NULL;
		}
	}
	if (!found) {
		/* trying filename without suffix */
		xasprintf(&path, "%s/%s/%s", root, project, config_name);
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
	struct file_element *file_element = xcalloc(1, sizeof(*file_element));

	INIT_LIST_HEAD(&file_element->file_list);

	if (filename != NULL)
		file_element->filename = xstrdup(filename);
	else
		file_element->filename = NULL;

	return file_element;
}

static int issuedir_filter(const struct dirent *d)
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

static int  read_dir( struct list_head *file_list,
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

	INIT_LIST_HEAD(file_list);

#if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)

	if (config_suffix) {
		xasprintf(&dirname, "%s/%s/%s.%s.d",
			  root, project, config_name, config_suffix);
		if (stat(dirname, &st) == 0) {
			found = true;
		} else {
			free(dirname);
			dirname = NULL;
		}
	}
	if (!found) {
		/* trying path without suffix */
		xasprintf(&dirname, "%s/%s/%s.d", root, project, config_name);
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

	nfiles = scandirat(dd, ".", &namelist, issuedir_filter, alphasort);
	if (nfiles <= 0) {
		free(dirname);
		return 0;
	}

	for (i = 0; i < nfiles; i++) {
		struct dirent *d = namelist[i];
		size_t namesz = strlen(d->d_name);
		if (strlen(config_suffix)>0 &&
		    (!namesz || namesz < strlen(config_suffix) + 1 ||
		     strcmp(d->d_name + (namesz - strlen(config_suffix)), config_suffix) != 0)) {
			/* filename does not have requested suffix */
			continue;
		}

		xasprintf(&filename, "%s/%s", dirname, d->d_name);
		entry = new_list_entry(filename);
		list_add_tail(&entry->file_list, file_list);
		counter++;
	}

	for (i = 0; i < nfiles; i++)
		free(namelist[i]);
	free(namelist);
        free(dirname);
	close(dd);
	return counter;
#else /* defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT) */
	return 0;
#endif
}

static void free_element(struct file_element *element)
{
	free(element->filename);
}


size_t config_file_list( struct list_head *file_list,
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
	
	INIT_LIST_HEAD(file_list);

	if (!config_name){
		warnx("config_name must be a valid value");
		return -1;
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
 	filename = main_config_file(etc_subdir, project, config_name, config_suffix);
	if (filename == NULL)
		filename = main_config_file(_PATH_RUNSTATEDIR, project, config_name, config_suffix);
	if (filename == NULL)
		filename = main_config_file(usr_subdir, project, config_name, config_suffix);
	if (filename != NULL) {
		add_element = new_list_entry(filename);
		list_add_tail(&add_element->file_list, file_list);
	}

        read_dir(&etc_file_list,
		 project,
		 etc_subdir,
		 config_name,
		 config_suffix);
        read_dir(&usr_file_list,
		 project,
		 usr_subdir,
		 config_name,
		 config_suffix);

	list_for_each(etc_entry, &etc_file_list) {
		etc_element = list_entry(etc_entry, struct file_element, file_list);
		etc_basename = ul_basename(etc_element->filename);
		list_for_each(usr_entry, &usr_file_list) {
			usr_element = list_entry(usr_entry, struct file_element, file_list);
			usr_basename = ul_basename(usr_element->filename);
			if (strcmp(usr_basename, etc_basename) <= 0) {
				if (strcmp(usr_basename, etc_basename) < 0) {
					add_element = new_list_entry(usr_element->filename);
					list_add_tail(&add_element->file_list, file_list);
				}
				list_del(&usr_element->file_list);
			} else {
				break;
			}
		}
		add_element = new_list_entry(etc_element->filename);
		list_add_tail(&add_element->file_list, file_list);
	}

	/* taking the rest of /usr */
	list_for_each(usr_entry, &usr_file_list) {
		usr_element = list_entry(usr_entry, struct file_element, file_list);
		add_element = new_list_entry(usr_element->filename);
		list_add_tail(&add_element->file_list, file_list);
	}

	list_free(&etc_file_list, struct file_element,  file_list, free_element);
	list_free(&usr_file_list, struct file_element,  file_list, free_element);

	return list_count_entries(file_list);
}
