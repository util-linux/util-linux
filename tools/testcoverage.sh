#!/bin/bash

# This file is part of util-linux.
#
# This file is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This file is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# Copyright (C) 2025 Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
#
# This script uses a heuristic approach to determine an approx.
# test coverage of all util-linux tools. It does this by simply
# looking at all the test scripts for a given tool and compares
# the long options seen in them with all available ones for the
# concerned tool.
#
# If a tool is missing a test, the script will report an error
# message and the script will exit with a non-zero exit code at
# the end. The exit code reflects the amount of failed checks.

top_srcdir="${0%/*}/../"
if [ ! -d "${top_srcdir}" ]; then
	echo "directory '${top_srcdir}' not found" >&2
	exit 1
fi

if ! type mktemp >/dev/null 2>&1; then
	echo "missing dependency 'mktemp'"
	exit 1
else
	TMP_COVERAGE_REPORT_FILE="$(mktemp "$PWD/test-coverage-report-XXXXXXXX")"
fi

# Global option flags
OPT_SHOW_MISSING=0

# Tests top-level directory
top_testdir="${top_srcdir}/tests/ts"

# These are programs that we do not need to check on
ignore_programs=""

# We skip these programs because they do not make use of 'struct option longopts[]'
# which is passed to getopt(3) for command line argument parsing.
unsupported_programs='^blockdev$|^fsck$|^kill$|^mkfs\.cramfs$|^pg$|^renice$|^runuser$|^whereis$'

# Each program has a dedicated subdirectory with test scripts
program_test_subdirs="$(ls -1 ${top_testdir} | tr '\n' ' ')"

function usage() {
	cat <<EOF
Usage:
 getopt [options] <program>...

Generate a test coverage report for util-linux programs.

Options:
 -h, --help                    display this help
 -m, --show-missing            display missing long options

EOF
}

# Extract all user-facing programs from Makemodule.am files
# We look for: bin_PROGRAMS, sbin_PROGRAMS, usrbin_exec_PROGRAMS, usrsbin_exec_PROGRAMS
function extract_programs() {
	find "$top_srcdir" -name "Makemodule.am" -type f -exec grep -h \
		-E "^(bin|sbin|usrbin_exec|usrsbin_exec)_PROGRAMS \+=" {} \; 2>/dev/null |
		sed 's/.*+= *//' |
		tr ' ' '\n' |
		sed 's/\\//' |
		grep -v '^$' |
		grep -v '\.static$' |
		sort -u
}

function get_share() {
	a="$1"
	b="$2"

	[[ "$a" == 0 && "$b" == 0 ]] && echo "100.00" && return 0

	echo "$a $b" | awk '{ sum = ( $2 / $1 ) * 100; printf "%.2f", sum }' 2>/dev/null
}

function get_test_scripts_long_opts() {
	local prog test_scripts regex opts
	prog="$1"
	test_scripts="$2"
	# shellcheck disable=SC2016
	regex="[[:space:]]*--(?![^[:alnum:]])[A-Za-z-.0-9_]*"

	for ts in $test_scripts; do
		found="$(grep -P -o "$regex" "${ts}" |
			grep -P -o -- '--(?![^[:alnum:]])[A-Za-z-.0-9_]*' |
			uniq)"

		if [ -n "$found" ]; then
			opts+="$(printf -- '\n%s' "$found")"
		fi
	done

	echo "$opts" | uniq | sort
}

function find_real_test_longopts_per_prog() {
	prog="$1"
	ts_long_opts="$2"

	prog_long_opts="$(TOP_SRCDIR="${top_srcdir}" "${top_srcdir}"/tools/get-options.sh "$prog" |
		sed -e 's/^$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

	if [[ "$?" != "0" || -z "$prog_long_opts" ]]; then
		echo "Failed to get long options for $prog"
		return 1
	fi

	# tools/get-options.sh prints 'ENOTSUP' when it receives the name of an
	# unsupported program. See comments for the 'unsupported_programs' variable
	# in tools/get-options.sh for more details.
	#
	# We do not treat this case as an error, thereby we simply return 0 to the
	# caller and skip the comparison.
	if [ "$prog_long_opts" == "ENOTSUP" ]; then
		echo "$p,100.00% (0/0),not supported by tools/get-options.sh"
		return 0
	fi

	# Filter out --help and --version as these don't need testing
	prog_long_opts="$(echo "$prog_long_opts" | grep --invert-match -E -- '--help|--version')"

	if [[ -z "${prog_long_opts}" ]]; then
		prog_lng_opts_cnt=0
	else
		prog_lng_opts_cnt="$(echo "$prog_long_opts" | wc -l)"
	fi

	# This will put the found long options from the test scripts in a
	# regex pattern delimited by alternation/OR operators. The primary
	# reason for this is to avoid running a for loop for each option.
	# shellcheck disable=SC2059
	long_opts_regex="$(printf -- "$ts_long_opts" | awk -v RS="" \
		'{gsub (/\n/,"$|")} {printf "%s$|", $1}')"

	# found valid long options
	valid_ts_lng_opts="$(echo "${prog_long_opts}" | grep -o -E -- "${long_opts_regex}")"

	# Amount of found real long options in the test scripts
	ts_lng_opts_cnt="$(echo "${valid_ts_lng_opts}" | wc -l)"
	percentage="$(get_share "$prog_lng_opts_cnt" "$ts_lng_opts_cnt")%"

	echo "$prog,$percentage ($ts_lng_opts_cnt/$prog_lng_opts_cnt)"

	if [[ "${OPT_SHOW_MISSING}" == 1 ]]; then
		missing_lng_opts="$(comm -23 <(echo "${prog_long_opts}") <(echo "${valid_ts_lng_opts}") | tr '\n' ' ')"
		echo ",,$missing_lng_opts"
	fi
}

function calculate_test_coverage() {
	all_progs="$1"
	tested_progs="$2"

	num_total_progs="$(echo "$all_progs" | wc -w)"
	num_tested_progs="$(echo "$tested_progs" | wc -w)"
	share_ts_progs="$(get_share "$num_total_progs" "$num_tested_progs")"

	printf "%-45s%.2f%% (%d/%d)\n" "Total share of tested programs:" \
		"$share_ts_progs" "$num_tested_progs" "$num_total_progs"

	percentages="$(cat "${TMP_COVERAGE_REPORT_FILE}" |
		cut -d ',' -f 2 |
		grep -E -o '[0-9]*\.[0-9]*')"

	avg_ts_coverage="$(echo "${percentages}" | awk -v progs="$num_total_progs" \
		'{ sum += $1 } END { print sum / progs }')"

	printf "%-45s%.2f%%\n" "Overall test coverage:" "$avg_ts_coverage"
}

function generate_report() {
	all_programs="$1"

	echo "-------------------- util-linux test coverage report --------------------"
	echo
	echo "                      For development purpose only.                      "
	echo "                   Don't execute on production system!                   "
	echo
	echo "       This report represents the amount of long options each tool       "
	echo "             is testing out of it's provided set of options.             "
	echo ""

	errors=0
	for p in $all_programs; do
		[[ -n "$ignore_programs" && "$p" =~ $ignore_programs ]] && continue

		if ! echo "$program_test_subdirs" | grep -E " $p " &>/dev/null; then
			echo "$p,0.00% (0/0),missing test subdirectory" >>"${TMP_COVERAGE_REPORT_FILE}"
			((errors++))
			continue
		fi

		test_scripts="$(find "${top_testdir}/${p}" -maxdepth 1 -type f -executable \
			-exec grep -l 'ts_init' {} \; 2>/dev/null |
			tr '\n' ' ')"

		if [[ -z "$test_scripts" ]]; then
			echo "$p,0.00% (0/0),no test scripts found" >>"${TMP_COVERAGE_REPORT_FILE}"
			((errors++))
			continue
		fi

		if [[ "$p" =~ $unsupported_programs ]]; then
			echo "$p,100.00% (0/0),not supported by tools/get-options.sh" \
				>>"${TMP_COVERAGE_REPORT_FILE}"
			tested_programs+=" $p"
			continue
		fi

		ts_long_opts="$(get_test_scripts_long_opts "$p" "$test_scripts")"
		if [[ -z "${ts_long_opts}" ]]; then
			echo "$p,0.00% (0/0),no long options found in test script" \
				>>"${TMP_COVERAGE_REPORT_FILE}"
			tested_programs+=" $p"
			continue
		fi

		tested_programs+=" $p"

		find_real_test_longopts_per_prog "$p" "$ts_long_opts" >>"${TMP_COVERAGE_REPORT_FILE}"
	done

	column --output-width 70 --table-column name=UTILITY,left \
		--table-column name="TEST COVERAGE",right \
		--table-column name="NOTES",left,wrap \
		-s ',' -t "${TMP_COVERAGE_REPORT_FILE}"
	echo "-------------------------------------------------------------------------"

	calculate_test_coverage "$all_programs" "$tested_programs"
	rm -f "${TMP_COVERAGE_REPORT_FILE}"

	return $errors
}

function main() {
	all_programs="$(extract_programs)"
	shortopts="hm"
	longopts="help,show-missing"

	OPTS="$(getopt -l "${longopts}" -o "${shortopts}" -- "$@")"

	# shellcheck disable=SC2181
	[[ "$?" != 0 ]] && {
		echo "getopt(1) error"
		exit 1
	}

	eval set -- "$OPTS"

	while true; do
		case "$1" in
		'-h' | '--help')
			usage
			exit 0
			;;
		'-m' | '--show-missing')
			OPT_SHOW_MISSING=1
			shift
			continue
			;;
		'--')
			shift
			break
			;;
		*)
			echo "invalid option" >&2
			exit 1
			;;
		esac
	done

	if [[ "$#" == 0 ]]; then
		generate_report "$all_programs"
	else
		all_programs="$*"
		generate_report "${all_programs}"
	fi

	exit $?
}

main "${@}"
