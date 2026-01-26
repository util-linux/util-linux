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
# This script verifies if each program has a corresponding manpage
# and checks if said manpages have a description for each program's
# long options. The list of program long options is based on the
# respective program's source code. 

top_srcdir="${1:-.}"
if [ -d "${top_srcdir}" ]; then
        shift 1
else
        echo "directory '${top_srcdir}' not found" >&2
        exit 1
fi

# These are programs that we do not need to check on
# swapoff: merged into the swapon manpage
ignore_programs="swapoff"

# We only need manpages for sections 1,7 and 8
manpages="$( find "$top_srcdir" -name "*[178].adoc" -type f | uniq )"

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

function find_prog_manpage() {
        local prog
        prog="$1"

        for m in ${manpages}; do
                if [[ "${m##*/}" =~ ^"${prog}"\.[1-9]\.adoc$ ]]; then
                        echo "${m}"
                        break
                fi
        done
}

function get_manpage_options() {
        local adoc
        adoc="$1"

        opts="$(cat "$adoc" \
                        | grep -o -P '[[:space:]]*--(?![^[:alnum:]])[A-Za-z-.0-9_]*' \
                        | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
                        | sort \
                        | uniq )"

        # --help and --version are included with a manpage stub @ man-common/help-version.adoc
        if grep -E 'include::man-common\/help-version.adoc\[\]' "$adoc" &>/dev/null; then
                opts="$(printf -- "--version\n--help\n%s" "$opts" | sort | uniq )"
        fi

        # --annotate is included with a manpage stub @ man-common/annotate.adoc
        if grep -E 'include::man-common\/annotate.adoc\[\]' "$adoc" &>/dev/null; then
                opts="$(printf -- "--annotate\n%s" "$opts" | sort | uniq )"
        fi

        # --hyperlink is included with a manpage stub @ man-common/hyperlink.adoc
        if grep -E 'include::man-common\/hyperlink.adoc\[\]' "$adoc" &>/dev/null; then
                opts="$(printf -- "--hyperlink\n%s" "$opts" | sort | uniq )"
        fi

        echo "$opts"
}

function check_manpage_integrity() {
        local prog
        prog="$1"

        adoc="$(find_prog_manpage "$prog")"
        if [ -z "$adoc" ]; then
                echo "ERROR: no manpage found for $prog" >&2
                return 1
        fi

        manpage_opts="$( get_manpage_options "$adoc" )"

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

        res="$( comm -23 <(echo "${prog_long_opts}") <(echo "${manpage_opts}") )"
        if [ -n "$res" ]; then
                printf "%s\n%s\n" "${prog} (missing/extraneous manpage description(s)):" "$res"
                return 1
        fi

        return 0
}

function main() {
        programs="$(extract_programs)"

        errors=0

        for p in $programs; do
                [[ "$p" =~ $ignore_programs ]] && continue
                check_manpage_integrity "$p" || errors=$((errors + 1))
        done

        exit $errors
}

main
