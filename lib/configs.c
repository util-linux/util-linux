/*
 * configs_file.c instantiates functions defined and described in configs_file.h
 */
#include <err.h>
#include <errno.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <stdarg.h>
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

struct file_element {
	struct list_head file_list;
	char *filename;
};

static __attribute__ ((__format__ (__printf__, 2, 0)))
	char *config_mk_path(int types, const char *fmt, ...)
{
	va_list ap;
	char *path = NULL;
	int rc;

	errno = 0;

	va_start(ap, fmt);
	rc = vasprintf(&path, fmt, ap);
	va_end(ap);

	if (rc > 0 && types) {
		struct stat st;

		if (stat(path, &st) != 0 ||
		    !((st.st_mode & S_IFMT) & types))
			goto fail;

	}

	return path;
fail:
	free(path);
	return NULL;
}

/* Checking for main configuration file
 *
 * Returning absolute path or NULL if not found The return value has to be
 * freed by the caller.
 */
static char *main_configs(const char *root,
			  const char *project,
			  const char *config_name,
			  const char *config_suffix)
{
	char *path = NULL;

	if (config_suffix)
		path = config_mk_path(S_IFREG, "%s/%s/%s.%s",
				root, project, config_name, config_suffix);
	if (!path)
		path = config_mk_path(S_IFREG, "%s/%s/%s",
				root, project, config_name);
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
	char *dirname = NULL;
	char *filename = NULL;
	int dd = -1, nfiles = 0, i;
	int counter = 0;
	struct dirent **namelist = NULL;
	struct file_element *entry = NULL;

	if (config_suffix)
		dirname = config_mk_path(S_IFDIR, "%s/%s/%s.%s.d",
				root, project, config_name, config_suffix);
	if (!dirname)
		dirname = config_mk_path(S_IFDIR, "%s/%s/%s.d",
				root, project, config_name);
	if (!dirname)
		return errno == ENOMEM ? -ENOMEM : 0;

	dd = open(dirname, O_RDONLY|O_CLOEXEC|O_DIRECTORY);
	if (dd < 0)
		goto finish;

	nfiles = scandirat(dd, ".", &namelist, filter, alphasort);
	if (nfiles <= 0)
		goto finish;

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

finish:
	for (i = 0; i < nfiles; i++)
		free(namelist[i]);
	free(namelist);
	free(dirname);
	if (dd >= 0)
		close(dd);
	return counter;
}

#endif /* HAVE_SCANDIRAT */

static void free_list_entry(struct file_element *element)
{
	free(element->filename);
	free(element);
}

int ul_configs_file_list(struct list_head *file_list,
			 const char *project,
			 const char *etc_subdir,
			 const char *run_subdir,
			 const char *usr_subdir,
			 const char *config_name,
			 const char *config_suffix)
{
	char *filename = NULL, *run_basename = NULL, *usr_basename = NULL,
		*etc_basename = NULL, *etc_run_basename = NULL;
	struct list_head etc_file_list;
	struct list_head run_file_list;
	struct list_head etc_run_file_list;
	struct list_head usr_file_list;
	struct list_head *etc_entry = NULL, *usr_entry = NULL, *run_entry = NULL, *etc_run_entry = NULL;
	struct file_element *add_element = NULL, *usr_element = NULL,
		*run_element = NULL, *etc_element = NULL, *etc_run_element = NULL;
	int counter = 0;

	INIT_LIST_HEAD(file_list);

	if (!config_name){
		return -ENOTEMPTY;
	}

	/* Default is /etc */
	if (!etc_subdir)
		etc_subdir = _PATH_SYSCONFDIR;
	if (!run_subdir)
		run_subdir = "";
	if (!usr_subdir)
		usr_subdir = "";

	if (!project)
		project = "";

	/* Evaluating first "main" file which has to be parsed */
	/* in the following order : /etc /run /usr             */
	filename = main_configs(etc_subdir, project, config_name, config_suffix);
	if (filename == NULL)
		filename = main_configs(run_subdir, project, config_name, config_suffix);
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
	INIT_LIST_HEAD(&run_file_list);
	INIT_LIST_HEAD(&etc_run_file_list);
	INIT_LIST_HEAD(&usr_file_list);

#if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)
	int ret_usr = 0, ret_etc = 0, ret_run = 0;
        ret_etc = read_dir(&etc_file_list,
			   project,
			   etc_subdir,
			   config_name,
			   config_suffix);
	ret_run = read_dir(&run_file_list,
			   project,
			   run_subdir,
			   config_name,
			   config_suffix);
	ret_usr = read_dir(&usr_file_list,
			   project,
			   usr_subdir,
			   config_name,
			   config_suffix);
	if (ret_etc == -ENOMEM || ret_usr == -ENOMEM || ret_run == -ENOMEM) {
		counter = -ENOMEM;
		goto finish;
	}
#endif

	/* Merging run and etc list in the correct order. Output: etc_run_list */
	list_for_each(etc_entry, &etc_file_list) {

		etc_element = list_entry(etc_entry, struct file_element, file_list);
		etc_basename = ul_basename(etc_element->filename);

		list_for_each(run_entry, &run_file_list) {

			run_element = list_entry(run_entry, struct file_element, file_list);
			run_basename = ul_basename(run_element->filename);

			if (strcmp(run_basename, etc_basename) <= 0) {
				if (strcmp(run_basename, etc_basename) < 0) {
					add_element = new_list_entry(run_element->filename);
					if (add_element == NULL) {
						counter = -ENOMEM;
						goto finish;
					}
					list_add_tail(&add_element->file_list, &etc_run_file_list);
				}
				list_del(&run_element->file_list);
			} else {
				break;
			}
		}
		add_element = new_list_entry(etc_element->filename);
		if (add_element == NULL) {
			counter = -ENOMEM;
			goto finish;
		}
		list_add_tail(&add_element->file_list, &etc_run_file_list);
	}

	/* taking the rest of /run */
	list_for_each(run_entry, &run_file_list) {
		run_element = list_entry(run_entry, struct file_element, file_list);
		add_element = new_list_entry(run_element->filename);
		if (add_element == NULL) {
			counter = -ENOMEM;
			goto finish;
		}
		list_add_tail(&add_element->file_list, &etc_run_file_list);
	}

	/* Merging etc_run list and var list in the correct order. Output: file_list
	   which will be returned. */
	list_for_each(etc_run_entry, &etc_run_file_list) {

		etc_run_element = list_entry(etc_run_entry, struct file_element, file_list);
		etc_run_basename = ul_basename(etc_run_element->filename);

		list_for_each(usr_entry, &usr_file_list) {

			usr_element = list_entry(usr_entry, struct file_element, file_list);
			usr_basename = ul_basename(usr_element->filename);

			if (strcmp(usr_basename, etc_run_basename) <= 0) {
				if (strcmp(usr_basename, etc_run_basename) < 0) {
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
		add_element = new_list_entry(etc_run_element->filename);
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
	ul_configs_free_list(&etc_run_file_list);
	ul_configs_free_list(&usr_file_list);

	return counter;
}

void ul_configs_free_list(struct list_head *file_list)
{
	list_free(file_list, struct file_element,  file_list, free_list_entry);
}

int ul_configs_next_filename(struct list_head *file_list,
			     struct list_head **current_entry,
			     char **name)
{
	struct file_element *element = NULL;

	if (list_empty(file_list) || *current_entry == file_list)
		return 1;

	if (*current_entry == NULL)
		*current_entry = file_list->next;

	element = list_entry(*current_entry, struct file_element, file_list);
	*name = element->filename;
	*current_entry = (*current_entry)->next;

	return 0;
}

#ifdef TEST_PROGRAM_CONFIGS
# include <getopt.h>

int main(int argc, char *argv[])
{
	struct list_head file_list;
	struct list_head *current = NULL;
	char *name = NULL;
	const char *etc_path = NULL;
	const char *run_path = NULL;
	const char *usr_path = NULL;
	const char *project = NULL;
	const char *config_name = NULL;
	const char *config_suffix = NULL;
	static const struct option longopts[] = {
		{ "etc", required_argument, NULL, 'e' },
		{ "run", required_argument, NULL, 'r' },
		{ "usr", required_argument, NULL, 'u' },
		{ "project", required_argument, NULL, 'p' },
		{ "name", required_argument, NULL, 'n' },
		{ "suffix", required_argument, NULL, 's' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	int ch, count;

	while ((ch = getopt_long(argc, argv, "e:r:u:p:n:s:h", longopts, NULL)) != -1) {
		switch (ch) {
		case 'e':
			etc_path = optarg;
			break;
		case 'r':
			run_path = optarg;
			break;
		case 'u':
			usr_path = optarg;
			break;
		case 'p':
			project = optarg;
			break;
		case 'n':
			config_name = optarg;
			break;
		case 's':
			config_suffix = optarg;
			break;
		case 'h':
			printf("usage: %s [options]\n"
				" -e, --etc <path>      path to /etc directory\n"
				" -r, --run <path>      path to /run directory\n"
				" -u, --usr <path>      path to /usr directory\n"
				" -p, --project <name>  project name subdirectory\n"
				" -n, --name <name>     config file base name\n"
				" -s, --suffix <suffix> config file suffix\n"
				" -h, --help            display this help\n",
				program_invocation_short_name);
			return EXIT_SUCCESS;
		default:
			return EXIT_FAILURE;
		}
	}

	if (!config_name) {
		fprintf(stderr, "config name is required (use --name)\n");
		return EXIT_FAILURE;
	}

	count = ul_configs_file_list(&file_list, project, etc_path, run_path, usr_path,
				     config_name, config_suffix);
	if (count < 0) {
		fprintf(stderr, "failed to get config file list: %d\n", count);
		return EXIT_FAILURE;
	}

	printf("Found %d configuration file(s):\n", count);
	while (ul_configs_next_filename(&file_list, &current, &name) == 0)
		printf("  %s\n", name);

	ul_configs_free_list(&file_list);
	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_CONFIGS */
