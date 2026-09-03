#!/bin/bash
#
# Check for file-descriptor-creating calls that are missing the
# close-on-exec flag.  Leaked descriptors across exec() are a
# security risk (CVE-worthy in setuid tools and libraries).
#
# Scope:
#   - library sources (always -- callers may exec)
#   - tool sources that call exec*()
#
# Lines tagged with a NOCLOEXEC comment are intentional (the descriptor
# must survive exec, e.g. when it becomes stdin for a child) and are
# silently skipped.
#
# Usage:  checkcloexec.sh
#

cd "$(git rev-parse --show-toplevel)" || {
	echo "error: failed to chdir to git root"
	exit 1
}

TMPFILE=$(mktemp) || exit 1
trap 'rm -f "$TMPFILE"' EXIT

# Collect files to check once:
#   1. All library and shared include sources
#   2. Any .c file that calls exec*()
collect_files()
{
	{
		git ls-files -- 'lib/*.[ch]' 'include/*.[ch]'
		git ls-files -- 'lib*/*.[ch]'
		git grep -rlE '\bexecv?[lp]?e?\s*\(' -- '*.[ch]'
	} | sort -u
}

FILES=$(collect_files)
if [ -z "$FILES" ]; then
	exit 0
fi

found=0

# Strip false positives from git-grep output (file:line:content format):
#   - comment lines (// or /* or continuation *)
#   - the call name only appears inside a string literal or a comment
#   - function definitions/declarations (the syscall wrapper itself)
strip_noise()
{
	grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|/\*|\*[[:space:]]|\*$)' |
	grep -vE '/\*.*\b'"$1"'\s*\(' |
	grep -vE '"[^"]*\b'"$1"'\s*\(' |
	grep -vE 'static[[:space:]]+inline[[:space:]].*\b'"$1"'\s*\(' |
	grep -v 'NOCLOEXEC'
}

# check_flag CALL CLOEXEC [LITERAL_FILTER]
#
# Find lines that call CALL() and are missing the CLOEXEC token.
# LITERAL_FILTER, when set, limits matches to lines containing
# that string -- used to skip calls where flags are a variable
# (e.g. only flag open() lines that have a literal "O_" constant).
check_flag()
{
	local call="$1" cloexec="$2" literal="${3-}"
	local result

	echo "$FILES" | xargs git grep -nE "\b${call}\s*\(" -- \
		2>/dev/null > "$TMPFILE" || return
	[ -s "$TMPFILE" ] || return

	result=$(strip_noise "$call" < "$TMPFILE")
	[ -n "$result" ] || return

	if [ -n "$literal" ]; then
		result=$(echo "$result" | grep -F "$literal")
		[ -n "$result" ] || return
	fi

	result=$(echo "$result" | grep -v "$cloexec")
	[ -n "$result" ] || return

	echo "# ${call}() missing ${cloexec}:"
	echo "$result"
	echo
	found=1
}

# check_obsolete OLD_CALL NEW_CALL
#
# Flag uses of OLD_CALL() that should be replaced by NEW_CALL().
# The regex is crafted so that OLD_CALL does not match NEW_CALL
# (e.g. epoll_create vs epoll_create1).
check_obsolete()
{
	local old="$1" new="$2"
	local result

	echo "$FILES" | xargs git grep -nE "\b${old}\s*\(" -- \
		2>/dev/null > "$TMPFILE" || return
	[ -s "$TMPFILE" ] || return

	result=$(strip_noise "$old" < "$TMPFILE")
	[ -n "$result" ] || return

	echo "# ${old}() is obsolete, use ${new}:"
	echo "$result"
	echo
	found=1
}

# --- flag checks ---

check_flag 'open'            'O_CLOEXEC'          'O_'
check_flag 'openat'          'O_CLOEXEC'          'O_'
check_flag 'fopen'           'UL_CLOEXECSTR'      '"'
check_flag 'socket'          'SOCK_CLOEXEC'
check_flag 'socketpair'      'SOCK_CLOEXEC'
check_flag 'accept4'         'SOCK_CLOEXEC'
check_flag 'epoll_create1'   'EPOLL_CLOEXEC'
check_flag 'signalfd'        'SFD_CLOEXEC'
check_flag 'timerfd_create'  'TFD_CLOEXEC'
check_flag 'eventfd'         'EFD_CLOEXEC'
check_flag 'inotify_init1'   'IN_CLOEXEC'
check_flag 'fanotify_init'   'FAN_CLOEXEC'
check_flag 'fsopen'          'FSOPEN_CLOEXEC'
check_flag 'fsmount'         'FSMOUNT_CLOEXEC'
check_flag 'fspick'          'FSPICK_CLOEXEC'
check_flag 'open_tree'       'OPEN_TREE_CLOEXEC'  'OPEN_TREE_'
check_flag 'pipe2'           'O_CLOEXEC'

# --- obsolete calls ---

check_obsolete 'epoll_create'   'epoll_create1(EPOLL_CLOEXEC)'
check_obsolete 'inotify_init'   'inotify_init1(IN_CLOEXEC)'

exit $found
