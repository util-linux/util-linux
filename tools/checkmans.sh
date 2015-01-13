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

# Try to find missing manuals matching build targets with manual files.
declare -A MAN_LIST BIN_LIST

declare -i ITER
declare -i COUNT_ERRORS=0
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

cd $(git rev-parse --show-toplevel)

for I in $(
	find -path './autom4te.cache' -prune -o -name '*[[:alpha:]].[1-8]' -print
); do
	MAN_FILE=${I##*/}
	MAN_LIST[${MAN_FILE%%.[0-9]}]=1
	if awk '{if (1 < NR) {exit 1}; if ($1 ~ /^.so$/) {exit 0}}' ${I}; then
		# Some manuals, such as x86_64, call inclusion and they
		# should not be tested any further.
		if ${VERBOSE}; then
			printf "skipping: ${I}: includes "
			awk '{print $2}' ${I}
		fi
		continue
	fi
	I_ERR=0
	if ${VERBOSE}; then
		echo "testing: ${I}"
	fi
	RET=1
	cat ${I} | troff -mandoc -ww -z |& grep "<" && RET=$?
	if [ $RET = 0 ]; then
	echo "From: cat ${I} | troff -mandoc -ww -z"
	echo "=================================================="
	fi
	GROG=1
	if command -v lexgrog &> /dev/null; then
		if ! lexgrog ${I} >/dev/null; then
			echo "error: run: lexgrog ${I}"
			echo "=================================================="
			((++COUNT_ERRORS))
		fi
	elif command -v grog &> /dev/null; then
		if ! grog ${I} | grep man >/dev/null; then
			echo "error: grog ${I} is not a man file"
			echo "=================================================="
			((++COUNT_ERRORS))
		fi
	else
	GROG=0
	fi
	REPEATS=( $(cat ${I} | troff -mandoc -Tascii 2>/dev/null | grotty |
		col -b |
		sed  -e 's/\s\+/\n/g; /^$/d' |
		awk 'BEGIN { p="" } { if (0 < length($0)) { if (p == $0) { print } } p = $0 }') )
	if [ 0 -lt "${#REPEATS[@]}" ]; then
		ITER=${#REPEATS[@]}+1
		while ((ITER--)); do
			remove_repeats ${ITER}
		done
		if [ 0 -lt "${#REPEATS[@]}" ]; then
			echo "warning: ${I} has repeating words: ${REPEATS[@]}"
		fi
	fi
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

if [ ${GROG} = 0 ]; then
echo "warning: neither grog nor lexgrog commands were found"
fi

if [ ${COUNT_ERRORS} -ne 0 ]; then
	echo "error: ${SCRIPT_INVOCATION_SHORT_NAME}: ${COUNT_ERRORS} manuals failed"
	exit 1
fi

if ! ${VERBOSE}; then
	echo "${SCRIPT_INVOCATION_SHORT_NAME}: success"
fi

exit 0
