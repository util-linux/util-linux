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
# This script extracts all long options from each program's source file
# and prints them in an alphabetically sorted list.

TOP_SRCDIR=${TOP_SRCDIR:-../}

# Directories that contain relevant source files for util-linux programs.
src_file_paths="$(cd "${TOP_SRCDIR}" && grep -rE --include="*.c" --exclude="*test_*"          \
                        --exclude-dir="lib*"                            \
                        --exclude-dir="po*"                             \
                        --exclude-dir="tests"                           \
                        --exclude-dir="tools"                           \
                        --exclude-dir="bash-completion"                 \
                        --exclude-dir=".[a-z]*"                         \
                        --exclude-dir="man-common"                      \
                        --exclude-dir="Documentation"                   \
                        --exclude-dir="build*"                          \
                        -l "getopt_long(_only)?\s*\("                   \
                )"

# We skip these programs because they do not make use of 'struct option longopts[]'
# which is passed to getopt(3) for command line argument parsing.
unsupported_programs='blockdev|fsck|kill|mkfs\.cramfs|pg|renice|runuser|whereis'

# In general a program's source file name will be '<program_name>.c', however
# some tools have differing file names. To handle these special cases we build
# a hash table with the program name as the key and the actual source file name
# as the value, the latter will ultimately be passed to find_prog_src().
typeset -A canonical_src_prefix
canonical_src_prefix=( \
        [su]="su-common"
)

function find_prog_src() {
        local prog
        prog="$1"

        for p in ${src_file_paths}; do
                if [[ "${p##*/}" =~ ^"${prog}".c$ ]]; then
                        echo "${TOP_SRCDIR}/${p}"
                        break
                fi
        done
}

function extract_long_opts() {
        local src_path
        src_path="$1"

        awk -F ',' 'BEGIN { x = 0 }; \
                /struct[[:space:]]*option[[:space:]]*.*[[:space:]]*\[\][[:space:]]*=[[:space:]]*(\{)?/ { x = 1 } \
                x && ! /.*\/\*.*(deprecated|IGNORECHECK=yes).*\*\/.*/ {  print $1 } \
                /\};/ { x = 0 }' "${src_path}"  \
                | grep -Eo '".*"'               \
                | tr -d '"'                     \
                | sort                          \
                | awk '{ printf "--%s\n", $0 }' \
                | grep -v '^--_.*$'
}

function main() {
        local progname
        progname="$1"

        if [[ "$progname" =~ $unsupported_programs ]]; then
                echo "ENOTSUP"
                return 0
        fi

        # Handle special programs that have unusual source file names
        if [ -n "${canonical_src_prefix[$progname]+exists}" ]; then
                progname="${canonical_src_prefix[$progname]}"
        fi

        src_path="$(find_prog_src "$progname")"
        if [ -z "$src_path" ]; then
                return 1
        fi

        extract_long_opts "$src_path"

        return 0
}

main "$@"
