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
			echo "util-linux: ${SCRIPT_INVOCATION_SHORT_NAME}: v2.1"
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
declare -i COUNT_GROG=0
declare -i COUNT_TROFF=0
declare -i COUNT_REPEATS=0
declare -a REPEATS
declare -A KNOWN_REPEATS
KNOWN_REPEATS[mount.8]='foo l2 l c overlay'
KNOWN_REPEATS[hexdump.1]='l'
KNOWN_REPEATS[flock.1]='"$0"'
KNOWN_REPEATS[switch_root.8]='$DIR'
KNOWN_REPEATS[logger.1]='-'

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
	find . -type f -name '*[[:alpha:]].[1-8]' |grep -v "autom4te.cache\|\.libs/\|\.git"
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
	if ${VERBOSE}; then
		echo "testing: ${I}"
	fi
	RET=1
	cat ${I} | troff -mandoc -ww -z |& grep "<" && RET=$?
	if [ $RET = 0 ]; then
	echo "From: cat ${I} | troff -mandoc -ww -z"
	echo "=================================================="
	((++COUNT_TROFF))
	fi
	GROG=1
	if command -v lexgrog &> /dev/null; then
		if ! lexgrog ${I} >/dev/null; then
			echo "error: run: lexgrog ${I}"
			echo "=================================================="
			((++COUNT_GROG))
		fi
	elif command -v grog &> /dev/null; then
		if ! grog ${I} | grep man >/dev/null; then
			echo "error: grog ${I} is not a man file"
			echo "=================================================="
			((++COUNT_GROG))
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
			echo "=================================================="
			((++COUNT_REPEATS))
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
		echo "warning: ${SCRIPT_INVOCATION_SHORT_NAME}: ${I} does not have man page"
	fi
done
set -u

echo "${SCRIPT_INVOCATION_SHORT_NAME}: ${#BIN_LIST[*]} build targets were found"
echo "${SCRIPT_INVOCATION_SHORT_NAME}: ${#MAN_LIST[*]} files were tested"

if [ ${GROG} = 0 ]; then
echo "warning: neither grog nor lexgrog commands were found"
fi

if [ ${COUNT_GROG} -ne 0 ]; then
	echo "error: ${SCRIPT_INVOCATION_SHORT_NAME}: ${COUNT_GROG} files failed (lex)grog man-page test"
fi
if [ ${COUNT_TROFF} -ne 0 ]; then
	echo "error: ${SCRIPT_INVOCATION_SHORT_NAME}: ${COUNT_TROFF} files failed troff parsing test"
fi
if [ ${COUNT_REPEATS} -ne 0 ]; then
	echo "error: ${SCRIPT_INVOCATION_SHORT_NAME}: ${COUNT_REPEATS} files have repeating words"
fi
ITER=${#MAN_LIST[*]}-${COUNT_GROG}-${COUNT_TROFF}-${COUNT_REPEATS}
echo "${SCRIPT_INVOCATION_SHORT_NAME}: ${ITER} man-pages approved"

if [ ${COUNT_GROG} -ne 0 -o ${COUNT_TROFF} -ne 0 -o ${COUNT_REPEATS} -ne 0 ]; then
	exit 1
fi

if ! ${VERBOSE}; then
	echo "${SCRIPT_INVOCATION_SHORT_NAME}: success"
fi
exit 0
