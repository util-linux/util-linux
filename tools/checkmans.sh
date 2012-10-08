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
	echo "Usage: ${0} [-ph]"
	echo " -p   print file names before checking"
}

PRINT_FILE_NAMES='false'
while getopts ph OPTIONS; do
	case ${OPTIONS} in
		p)
			PRINT_FILE_NAMES='true'
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

for I in $(
	find $(git rev-parse --show-toplevel) -name '*.[1-8]' |
	egrep -v '(Documentation|.git|/.libs/|autom4te.cache)'
); do
	# FIXME: the determination whether a manual does include
	# should probably be somewhat smarter.
	IS_INCLUDE=$(wc -w ${I} | awk '{print $1}')
	if [ ${IS_INCLUDE} -eq 2 ]; then
		# Some manuals, such as x86_64, call include which
		# will read system manual.  Testing what comes from
		# package does not make much sense, so skip doing it.
		if ${PRINT_FILE_NAMES}; then
			echo "skipping: ${I}"
		fi
		continue
	fi
	if ${PRINT_FILE_NAMES}; then
		echo "testing: ${I}"
		man --warnings=all ${I} >/dev/null
	else
		man --warnings=all ${I} >/dev/null 2>> ${ERROR_FILE}
	fi
done

COUNT_ERRORS=$(awk 'END {print NR}' ${ERROR_FILE})
if [ ${COUNT_ERRORS} -ne 0 ]; then
	echo "${SCRIPT_INVOCATION_SHORT_NAME}: failed"
	echo "use: $(readlink -f ${0}) -p"
	echo "     to find where the problems are."
	exit 1
fi

if ! ${PRINT_FILE_NAMES}; then
	echo "${SCRIPT_INVOCATION_SHORT_NAME}: success"
fi
exit 0
