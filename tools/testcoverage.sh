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
# If an issue has been encountered with any tool's tests, a note
# will be added to each respective tool's row and the script will
# exit with a non-zero status code.

top_srcdir="$(realpath -qLs "${1:-.}")"
if [ -d "${top_srcdir}" ]; then
        shift 1
else
        echo "directory '${top_srcdir}' not found" >&2
        exit 1
fi

# shellcheck disable=SC2329
function cleanup() {
	rm -f "$TMP_COVERAGE_RAW_REPORT_FILE"
	rm -f "$TMP_COVERAGE_SUMMARY_REPORT_FILE"
	[ -t 1 ] && printf "\033[2K\r"
	exit 0
}

trap cleanup SIGTERM SIGHUP SIGINT

if ! type mktemp >/dev/null 2>&1; then
	echo "missing dependency 'mktemp'"
	exit 1
else
	TMP_COVERAGE_RAW_REPORT_FILE="$(mktemp "$PWD/test-coverage-raw-report-XXXXXXXX")"
	TMP_COVERAGE_SUMMARY_REPORT_FILE="$(mktemp "$PWD/test-coverage-summary-report-XXXXXXXX")"
fi

# Global option flags
OPT_SHOW_MISSING_OPTS=0
OPT_SAVE_REPORT=0

# Tests top-level directory
top_testdir="${top_srcdir}/tests/ts"

# We skip these programs because they do not make use of 'struct option longopts[]'
# which is passed to getopt(3) for command line argument parsing.
unsupported_programs=$(grep 'unsupported_programs=' "${top_srcdir}"/tools/get-options.sh \
								| cut -d '=' -f 2 | tr -d "\'" )

# These are programs that we do not need to check on
ignore_programs=""

# Each program has a dedicated subdirectory with test scripts
program_test_subdirs="$(ls -1 ${top_testdir} | tr '\n' ' ')"

# All registered test scripts for all programs
ALL_TEST_SCRIPTS="$(find "${top_testdir}/" -maxdepth 2 -type f -executable \
			-exec realpath -qLs {} \; 2>/dev/null |
			tr '\n' ' ')"

function usage() {
	cat <<EOF
Usage:
 testcoverage.sh <top_srcdir> [options] <program>...

Generate a test coverage report for util-linux programs.

Options:
 -h, --help             	display this help
 -m, --show-missing-opts	display missing long options
 -s, --save-report		save the report file

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

function get_opts_from_src() {
	local long_opts prog
	prog="$1"

	long_opts="$(TOP_SRCDIR="${top_srcdir}" "${top_srcdir}"/tools/get-options.sh "$prog" |
					sed -e 's/^$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

	[[ "$?" != "0" || -z "$long_opts" ]] && return 1

	echo "${long_opts}" | uniq | sort
	return 0
}

function progress_status() {
	local counter num_total_progs
	prog="$1"
	num_total_progs="$2"
	counter="$3"

	printf "\033[2K\rtesting program %d out of %d ('%s')" "$counter" "$num_total_progs" "$prog"
}

# Since we do "cross-testing", we check if $prog is being tested
# in other program's test scripts and store the found options too.
function get_cross_test_long_opts() {
	local prog test_scripts regex opts
	prog="$1"
	test_scripts="$2"

	[[ -z "$test_scripts" ]] && has_ts=0
	# shellcheck disable=SC2016
	regex="$( printf '\$TS_CMD_%s[[:space:]]+.*([[:space:]])*--(?![^[:alnum:]])[A-Za-z-.0-9_]*' "${prog^^}" )"

	for t in $ALL_TEST_SCRIPTS; do
		# If the program has a test subdirectory we have probably
		# already traversed it, so no need to do it again.
		[[ "$has_ts" == 1 && "$t" =~ \/"$prog"\/ ]] && continue

		found="$(grep -P -o "$regex" "${t}" \
			| grep -P -o -- '--(?![^[:alnum:]])[A-Za-z-.0-9_]*' \
			| uniq)"

		if [ -n "$found" ]; then
			opts+="$(printf -- '\n%s' "$found")"
		fi
	done

	echo "$opts" | sort | uniq
}

function get_test_scripts_l_opts() {
	local prog test_scripts regex opts
	prog="$1"
	test_scripts="$2"

	# Look for all options in $prog test scripts
	for ts in $test_scripts; do
		found="$(grep -P -o '[[:space:]]--(?![^[:alnum:]])[A-Za-z-.0-9_]*' "${ts}" \
			| grep -P -o -- '--(?![^[:alnum:]])[A-Za-z-.0-9_]*' \
			| uniq)"

		if [ -n "$found" ]; then
			opts+="$(printf -- '\n%s' "$found")"
		fi
	done

	echo "$opts" | sort | uniq
}

function print_long_opts_summary() {
	prog="$1"
	prog_l_opts="$2"
	ts_l_opts="$3"
	notes="$4"
	missing_l_opts='-'
	prog_l_opts_cnt="$(echo "$prog_l_opts" | wc -l)"

	# This will put the found long options from the test scripts in a
	# regex pattern delimited by alternation/OR operators. The primary
	# reason for this is to avoid running a for loop for each option.
	# shellcheck disable=SC2059
	l_opts_regex="$(printf -- "$ts_l_opts" | awk -v RS="" \
			'{gsub (/\n/,"$|")} {printf "%s", $1}')"

	# valid long options found in the test scripts
	valid_ts_l_opts="$(echo "${prog_l_opts}" | grep -o -E -- "${l_opts_regex}")"

	# Amount of found valid long options in the test scripts
	ts_l_opts_cnt="$(echo "${valid_ts_l_opts}" | wc -l)"

	percentage="$(get_share "$prog_l_opts_cnt" "$ts_l_opts_cnt")%"

	if [[ "${OPT_SHOW_MISSING_OPTS}" == 1 ]]; then
		missing_l_opts="$( comm -23 <(echo "${prog_l_opts}") \
					<(echo "${valid_ts_l_opts}") | tr '\n' ' ')"
	fi

	echo "$prog|$percentage% ($ts_l_opts_cnt/$prog_l_opts_cnt)|$missing_l_opts|$notes"
}

function print_report() {
	[ -t 1 ] && printf "\033[2K\r"
	echo "-------------------- util-linux test coverage report --------------------"
	echo
	echo "                      For development purpose only.                      "
	echo "                   Don't execute on production system!                   "
	echo
	echo "       This report represents the amount of long options each tool       "
	echo "             is testing out of it's provided set of options.             "
	echo ""
	echo ""

	column  --output-width 80 \
	--output-separator "    " \
	--table-column name=UTILITY,left,wrap \
	--table-column name="TEST COVERAGE",right,wrap \
	--table-column name="MISSING OPTIONS",left,wrap \
	--table-column name="NOTES",left,noextreme \
	-s '|' -t "${TMP_COVERAGE_RAW_REPORT_FILE}" >>"${TMP_COVERAGE_SUMMARY_REPORT_FILE}"

	cat "${TMP_COVERAGE_SUMMARY_REPORT_FILE}"

	echo ""
	echo "-------------------------------------------------------------------------"
}

function calculate_test_coverage() {
	num_total_progs="$1"
	num_tested_progs="$2"

	share_ts_progs="$(get_share "$num_total_progs" "$num_tested_progs")"

	printf "%-45s%.2f%% (%d/%d)\n" "Total share of tested programs:"\
				"$share_ts_progs" "$num_tested_progs" "$num_total_progs"

	percentages="$(cat "${TMP_COVERAGE_RAW_REPORT_FILE}" |
						cut -d '|' -f 2 | grep -E -o '[0-9]*\.[0-9]*')"

	avg_ts_coverage="$( echo "${percentages}" | awk -v progs="$num_total_progs" \
						'{ sum += $1 } END { print sum / progs }' )"

	printf "%-45s%.2f%%\n" "Overall test coverage:" "$avg_ts_coverage"
}

function generate_report() {
	local percentage frac notes
	local has_ts_dir has_ts
	local num_total_progs num_tested_progs
	all_programs="$1"

	num_total_progs="$(echo "$all_programs" | wc -w)"

	echo "Generating report ..."

	error=0
	counter=0
	for prog in $all_programs; do
		percentage=''
		frac=''
		notes=''
		((counter++))

		[ -t 1 ] && progress_status "$prog" "$num_total_progs" "$counter"

		[[ -n "$ignore_programs" && "$prog" =~ $ignore_programs ]] && continue

		# Test whether the program is supported by tools/get-options.sh.
		# If it isn't, we will not be able to get an exact list of long
		# options from the program's source code, so we skip the check.
		#
		# In this case, we will also assume that all long options are
		# tested, it is up to the developer to ensure this is correct.
		if [[ "$prog" =~ $unsupported_programs ]]; then
			percentage=100.00
			frac=1/1
			notes="skipped check (not supported by tools/get-options.sh)"

			echo "$prog|${percentage}% (${frac})|-|${notes}" >>"${TMP_COVERAGE_RAW_REPORT_FILE}"
			tested_programs+=" $prog"
			continue
		fi

		if ! echo "$program_test_subdirs" | grep -E " $prog " &>/dev/null; then
			percentage=0.00
			frac=0/0
			notes="missing test subdirectory, "
			has_ts_dir=0
			error=1
		else
			has_ts_dir=1
		fi

		if [[ "$has_ts_dir" == 1 ]]; then
			test_scripts="$(find "${top_testdir}/${prog}" -maxdepth 1 -type f -executable \
							-exec grep -l 'ts_init' {} \; 2>/dev/null | tr '\n' ' ')"
		fi

		if [[ -z "$test_scripts" ]]; then
			percentage=0.00
			frac=0/0
			notes+="no test scripts found"
			has_ts=0
			error=1
		else
			has_ts=1
		fi

		# get the real long options from the program's source code
		prog_l_opts="$(get_opts_from_src "$prog")"
		if [[ "$?" != 0 || -z "${prog_l_opts}" ]]; then
			percentage=0.00
			frac=0/0
			notes="failed to get long options from source code"

			echo "$prog|${percentage}% (${frac})|-|${notes}" >>"${TMP_COVERAGE_RAW_REPORT_FILE}"
			error=1
			continue
		fi

		# we don't need --help and --version
		prog_l_opts="$(echo "$prog_l_opts" | grep --invert-match -E -- '--help|--version')"

		if [[ -z "${prog_l_opts}" ]]; then
			percentage=100.00
			frac=0/0
			notes="no long options to test"

			echo "$prog|$percentage% ($frac)|-|$notes" >>"${TMP_COVERAGE_RAW_REPORT_FILE}"
			continue
		fi

		# get long options from the program's tests scripts
		if [[ $has_ts == 1 ]]; then
			ts_l_opts="$(get_test_scripts_l_opts "$prog" "$test_scripts")"
		fi

		# get long options from cross tests in other program scripts
		ts_l_opts+="$(get_cross_test_long_opts "$prog")"

		ts_l_opts="$(echo "$ts_l_opts" | sort | uniq)"

		if [[ -z "${ts_l_opts}" ]]; then
			percentage=0.00
			frac=0/0
			notes="no long options found in test script(s)"

			prog_l_opts="$(echo "$prog_l_opts" | tr '\n' ' ')"

			if [[ "${OPT_SHOW_MISSING_OPTS}" != 1 ]]; then
				prog_l_opts='-'
			fi

			echo "$prog|${percentage}% (${frac})|${prog_l_opts}|${notes}" >>"${TMP_COVERAGE_RAW_REPORT_FILE}"
			error=1
			continue
		fi

		tested_programs+=" $prog"

		print_long_opts_summary "$prog" "$prog_l_opts" "$ts_l_opts" "$notes" >>"${TMP_COVERAGE_RAW_REPORT_FILE}"
	done

	num_tested_progs="$(echo "$tested_programs" | wc -w)"

	print_report

	calculate_test_coverage "$num_total_progs" "$num_tested_progs"

	if [[ "${OPT_SAVE_REPORT}" != 1 ]]; then
		rm -f "${TMP_COVERAGE_SUMMARY_REPORT_FILE}"
	else
		printf "%-45s%s\n" "Saved report file:" "${TMP_COVERAGE_SUMMARY_REPORT_FILE}"
	fi

	rm -f "${TMP_COVERAGE_RAW_REPORT_FILE}"

	return $error
}

function main() {
	all_programs="$(extract_programs)"
	shortopts="hms"
	longopts="help,save-report,show-missing-opts"

	OPTS="$(getopt -l "${longopts}" -o "${shortopts}" -- "$@")"

	# shellcheck disable=SC2181
	[[ "$?" != 0 ]] && {
		echo "getopt(1) error"
		exit 1
	}

	eval set -- "$OPTS"

	while true; do
		case "$1" in
		'-h'|'--help')
			usage
			exit 0
			;;
		'-m'|'--show-missing-opts')
			OPT_SHOW_MISSING_OPTS=1
			shift
			continue
			;;
		'-s'|'--save-report')
			OPT_SAVE_REPORT=1
			shift
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
