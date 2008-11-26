#!/bin/bash

#
# This script checks for HAVE_ and ENABLE_ macros which are
# not included in config.h.in
#
# Copyright (C) 2007 Matthias Koenig <mkoenig@suse.de>
#

srcdir=$1

if [ ! "$srcdir" ]; then
	srcdir=$PWD
fi

CONFIG="$srcdir/config.h.in"
if [ ! -f "$CONFIG" ]; then
	echo "config.h.in is needed"
	exit 1
fi

SOURCES=$(find $srcdir -name "*.c")

for f in $SOURCES; do
	DEFINES=$(sed -n -e 's/.*[ \t(]\+\(HAVE_[[:alnum:]]\+[^ \t);]*\).*/\1/p' \
                         -e 's/.*[ \t(]\+\(ENABLE_[[:alnum:]]\+[^ \t);]*\).*/\1/p' \
                         $f | sort -u)
	[ -z "$DEFINES" ] && continue

	for d in $DEFINES; do
		case $d in
		HAVE_CONFIG_H) continue;;
		*) grep -q "$d\( \|\>\)" $CONFIG || echo $(echo $f | sed 's:'$srcdir/'::') ": $d"

	           ;;
		esac
	done
done
