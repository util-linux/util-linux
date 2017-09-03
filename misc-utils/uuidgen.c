/*
 * gen_uuid.c --- generate a DCE-compatible uuid
 *
 * Copyright (C) 1999, Andreas Dilger and Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "uuid.h"
#include "nls.h"
#include "c.h"
#include "closestream.h"

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Create a new UUID value.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -r, --random        generate random-based uuid\n"), out);
	fputs(_(" -t, --time          generate time-based uuid\n"), out);
	fputs(_(" -n, --namespace ns  generate hash-based uuid in this namespace\n"), out);
	fputs(_(" -N, --name name     generate hash-based uuid from this name\n"), out);
	fputs(_(" -m, --md5           generate md5 hash\n"), out);
	fputs(_(" -s, --sha1          generate sha1 hash\n"), out);
	fputs(_(" -x, --hex           interpret name as hex string\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(18));
	printf(USAGE_MAN_TAIL("uuidgen(1)"));
	exit(EXIT_SUCCESS);
}

static char *unhex(const char *value, size_t *valuelen)
{
	char c, *value2;
	unsigned n, x;

	if (*valuelen % 2 != 0) {
badstring:
		fprintf(stderr, "%s: not a valid hex string\n", program_invocation_short_name);
		errtryhelp(EXIT_FAILURE);
	}

	value2 = malloc(*valuelen / 2 + 1);

	for (x = n = 0; n < *valuelen; n++) {
		c = value[n];
		if ('0' <= c && c <= '9')
			x += c - '0';
		else if (('a' <= c && c <= 'f') || ('A' <= c && c <= 'F'))
			x += (c - 'A' + 10) & 0xf;
		else
			goto badstring;

		if (n % 2 == 0)
			x *= 16;
		else {
			value2[n / 2] = x;
			x = 0;
		}
	}
	value2[n / 2] = '\0';

	*valuelen = (n / 2);

	return value2;
}

int
main (int argc, char *argv[])
{
	int    c;
	int    do_type = 0, is_hex = 0;
	char   str[UUID_STR_LEN];
	char   *namespace = NULL, *name = NULL;
	size_t namelen = 0;
	uuid_t ns, uu;

	static const struct option longopts[] = {
		{"random", no_argument, NULL, 'r'},
		{"time", no_argument, NULL, 't'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{"namespace", required_argument, NULL, 'n'},
		{"name", required_argument, NULL, 'N'},
		{"md5", no_argument, NULL, 'm'},
		{"sha1", no_argument, NULL, 's'},
		{"hex", no_argument, NULL, 'x'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "rtVhn:N:msx", longopts, NULL)) != -1)
		switch (c) {
		case 't':
			do_type = UUID_TYPE_DCE_TIME;
			break;
		case 'r':
			do_type = UUID_TYPE_DCE_RANDOM;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'n':
			namespace = optarg;
			break;
		case 'N':
			name = optarg;
			break;
		case 'm':
			do_type = UUID_TYPE_DCE_MD5;
			break;
		case 's':
			do_type = UUID_TYPE_DCE_SHA1;
			break;
		case 'x':
			is_hex = 1;
			break;
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}

	if (namespace) {
		if (!name) {
			fprintf(stderr, "%s: --namespace requires --name argument\n", program_invocation_short_name);
			errtryhelp(EXIT_FAILURE);
		}
		if (do_type != UUID_TYPE_DCE_MD5 && do_type != UUID_TYPE_DCE_SHA1) {
			fprintf(stderr, "%s: --namespace requires --md5 or --sha1\n", program_invocation_short_name);
			errtryhelp(EXIT_FAILURE);
		}
	} else {
		if (name) {
			fprintf(stderr, "%s: --name requires --namespace argument\n", program_invocation_short_name);
			errtryhelp(EXIT_FAILURE);
		}
		if (do_type == UUID_TYPE_DCE_MD5 || do_type == UUID_TYPE_DCE_SHA1) {
			fprintf(stderr, "%s: --md5 or --sha1 require --namespace\n", program_invocation_short_name);
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (name) {
		namelen = strlen(name);
		if (is_hex)
			name = unhex(name, &namelen);
	}

	switch (do_type) {
	case UUID_TYPE_DCE_TIME:
		uuid_generate_time(uu);
		break;
	case UUID_TYPE_DCE_RANDOM:
		uuid_generate_random(uu);
		break;
	case UUID_TYPE_DCE_MD5:
	case UUID_TYPE_DCE_SHA1:
		if (namespace[0] == '@' && namespace[1] != '\0') {
			const uuid_t *uuidptr;

			uuidptr = uuid_get_template(&namespace[1]);
			if (uuidptr == NULL) {
				fprintf(stderr, "%s: unknown namespace alias '%s'\n", program_invocation_short_name, namespace);
				errtryhelp(EXIT_FAILURE);
			}
			memcpy(ns, *uuidptr, sizeof(ns));
		} else {
			if (uuid_parse(namespace, ns) != 0) {
				fprintf(stderr, "%s: invalid uuid for namespace '%s'\n", program_invocation_short_name, namespace);
				errtryhelp(EXIT_FAILURE);
			}
		}
		if (do_type == UUID_TYPE_DCE_MD5)
			uuid_generate_md5(uu, ns, name, namelen);
		else
			uuid_generate_sha1(uu, ns, name, namelen);
		break;
	default:
		uuid_generate(uu);
		break;
	}

	uuid_unparse(uu, str);

	printf("%s\n", str);

	if (is_hex)
		free(name);

	return EXIT_SUCCESS;
}
