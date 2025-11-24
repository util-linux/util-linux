#!/bin/bash

#
# This script verifies bash-completion consistency by checking:
# 1. All user-facing programs have bash-completion files
# 2. All bash-completion files are registered in bash-completion/Makemodule.am
# 3. All bash-completion files are registered in meson.build
# 4. All bash-completion files correspond to actual programs
# 5. All bash-completion files handle all available long options in each program
#
# Copyright (C) 2025 Karel Zak <kzak@redhat.com>
# Copyright (C) 2025 Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
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
	echo "  - meson.build registrations"
	exit 1
}

# Programs that don't need bash completion:
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

# Certain completions have an unusual algorithm that is distinct from the pattern used
# in the majority of completion files, we skip these for now.
unusual_completions="pipesz"

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
		| sed -e 's/.*bash-completion\///' \
		      -e 's/\s.*//' \
		| sort -u
}

# Get actual bash-completion files
get_completion_files() {
	ls "${completion_dir}" \
		| grep -v '^Makemodule.am$' \
		| sort
}

# Extract programs from meson.build
extract_meson_registered() {
	if [ -f "meson.build" ]; then
		grep "bashcompletions +=" meson.build \
			| sed -e "s/.*bashcompletions += //" \
			      -e "s/\[//" \
			      -e "s/\]//" \
			      -e "s/'//g" \
			| tr ',' '\n' \
			| sed -e 's/^ *//' \
			      -e 's/ *$//' \
			| grep -v '^$' \
			| sort -u
	fi
}

# Check for the bash-completion file integrity, i.e. all long options are completed
# Argument(s): program_name
check_completion_file_integrity() {
	local prog="$1"

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

	comp_opts="$( cat "${completion_dir}/${prog}" \
			| grep -o -P '[[:space:]]*--(?![^[:alnum:]])[A-Za-z-.0-9_]*' \
			| sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
			| sort \
			| uniq )"

	res="$( comm -23 <(echo "${prog_long_opts}") <(echo "${comp_opts}") )"
	if [ -n "$res" ]; then
		printf "%s\n%s\n" "${prog}:" "$res"
		return 1
	fi

	return 0
}

# Get programs that should have completions
programs=$(extract_programs | grep -v -w -E "(${exclude_programs}|${special_handling})")

# Get registered completions
registered=$(extract_completion_registered)

# Get meson registered completions
meson_registered=$(extract_meson_registered)

# Get actual completion files
files=$(get_completion_files)

# Find programs without completion files
missing_files=$(comm -23 <(echo "$programs") <(echo "$files"))

# Find completion files not registered in Makemodule.am
unregistered=$(comm -23 <(echo "$files") <(echo "$registered"))

# Find completion files without corresponding programs
orphaned=$(comm -23 <(echo "$files") <(echo "$programs"))

# Find completion files not registered in meson.build
meson_unregistered=$(comm -23 <(echo "$files") <(echo "$meson_registered"))

# Report findings
errors=0

if [ -n "$missing_files" ]; then
	echo "Programs missing bash-completion files:"
	echo "$missing_files" | sed 's/^/  /'
	errors=$((errors + 1))
fi

if [ -n "$unregistered" ]; then
	echo "bash-completion files not registered in bash-completion/Makemodule.am:"
	echo "$unregistered" | sed 's/^/  /'
	errors=$((errors + 1))
fi

if [ -n "$orphaned" ]; then
	echo "bash-completion files without corresponding programs:"
	echo "$orphaned" | sed 's/^/  /'
	errors=$((errors + 1))
fi

if [ -n "$meson_unregistered" ]; then
	echo "bash-completion files not registered in meson.build:"
	echo "$meson_unregistered" | sed 's/^/  /'
	errors=$((errors + 1))
fi

for f in $files; do
	[[ "$f" =~ $unusual_completions ]] && continue
	check_completion_file_integrity "$f" || errors=$((errors + 1))
done

if [ $errors -eq 0 ]; then
	echo "All bash-completion files are consistent."
	exit 0
else
	exit 1
fi
