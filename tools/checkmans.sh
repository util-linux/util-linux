#!/bin/bash
#
# Find all man pages, and check they do not have groff syntax errors
# or warnings.
#
# Sami Kerola <kerolasa@iki.fi>

set -e		# exit on errors
set -o pipefail	# exit if pipe writer fails
set -u		# disallow usage of unset variables
set -C		# disallow redirection file overwriting
SCRIPT_INVOCATION_SHORT_NAME=$(basename ${0})
trap 'echo "${SCRIPT_INVOCATION_SHORT_NAME}: exit on error"; exit 1' ERR

usage() {
	echo "Usage: ${0} [-vVh]"
	echo " -v  verbose messaging"
	echo " -V  print version and exit"
	echo " -h  print this help and exit"
}

VERBOSE='false'
while getopts vVh OPTIONS; do
	case ${OPTIONS} in
		v)
			VERBOSE='true'
			;;
		V)
			echo "util-linux: ${SCRIPT_INVOCATION_SHORT_NAME}: 2"
			exit 0
			;;
		h)
			usage
			exit 0
			;;
		*)
			usage
			exit 1
	esac
done

ERROR_FILE=$(mktemp ${SCRIPT_INVOCATION_SHORT_NAME}.XXXXXXXXXX)
# remove tmp file at exit
trap "rm -f ${ERROR_FILE}" 0

# Try to find missing manuals matching build targets with manual files.
declare -A MAN_LIST BIN_LIST

COUNT_ERRORS=0
declare -a REPEATS
declare -A KNOWN_REPEATS
KNOWN_REPEATS[mount.8]='foo'
KNOWN_REPEATS[sfdisk.8]="0 <c,h,s>"
KNOWN_REPEATS[flock.1]='"$0"'
KNOWN_REPEATS[switch_root.8]='$DIR'

remove_repeats()
{
	set +u
	for KN in ${KNOWN_REPEATS[${I##*/}]}; do
		if [ "${KN}" = "${REPEATS[$1]}" ]; then
			if $VERBOSE; then
				echo "info: ${I} removing repeat: ${REPEATS[$1]}"
			fi
			unset REPEATS[$1]
		fi
	done
	set -u
}

for I in $(
	find $(git rev-parse --show-toplevel) -name '*.[1-8]' |
	egrep -v '(Documentation|.git|/.libs/|autom4te.cache)'
); do
	MAN_FILE=${I##*/}
	MAN_LIST[${MAN_FILE%%.[0-9]}]=1
	if awk '{if (1 < NR) {exit 1}; if ($1 ~ /^.so$/) {exit 0}}' ${I}; then
		# Some manuals, such as x86_64, call inclusion and they
		# should not be tested any further.
		if ${VERBOSE}; then
			printf "skipping: ${I##*util-linux/}: includes "
			awk '{print $2}' ${I}
		fi
		continue
	fi
	I_ERR=0
	if ${VERBOSE}; then
		echo "testing: ${I}"
	fi
	MANWIDTH=80 man --warnings=all ${I} >/dev/null 2>| ${ERROR_FILE}
	if [ -s ${ERROR_FILE} ]; then
		echo "error: run: man --warnings=all ${I##*util-linux/} >/dev/null" >&2
		I_ERR=1
	fi
	if ! lexgrog ${I} >/dev/null; then
		echo "error: run: lexgrog ${I##*util-linux/}" >&2
		I_ERR=1
	fi
	REPEATS=( $(MANWIDTH=2000 man -l ${I} |
		col -b |
		sed  -e 's/\s\+/\n/g; /^$/d' |
		awk 'BEGIN { p="" } { if (0 < length($0)) { if (p == $0) { print } } p = $0 }') )
	if [ 0 -lt "${#REPEATS[@]}" ]; then
		ITER=${#REPEATS[@]}
		while [ -1 -lt ${ITER} ]; do
			remove_repeats ${ITER}
			# The 'let' may cause exit on error.
			# When ITER == 0 -> let returns 1, bash bug?
			let ITER=${ITER}-1 || true
		done
		if [ 0 -lt "${#REPEATS[@]}" ]; then
			echo "warning: ${I##*util-linux/} has repeating words: ${REPEATS[@]}"
		fi
	fi
	# The 'let' may cause exit on error.
	# COUNT_ERRORS=0; let COUNT_ERRORS=$COUNT_ERRORS+0; echo $?
	# Is this a bash bug?
	let COUNT_ERRORS=$COUNT_ERRORS+$I_ERR || true
done

# Create a list of build targets.
for I in $(find $(git rev-parse --show-toplevel) -name 'Make*.am' | xargs awk '
$1 ~ /_SOURCES/ {
	if ($1 ~ /^test/ ||
	    $1 ~ /^no(inst|dist)/ ||
	    $1 ~ /^sample/ ||
	    $1 ~ /^BUILT/) {
		next
	}
	sub(/_SOURCES/, "")
	if ($1 ~ /^lib.*_la/) {
		next
	}
	sub(/_static/, "")
	gsub(/_/, ".")
	sub(/switch.root/, "switch_root")
	sub(/pivot.root/, "pivot_root")
	print $1
}'); do
	BIN_LIST[$I]=1
done

# Find if build target does not have manual.
set +u
for I in ${!BIN_LIST[@]}; do
	if [ -v ${MAN_LIST[$I]} ]; then
		echo "warning: ${I} does not have man page"
	fi
done
set -u

if [ ${COUNT_ERRORS} -ne 0 ]; then
	echo "error: ${SCRIPT_INVOCATION_SHORT_NAME}: ${COUNT_ERRORS} manuals failed" >&2
	exit 1
fi

if ! ${VERBOSE}; then
	echo "${SCRIPT_INVOCATION_SHORT_NAME}: success"
fi

exit 0
