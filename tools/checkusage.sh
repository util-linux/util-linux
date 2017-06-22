#!/bin/bash

export LC_ALL=C

if [ "$#" -lt 1 ]; then
	echo "usage: $0 program..." >&2
	echo "       or try 'make checkusage' to check all built programs" >&2
	exit 1
fi

builddir="."
cmds=$(echo $@ | tr ' ' '\n' | sort)

function exec_option {
	local cmd=$1
	shift

	opt=$@
	out=$("$cmd" "$@" 2>/dev/null)
	err=$("$cmd" "$@" 2>&1 >/dev/null)
	ret=$?
}

for c in $cmds; do
	cc="$builddir/$c"
	if ! type "$cc" &>/dev/null; then
		echo "$c: does not exist"
		continue
	fi

	exec_option "$cc" --help
	if test $ret != 0; then
		echo "$c: $opt, returns error"
	else
		if test -z "$out"; then
			echo "$c: $opt, no stdout"
		fi
		if test -n "$err"; then
			echo "$c: $opt, non-empty stderr"
		fi
	fi

	exec_option "$cc" --version
	if test $ret != 0; then
		echo "$c: $opt, returns error"
	else
		if test -z "$out"; then
			echo "$c: $opt, no stdout"
		fi
		if test -n "$err"; then
			echo "$c: $opt, non-empty stderr"
		fi
	fi

	exec_option "$cc" --unknownopt
	if test $ret = 0; then
		echo "$c: $opt, returns no error"
	fi
	if test -n "$out"; then
		echo "$c: $opt, non-empty stdout"
	fi
	if test -z "$err"; then
		echo "$c: $opt, no stderr"
	else
		out_len=$(echo "$out" | wc -l)
		err_len=$(echo "$err" | wc -l)
		if test "$err_len" -gt 2; then
			echo "$c: $opt, stderr too long: $err_len"
		elif test "$err_len" -lt 2; then
			echo "$c: $opt, stderr too short: $err_len"
		fi
	fi
done

