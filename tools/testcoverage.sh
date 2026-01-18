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

top_srcdir="${1:-.}"
if [ -d "${top_srcdir}" ]; then
        shift 1
else
        echo "directory '${top_srcdir}' not found" >&2
        exit 1
fi

# Tests top-level directory
top_testdir="${top_srcdir}/tests/ts"

# These are programs that we do not need to check on
# swapoff: merged into the swapon manpage
ignore_programs="swapoff"

# Each program has a dedicated subdirectory with test scripts
program_test_subdirs="$(ls -1 ${top_testdir} | tr '\n' ' ')"

# Extract all user-facing programs from Makemodule.am files
# We look for: bin_PROGRAMS, sbin_PROGRAMS, usrbin_exec_PROGRAMS, usrsbin_exec_PROGRAMS
function extract_programs() {
        find "$top_srcdir" -name "Makemodule.am" -type f -exec grep -h \
                -E "^(bin|sbin|usrbin_exec|usrsbin_exec)_PROGRAMS \+=" {} \; \
                | sed 's/.*+= *//' \
                | tr ' ' '\n' \
                | sed 's/\\//' \
                | grep -v '^$' \
                | grep -v '\.static$' \
                | sort -u
}

function get_test_scripts_long_opts() {
        local prog test_scripts regex opts
	prog="$1"
        test_scripts="$2"
	# shellcheck disable=SC2016
	regex="$(printf '(\")?\$TS_CMD_%s(\")?.*[[:space:]]*--(?![^[:alnum:]])[A-Za-z-.0-9_]*' "${prog^^?}" )"

	for ts in $test_scripts; do
		found="$(grep -P -o "$regex" "${ts}" \
			| grep -P -o -- '--(?![^[:alnum:]])[A-Za-z-.0-9_]*' \
			| uniq )"

		if [ -n "$found" ]; then
			opts="$(printf -- '%s\n' "$found")"
		fi
	done

	[ -z "$opts" ] && printf "%s: no long options found" "$prog"

        echo "$opts" | uniq | sort
}

function calculate_test_coverage() {
        local prog ts_long_opts
        prog="$1"
	ts_long_opts="$2"

        prog_long_opts="$( TOP_SRCDIR="${top_srcdir}" "${top_srcdir}"/tools/get-options.sh "$prog" \
                        | sed -e 's/^$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

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
                return 0
        fi

	# This will put the found long options from the test scripts in a
	# regex pattern delimited by alternation/OR operator. The primary
	# reason for this is to avoid running a for loop for each option.
	# The last
	# shellcheck disable=SC2059
	long_opts_regex="$(printf -- "$ts_long_opts" | awk -v RS="" '{gsub (/\n/,"$|")} {printf "%s$|", $1}')"

	printf "%s: tested long options\n" "$prog"

	echo "${prog_long_opts}" | grep -o -E -- "${long_opts_regex}"

        return 0
}

function main() {
	local ts_long_opts
        programs="$(extract_programs)"

        errors=0

        for p in $programs; do
                [[ "$p" =~ $ignore_programs ]] && continue

		if ! echo "$program_test_subdirs" | grep "$p" &>/dev/null; then
			printf "[ERROR] the '%s' utility is missing tests\n" "$p"
			((errors++))
			continue
		fi

		test_scripts="$( find "${top_testdir}/${prog}" -type f -executable \
				-exec grep -l 'ts_init' {} \; 2>/dev/null \
				| tr '\n' ' ')"

		if [[ -z "$test_scripts" ]]; then
			printf "[ERROR] %s: no test scripts available" "$p"
			((errors++))
			continue
		fi

		ts_long_opts="$(get_test_scripts_long_opts "$p" "$test_scripts")"

		calculate_test_coverage "$p" "$ts_long_opts"

        done

        exit $errors
}

main
