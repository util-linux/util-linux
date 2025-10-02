#!/bin/bash

#
# This script verifies bash-completion consistency by checking:
# 1. All user-facing programs have bash-completion files
# 2. All bash-completion files are registered in bash-completion/Makemodule.am
# 3. All bash-completion files correspond to actual programs
#
# Copyright (C) 2025 Karel Zak <kzak@redhat.com>
#

set -e

die() {
	echo "error: $1" >&2
	exit 1
}

usage() {
	echo "Usage: $0 [<top_srcdir>]"
	echo
	echo "Verifies bash-completion consistency across:"
	echo "  - Program definitions in */Makemodule.am"
	echo "  - bash-completion/ directory contents"
	echo "  - bash-completion/Makemodule.am registrations"
	exit 1
}

# Programs that don't need bash completion (system tools, low-level utilities)
# - nologin: no interactive use
# - agetty: terminal setup, no user interaction
# - login: authentication, no command-line arguments
# - sulogin: emergency login, no arguments
# - switch_root: initramfs utility, internal use
# - vipw: wrapper around editor
# - line: deprecated, simple utility
# - kill: conflicts with bash built-in kill command
exclude_programs="nologin|agetty|login|sulogin|switch_root|vipw|line|kill"

# Programs with special handling in bash-completion/Makemodule.am (symlinks/aliases)
# These are handled via install-data-hook-bashcomp-* rules
# - runuser: symlinked to su completion
# - lastb: symlinked to last completion
special_handling="runuser|lastb"

top_srcdir=${1:-.}
[ -d "${top_srcdir}" ] || die "directory '${top_srcdir}' not found"

cd "${top_srcdir}" || die "cannot cd to ${top_srcdir}"

completion_dir="bash-completion"
[ -d "${completion_dir}" ] || die "not found ${completion_dir}"

# Extract all user-facing programs from Makemodule.am files
# We look for: bin_PROGRAMS, sbin_PROGRAMS, usrbin_exec_PROGRAMS, usrsbin_exec_PROGRAMS
extract_programs() {
	find . -name "Makemodule.am" -type f -exec grep -h \
		-E "^(bin|sbin|usrbin_exec|usrsbin_exec)_PROGRAMS \+=" {} \; \
		| sed 's/.*+= *//' \
		| tr ' ' '\n' \
		| sed 's/\\//' \
		| grep -v '^$' \
		| grep -v '\.static$' \
		| sort -u
}

# Extract programs from bash-completion/Makemodule.am
extract_completion_registered() {
	grep 'bash-completion/' "${completion_dir}/Makemodule.am" \
		| sed 's/.*bash-completion\///' \
		| sed 's/\s.*//' \
		| sort -u
}

# Get actual bash-completion files
get_completion_files() {
	ls "${completion_dir}" \
		| grep -v '^Makemodule.am$' \
		| sort
}

# Get programs that should have completions
programs=$(extract_programs | grep -v -w -E "(${exclude_programs}|${special_handling})")

# Get registered completions
registered=$(extract_completion_registered)

# Get actual completion files
files=$(get_completion_files)

# Find programs without completion files
missing_files=$(comm -23 <(echo "$programs") <(echo "$files"))

# Find completion files not registered in Makemodule.am
unregistered=$(comm -23 <(echo "$files") <(echo "$registered"))

# Find completion files without corresponding programs
orphaned=$(comm -23 <(echo "$files") <(echo "$programs"))

# Report findings
errors=0

if [ -n "$missing_files" ]; then
	echo "Programs missing bash-completion files:"
	echo "$missing_files" | sed 's/^/  /'
	echo
	errors=$((errors + 1))
fi

if [ -n "$unregistered" ]; then
	echo "bash-completion files not registered in bash-completion/Makemodule.am:"
	echo "$unregistered" | sed 's/^/  /'
	echo
	errors=$((errors + 1))
fi

if [ -n "$orphaned" ]; then
	echo "bash-completion files without corresponding programs:"
	echo "$orphaned" | sed 's/^/  /'
	echo
	errors=$((errors + 1))
fi

if [ $errors -eq 0 ]; then
	echo "All bash-completion files are consistent."
	exit 0
else
	exit 1
fi
