/*
 * blkpr.c -- persistent reservations on a block device.
 *
 * Copyright (C) 2021 zhenwei pi <pizhenwei@bytedance.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This program uses IOC_PR_XXX ioctl to do persistent reservations
 * operation on a block device if the device supports it.
 */
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <locale.h>
#include <sys/ioctl.h>
#include <linux/pr.h>

#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "strutils.h"
#include "xalloc.h"

struct type_string {
	int type;
	char *str;
};

/* This array should keep align with enum pr_type of linux/types.h */
static struct type_string pr_type[] = {
	{PR_WRITE_EXCLUSIVE,           "write-exclusive"},
	{PR_EXCLUSIVE_ACCESS,          "exclusive-access"},
	{PR_WRITE_EXCLUSIVE_REG_ONLY,  "write-exclusive-reg-only"},
	{PR_EXCLUSIVE_ACCESS_REG_ONLY, "exclusive-access-reg-only"},
	{PR_WRITE_EXCLUSIVE_ALL_REGS,  "write-exclusive-all-regs"},
	{PR_EXCLUSIVE_ACCESS_ALL_REGS, "exclusive-access-all-regs"}
};

static struct type_string pr_operation[] = {
	{IOC_PR_REGISTER,      "register"},
	{IOC_PR_RESERVE,       "reserve"},
	{IOC_PR_RELEASE,       "release"},
	{IOC_PR_PREEMPT,       "preempt"},
	{IOC_PR_PREEMPT_ABORT, "preempt-abort"},
	{IOC_PR_CLEAR,         "clear"},
};

static struct type_string pr_flag[] = {
	{PR_FL_IGNORE_KEY, "ignore-key"}
};

static void print_type(FILE *out, struct type_string *ts, size_t nmem)
{
	size_t i;

	for (i = 0; i < nmem; i++) {
		fprintf(out, "%s", ts[i].str);
		fputs(i + 2 < nmem ? ", " :
		      i + 1 < nmem ? _(", and ") : "\n", out);
	}
}


static int parse_type_by_str(struct type_string *ts, int nmem, char *pattern)
{
	int i;

	for (i = 0; i < nmem; i++) {
		if (!strcmp(ts[i].str, pattern))
			return ts[i].type;
	}

	return -1;
}


#define PRINT_SUPPORTED(XX) \
	static void print_##XX(FILE *out) \
	{ print_type(out, XX, ARRAY_SIZE(XX)); }

#define PARSE(XX) \
	static int parse_##XX(char *pattern) \
	{ return parse_type_by_str(XX, ARRAY_SIZE(XX), pattern); }

PRINT_SUPPORTED(pr_type)
PRINT_SUPPORTED(pr_operation)
PRINT_SUPPORTED(pr_flag)

PARSE(pr_type)
PARSE(pr_operation)
PARSE(pr_flag)

static int do_pr(char *path, uint64_t key, uint64_t oldkey, int op, int type, int flag)
{
	struct pr_registration pr_reg;
	struct pr_reservation pr_res;
	struct pr_preempt pr_prt;
	struct pr_clear pr_clr;
	int fd, ret;

	fd = open(path, O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);

	switch (op) {
	case IOC_PR_REGISTER:
		pr_reg.old_key = oldkey;
		pr_reg.new_key = key;
		pr_reg.flags = flag;
		ret = ioctl(fd, op, &pr_reg);
		break;
	case IOC_PR_RESERVE:
	case IOC_PR_RELEASE:
		pr_res.key = key;
		pr_res.type = type;
		pr_res.flags = flag;
		ret = ioctl(fd, op, &pr_res);
		break;
	case IOC_PR_PREEMPT:
	case IOC_PR_PREEMPT_ABORT:
		pr_prt.old_key = oldkey;
		pr_prt.new_key = key;
		pr_prt.type = type;
		pr_prt.flags = flag;
		ret = ioctl(fd, op, &pr_prt);
		break;
	case IOC_PR_CLEAR:
		pr_clr.key = key;
		pr_clr.flags = flag;
		ret = ioctl(fd, op, &pr_clr);
		break;
	default:
		errno = EINVAL;
		err(EXIT_FAILURE, _("unknown operation"));
	}

	close(fd);
	if (ret < 0)
		err(EXIT_FAILURE, _("pr ioctl failed"));
	if (ret > 0)
		errx(EXIT_FAILURE, _("error code 0x%x, for more detailed information see specification of device model."), ret);

	return ret;
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <device>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Persistent reservations on a device.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -o, --operation <oper>   operation of persistent reservations\n"), out);
	fputs(_(" -k, --key <num>          key to operate\n"), out);
	fputs(_(" -K, --oldkey <num>       old key to operate\n"), out);
	fputs(_(" -f, --flag <flag>        operation flag\n"), out);
	fputs(_(" -t, --type <type>        operation type\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(26));

	fputs(USAGE_ARGUMENTS, out);

	fputs(_(" <oper> is an operation, available operations:\n"), out);
	fputs("        ", out);
	print_pr_operation(out);

	fputs(_(" <flag> is an operation flag, available flags:\n"), out);
	fputs("        ", out);
	print_pr_flag(out);

	fputs(_(" <type> is an operation type, available types:\n"), out);
	fputs("        ", out);
	print_pr_type(out);

	printf(USAGE_MAN_TAIL("blkpr(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	char c;
	char *path;
	uint64_t key = 0, oldkey = 0;
	int operation = -1, type = -1, flag = 0;

	static const struct option longopts[] = {
	    { "help",            no_argument,       NULL, 'h' },
	    { "version",         no_argument,       NULL, 'V' },
	    { "operation",       required_argument, NULL, 'o' },
	    { "key",             required_argument, NULL, 'k' },
	    { "oldkey",          required_argument, NULL, 'K' },
	    { "flag",            required_argument, NULL, 'f' },
	    { "type",            required_argument, NULL, 't' },
	    { NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	errno = EINVAL;
	while ((c = getopt_long(argc, argv, "hVo:k:K:f:t:", longopts, NULL)) != -1) {
		switch(c) {
		case 'k':
			key = strtosize_or_err(optarg,
					_("failed to parse key"));
			break;
		case 'K':
			oldkey = strtosize_or_err(optarg,
					_("failed to parse old key"));
			break;
		case 'o':
			operation = parse_pr_operation(optarg);
			if (operation < 0)
				err(EXIT_FAILURE, _("unknown operation"));
			break;
		case 't':
			type = parse_pr_type(optarg);
			if (type < 0)
				err(EXIT_FAILURE, _("unknown type"));
			break;
		case 'f':
			flag = parse_pr_flag(optarg);
			if (flag < 0)
				err(EXIT_FAILURE, _("unknown flag"));
			break;

		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (optind == argc)
		errx(EXIT_FAILURE, _("no device specified"));

	path = argv[optind++];
	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		errtryhelp(EXIT_FAILURE);
	}

	do_pr(path, key, oldkey, operation, type, flag);

	return EXIT_SUCCESS;
}
