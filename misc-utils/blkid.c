/*
 * blkid.c - User command-line interface for libblkid
 *
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern int getopt(int argc, char * const argv[], const char *optstring);
extern char *optarg;
extern int optind;
#endif

#define OUTPUT_VALUE_ONLY	0x0001
#define OUTPUT_DEVICE_ONLY	0x0002
#define OUTPUT_PRETTY_LIST	0x0004
#define OUTPUT_UDEV_LIST	0x0008

#include <blkid.h>

#include "ismounted.h"

const char *progname = "blkid";

static void print_version(FILE *out)
{
	fprintf(out, "%s from %s (libblkid %s, %s)\n",
		progname, PACKAGE_STRING, LIBBLKID_VERSION, LIBBLKID_DATE);
}

static void usage(int error)
{
	FILE *out = error ? stderr : stdout;

	print_version(out);
	fprintf(out,
		"Usage:\n"
		"  %1$s -L <label> | -U <uuid>\n\n"
		"  %1$s [-c <file>] [-ghlLv] [-o format] [-s <tag>] \n"
		"        [-t <token>] [-w <file>] [dev ...]\n\n"
		"  %1$s -p [-O <offset>] [-S <size>] [-o format] <dev> [dev ...]\n\n"
		"Options:\n"
		"  -c <file>   cache file (default: /etc/blkid.tab, /dev/null = none)\n"
		"  -h          print this usage message and exit\n"
		"  -g          garbage collect the blkid cache\n"
		"  -o <format> output format; can be one of:\n"
		"              value, device, list, udev or full; (default: full)\n"
		"  -s <tag>    show specified tag(s) (default show all tags)\n"
		"  -t <token>  find device with a specific token (NAME=value pair)\n"
		"  -l          lookup the the first device with arguments specified by -t\n"
		"  -L <label>  convert LABEL to device name\n"
		"  -U <uuid>   convert UUID to device name\n"
		"  -v          print version and exit\n"
		"  -w <file>   write cache to different file (/dev/null = no write)\n"
		"  <dev>       specify device(s) to probe (default: all devices)\n\n"
		"Low-level probing options:\n"
		"  -p          switch to low-level mode (bypass cache)\n"
		"  -S <bytes>  overwrite device size\n"
		"  -O <bytes>  probe at the given offset\n"
		"  -u <list>   filter by \"usage\" (e.g. -u filesystem,raid)\n"
		"\n",
				progname);

	exit(error);
}

/*
 * This function does "safe" printing.  It will convert non-printable
 * ASCII characters using '^' and M- notation.
 */
static void safe_print(const char *cp, int len)
{
	unsigned char	ch;

	if (len < 0)
		len = strlen(cp);

	while (len--) {
		ch = *cp++;
		if (ch > 128) {
			fputs("M-", stdout);
			ch -= 128;
		}
		if ((ch < 32) || (ch == 0x7f)) {
			fputc('^', stdout);
			ch ^= 0x40; /* ^@, ^A, ^B; ^? for DEL */
		}
		fputc(ch, stdout);
	}
}

static int get_terminal_width(void)
{
#ifdef TIOCGSIZE
	struct ttysize	t_win;
#endif
#ifdef TIOCGWINSZ
	struct winsize	w_win;
#endif
        const char	*cp;

#ifdef TIOCGSIZE
	if (ioctl (0, TIOCGSIZE, &t_win) == 0)
		return (t_win.ts_cols);
#endif
#ifdef TIOCGWINSZ
	if (ioctl (0, TIOCGWINSZ, &w_win) == 0)
		return (w_win.ws_col);
#endif
        cp = getenv("COLUMNS");
	if (cp)
		return strtol(cp, NULL, 10);
	return 80;
}

static int pretty_print_word(const char *str, int max_len,
			     int left_len, int overflow_nl)
{
	int len = strlen(str) + left_len;
	int ret = 0;

	fputs(str, stdout);
	if (overflow_nl && len > max_len) {
		fputc('\n', stdout);
		len = 0;
	} else if (len > max_len)
		ret = len - max_len;
	do
		fputc(' ', stdout);
	while (len++ < max_len);
	return ret;
}

static void pretty_print_line(const char *device, const char *fs_type,
			      const char *label, const char *mtpt,
			      const char *uuid)
{
	static int device_len = 10, fs_type_len = 7;
	static int label_len = 8, mtpt_len = 14;
	static int term_width = -1;
	int len, w;

	if (term_width < 0)
		term_width = get_terminal_width();

	if (term_width > 80) {
		term_width -= 80;
		w = term_width / 10;
		if (w > 8)
			w = 8;
		term_width -= 2*w;
		label_len += w;
		fs_type_len += w;
		w = term_width/2;
		device_len += w;
		mtpt_len +=w;
	}

	len = pretty_print_word(device, device_len, 0, 1);
	len = pretty_print_word(fs_type, fs_type_len, len, 0);
	len = pretty_print_word(label, label_len, len, 0);
	len = pretty_print_word(mtpt, mtpt_len, len, 0);
	fputs(uuid, stdout);
	fputc('\n', stdout);
}

static void pretty_print_dev(blkid_dev dev)
{
	blkid_tag_iterate	iter;
	const char		*type, *value, *devname;
	const char		*uuid = "", *fs_type = "", *label = "";
	int			len, mount_flags;
	char			mtpt[80];
	int			retval;

	if (dev == NULL) {
		pretty_print_line("device", "fs_type", "label",
				  "mount point", "UUID");
		for (len=get_terminal_width()-1; len > 0; len--)
			fputc('-', stdout);
		fputc('\n', stdout);
		return;
	}

	devname = blkid_dev_devname(dev);
	if (access(devname, F_OK))
		return;

	/* Get the uuid, label, type */
	iter = blkid_tag_iterate_begin(dev);
	while (blkid_tag_next(iter, &type, &value) == 0) {
		if (!strcmp(type, "UUID"))
			uuid = value;
		if (!strcmp(type, "TYPE"))
			fs_type = value;
		if (!strcmp(type, "LABEL"))
			label = value;
	}
	blkid_tag_iterate_end(iter);

	/* Get the mount point */
	mtpt[0] = 0;
	retval = check_mount_point(devname, &mount_flags, mtpt, sizeof(mtpt));
	if (retval == 0) {
		if (mount_flags & MF_MOUNTED) {
			if (!mtpt[0])
				strcpy(mtpt, "(mounted, mtpt unknown)");
		} else if (mount_flags & MF_BUSY)
			strcpy(mtpt, "(in use)");
		else
			strcpy(mtpt, "(not mounted)");
	}

	pretty_print_line(devname, fs_type, label, mtpt, uuid);
}

static void print_udev_format(const char *name, const char *value, size_t sz)
{
	char enc[265], safe[256];

	*safe = *enc = '\0';

	if (!strcmp(name, "TYPE") || !strcmp(name, "VERSION")) {
		blkid_encode_string(value, enc, sizeof(enc));
		printf("ID_FS_%s=%s\n", name, enc);

	} else if (!strcmp(name, "UUID") ||
		 !strcmp(name, "LABEL") ||
		 !strcmp(name, "UUID_SUB")) {

		blkid_safe_string(value, safe, sizeof(safe));
		printf("ID_FS_%s=%s\n", name, safe);

		blkid_encode_string(value, enc, sizeof(enc));
		printf("ID_FS_%s_ENC=%s\n", name, enc);

	} else if (!strcmp(name, "PTTYPE"))
		printf("ID_PART_TABLE_TYPE=%s\n", value);

	/* TODO:  ID_PART_ENTRY_{UUID,NAME,FLAG} */

	else
		printf("ID_FS_%s=%s\n", name, value);
}

static int has_item(char *ary[], const char *item)
{
	char **p;

	for (p = ary; *p != NULL; p++)
		if (!strcmp(item, *p))
			return 1;
	return 0;
}

static void print_value(int output, int num, const char *devname,
			const char *value, const char *name, size_t valsz)
{
	if (output & OUTPUT_VALUE_ONLY) {
		fputs(value, stdout);
		fputc('\n', stdout);

	} else if (output & OUTPUT_UDEV_LIST) {
		print_udev_format(name, value, valsz);

	} else {
		if (num == 1 && devname)
			printf("%s: ", devname);
		fputs(name, stdout);
		fputs("=\"", stdout);
		safe_print(value, valsz);
		fputs("\" ", stdout);
	}
}

static void print_tags(blkid_dev dev, char *show[], int output)
{
	blkid_tag_iterate	iter;
	const char		*type, *value, *devname;
	int			num = 1;

	if (!dev)
		return;

	if (output & OUTPUT_PRETTY_LIST) {
		pretty_print_dev(dev);
		return;
	}

	devname = blkid_dev_devname(dev);

	if (output & OUTPUT_DEVICE_ONLY) {
		printf("%s\n", devname);
		return;
	}

	iter = blkid_tag_iterate_begin(dev);
	while (blkid_tag_next(iter, &type, &value) == 0) {
		if (show[0] && !has_item(show, type))
			continue;
		print_value(output, num++, devname, value, type, strlen(value));
	}
	blkid_tag_iterate_end(iter);

	if (num > 1 && !(output & (OUTPUT_VALUE_ONLY | OUTPUT_UDEV_LIST)))
		printf("\n");
}


static int append_str(char **res, size_t *sz, const char *a, const char *b)
{
	char *str = *res;
	size_t asz = a ? strlen(a) : 0;
	size_t bsz = b ? strlen(b) : 0;
	size_t len = *sz + asz + bsz;

	if (!len)
		return -1;

	str = realloc(str, len + 1);
	if (!str) {
		free(*res);
		return -1;
	}
	*res = str;
	str += *sz;

	if (a) {
		memcpy(str, a, asz);
		str += asz;
	}
	if (b) {
		memcpy(str, b, bsz);
		str += bsz;
	}
	*str = '\0';
	*sz = len;
	return 0;
}

/*
 * Compose and print ID_FS_AMBIVALENT for udev
 */
static int print_udev_ambivalent(blkid_probe pr)
{
	char *val = NULL;
	size_t valsz = 0;
	int count = 0, rc = -1;

	while (!blkid_do_probe(pr)) {
		const char *usage = NULL, *type = NULL, *version = NULL;
		char enc[256];

		blkid_probe_lookup_value(pr, "USAGE", &usage, NULL);
		blkid_probe_lookup_value(pr, "TYPE", &type, NULL);
		blkid_probe_lookup_value(pr, "VERSION", &version, NULL);

		if (!usage || !type)
			continue;

		blkid_encode_string(usage, enc, sizeof(enc));
		if (append_str(&val, &valsz, enc, ":"))
			goto done;

		blkid_encode_string(type, enc, sizeof(enc));
		if (append_str(&val, &valsz, enc, version ? ":" : " "))
			goto done;

		if (version) {
			blkid_encode_string(version, enc, sizeof(enc));
			if (append_str(&val, &valsz, enc, " "))
				goto done;
		}
		count++;
	}

	if (count > 1) {
		*(val + valsz - 1) = '\0';		/* rem tailing whitespace */
		printf("ID_FS_AMBIVALEN=%s\n", val);
		rc = 0;
	}
done:
	free(val);
	return rc;
}

static int lowprobe_device(blkid_probe pr, const char *devname,	char *show[],
			int output, blkid_loff_t offset, blkid_loff_t size)
{
	const char *data;
	const char *name;
	int nvals = 0, n, num = 1;
	size_t len;
	int fd;
	int rc = 0;
	struct stat st;

	fd = open(devname, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "error: %s: %s\n", devname, strerror(errno));
		return 2;
	}
	if (blkid_probe_set_device(pr, fd, offset, size))
		goto done;

	if (fstat(fd, &st))
		goto done;
	/*
	 * partitions probing
	 */
	blkid_probe_enable_superblocks(pr, 0);	/* enabled by default ;-( */

	blkid_probe_enable_partitions(pr, 1);
	rc = blkid_do_fullprobe(pr);
	blkid_probe_enable_partitions(pr, 0);

	if (rc < 0)
		goto done;	/* -1 = error, 1 = nothing, 0 = succes */

	/*
	 * Don't probe for FS/RAIDs on small devices
	 */
	if (rc || S_ISCHR(st.st_mode) ||
	    blkid_probe_get_size(pr) > 1024 * 1440) {
		/*
		 * filesystems/RAIDs probing
		 */
		blkid_probe_enable_superblocks(pr, 1);

		rc = blkid_do_safeprobe(pr);
		if (rc < 0)
			goto done;
	}

	nvals = blkid_probe_numof_values(pr);

	if (output & OUTPUT_DEVICE_ONLY) {
		printf("%s\n", devname);
		goto done;
	}

	for (n = 0; n < nvals; n++) {
		if (blkid_probe_get_value(pr, n, &name, &data, &len))
			continue;
		if (show[0] && !has_item(show, name))
			continue;
		len = strnlen((char *) data, len);
		print_value(output, num++, devname, (char *) data, name, len);
	}

	if (nvals >= 1 && !(output & (OUTPUT_VALUE_ONLY | OUTPUT_UDEV_LIST)))
		printf("\n");
done:
	if (rc == -2) {
		if (output & OUTPUT_UDEV_LIST)
			print_udev_ambivalent(pr);
		else
			fprintf(stderr,
				"%s: ambivalent result (probably more "
				"filesystems on the device, use wipefs(8) "
				"to see more details)\n",
				devname);
	}
	close(fd);
	return !nvals ? 2 : 0;
}

/* converts comma separated list to BLKID_USAGE_* mask */
static int list_to_usage(const char *list, int *flag)
{
	int mask = 0;
	const char *word, *p = list;

	if (p && strncmp(p, "no", 2) == 0) {
		*flag = BLKID_FLTR_NOTIN;
		p += 2;
	}

	for (word = p; p && *p; p++) {
		if (*p == ',' || *(p + 1) == '\0') {
			if (!strncmp(word, "filesystem", 10))
				mask |= BLKID_USAGE_FILESYSTEM;
			else if (!strncmp(word, "raid", 4))
				mask |= BLKID_USAGE_RAID;
			else if (!strncmp(word, "crypto", 6))
				mask |= BLKID_USAGE_CRYPTO;
			else if (!strncmp(word, "other", 5))
				mask |= BLKID_USAGE_OTHER;
			else {
				fprintf(stderr, "unknown usage keyword '%*s'\n",
						(int) (p - word), word);
				exit(4);
			}
			word = p + 1;
		}
	}
	return mask;
}

int main(int argc, char **argv)
{
	blkid_cache cache = NULL;
	char *devices[128] = { NULL, };
	char *show[128] = { NULL, };
	char *search_type = NULL, *search_value = NULL;
	char *read = NULL;
	char *write = NULL;
	int fltr_usage = 0;
	int fltr_flag = BLKID_FLTR_ONLYIN;
	unsigned int numdev = 0, numtag = 0;
	int version = 0;
	int err = 4;
	unsigned int i;
	int output_format = 0;
	int lookup = 0, gc = 0, lowprobe = 0, eval = 0;
	int c;
	blkid_loff_t offset = 0, size = 0;

	show[0] = NULL;

	while ((c = getopt (argc, argv, "c:f:ghlL:o:O:ps:S:t:u:U:w:v")) != EOF)
		switch (c) {
		case 'c':
			if (optarg && !*optarg)
				read = NULL;
			else
				read = optarg;
			if (!write)
				write = read;
			break;
		case 'L':
			eval++;
			search_value = strdup(optarg);
			search_type = strdup("LABEL");
			break;
		case 'u':
			fltr_usage = list_to_usage(optarg, &fltr_flag);
			break;
		case 'U':
			eval++;
			search_value = strdup(optarg);
			search_type = strdup("UUID");
			break;
		case 'l':
			lookup++;
			break;
		case 'g':
			gc = 1;
			break;
		case 'o':
			if (!strcmp(optarg, "value"))
				output_format = OUTPUT_VALUE_ONLY;
			else if (!strcmp(optarg, "device"))
				output_format = OUTPUT_DEVICE_ONLY;
			else if (!strcmp(optarg, "list"))
				output_format = OUTPUT_PRETTY_LIST;
			else if (!strcmp(optarg, "udev"))
				output_format = OUTPUT_UDEV_LIST;
			else if (!strcmp(optarg, "full"))
				output_format = 0;
			else {
				fprintf(stderr, "Invalid output format %s. "
					"Choose from value,\n\t"
					"device, list, udev or full\n", optarg);
				exit(4);
			}
			break;
		case 'O':
			offset = strtoll(optarg, NULL, 10);
			break;
		case 'p':
			lowprobe++;
			break;
		case 's':
			if (numtag + 1 >= sizeof(show) / sizeof(*show)) {
				fprintf(stderr, "Too many tags specified\n");
				usage(err);
			}
			show[numtag++] = optarg;
			show[numtag] = NULL;
			break;
		case 'S':
			size = strtoll(optarg, NULL, 10);
			break;
		case 't':
			if (search_type) {
				fprintf(stderr, "Can only search for "
						"one NAME=value pair\n");
				usage(err);
			}
			if (blkid_parse_tag_string(optarg,
						   &search_type,
						   &search_value)) {
				fprintf(stderr, "-t needs NAME=value pair\n");
				usage(err);
			}
			break;
		case 'v':
			version = 1;
			break;
		case 'w':
			if (optarg && !*optarg)
				write = NULL;
			else
				write = optarg;
			break;
		case 'h':
			err = 0;
		default:
			usage(err);
		}

	while (optind < argc)
		devices[numdev++] = argv[optind++];

	if (version) {
		print_version(stdout);
		goto exit;
	}

	/* convert LABEL/UUID lookup to evaluate request */
	if (lookup && output_format == OUTPUT_DEVICE_ONLY && search_type &&
	    (!strcmp(search_type, "LABEL") || !strcmp(search_type, "UUID"))) {
		eval++;
		lookup = 0;
	}

	if (!lowprobe && !eval && blkid_get_cache(&cache, read) < 0)
		goto exit;

	if (gc) {
		blkid_gc_cache(cache);
		err = 0;
		goto exit;
	}
	err = 2;

	if (eval == 0 && output_format & OUTPUT_PRETTY_LIST) {
		if (lowprobe) {
			fprintf(stderr, "The low-level probing mode does not "
					"support 'list' output format\n");
			exit(4);
		}
		pretty_print_dev(NULL);
	}

	if (lowprobe) {
		/*
		 * Low-level API
		 */
		blkid_probe pr;

		if (!numdev) {
			fprintf(stderr, "The low-level probing mode "
					"requires a device\n");
			exit(4);
		}
		pr = blkid_new_probe();
		if (!pr)
			goto exit;

		blkid_probe_set_superblocks_flags(pr,
				BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID |
				BLKID_SUBLKS_TYPE | BLKID_SUBLKS_SECTYPE |
				BLKID_SUBLKS_USAGE | BLKID_SUBLKS_VERSION);

		if (fltr_usage &&
		    blkid_probe_filter_superblocks_usage(pr, fltr_flag, fltr_usage))
			goto exit;

		for (i = 0; i < numdev; i++)
			err = lowprobe_device(pr, devices[i], show,
					output_format, offset, size);
		blkid_free_probe(pr);
	} else if (eval) {
		/*
		 * Evaluate API
		 */
		char *res = blkid_evaluate_tag(search_type, search_value, NULL);
		if (res) {
			err = 0;
			printf("%s\n", res);
		}
	} else if (lookup) {
		/*
		 * Classic (cache based) API
		 */
		blkid_dev dev;

		if (!search_type) {
			fprintf(stderr, "The lookup option requires a "
				"search type specified using -t\n");
			exit(4);
		}
		/* Load any additional devices not in the cache */
		for (i = 0; i < numdev; i++)
			blkid_get_dev(cache, devices[i], BLKID_DEV_NORMAL);

		if ((dev = blkid_find_dev_with_tag(cache, search_type,
						   search_value))) {
			print_tags(dev, show, output_format);
			err = 0;
		}
	/* If we didn't specify a single device, show all available devices */
	} else if (!numdev) {
		blkid_dev_iterate	iter;
		blkid_dev		dev;

		blkid_probe_all(cache);

		iter = blkid_dev_iterate_begin(cache);
		blkid_dev_set_search(iter, search_type, search_value);
		while (blkid_dev_next(iter, &dev) == 0) {
			dev = blkid_verify(cache, dev);
			if (!dev)
				continue;
			print_tags(dev, show, output_format);
			err = 0;
		}
		blkid_dev_iterate_end(iter);
	/* Add all specified devices to cache (optionally display tags) */
	} else for (i = 0; i < numdev; i++) {
		blkid_dev dev = blkid_get_dev(cache, devices[i],
						  BLKID_DEV_NORMAL);

		if (dev) {
			if (search_type &&
			    !blkid_dev_has_tag(dev, search_type,
					       search_value))
				continue;
			print_tags(dev, show, output_format);
			err = 0;
		}
	}

exit:
	free(search_type);
	free(search_value);
	if (!lowprobe && !eval)
		blkid_put_cache(cache);
	return err;
}
