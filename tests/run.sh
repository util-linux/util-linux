#!/bin/bash

#
# Copyright (C) 2007 Karel Zak <kzak@redhat.com>
#
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

TS_TOPDIR=$(cd $(dirname $0) && pwd)
SUBTESTS=
OPTS=

top_srcdir=
top_builddir=

while [ -n "$1" ]; do
	case "$1" in
	--force)
		OPTS="$OPTS --force"
		;;
	--fake)
		OPTS="$OPTS --fake"
		;;
	--memcheck)
		OPTS="$OPTS --memcheck"
		;;
	--verbose)
		OPTS="$OPTS --verbose"
		;;
	--nonroot)
		if [ $(id -ru) -eq 0 ]; then
			echo "Ignore utils-linux test suite [non-root UID expected]."
			exit 0
		fi
		;;
	--srcdir=*)
		top_srcdir="${1##--srcdir=}"
		;;
	--builddir=*)
		top_builddir="${1##--builddir=}"
		;;
	--*)
		echo "Unknown option $1"
		echo "Usage: "
		echo "  $(basename $0) [options] [<component> ...]"
		echo "Options:"
		echo "  --force           execute demanding tests"
		echo "  --fake            do not run, setup tests only"
		echo "  --memcheck        run with valgrind"
		echo "  --verbose         verbose mode"
		echo "  --nonroot         ignore test suite if user is root"
		echo "  --srcdir=<path>   autotools top source directory"
		echo "  --builddir=<path> autotools top build directory"
		echo
		exit 1
		;;

	*)
		SUBTESTS="$SUBTESTS $1"
		;;
	esac
	shift
done

# For compatibility with autotools is necessary to differentiate between source
# (with test scripts) and build (with temporary files) directories when
# executed by our build-system.
#
# The default is the source tree with this script.
#
if [ -z "$top_srcdir" ]; then
	top_srcdir="$TS_TOPDIR/.."
fi
if [ -z "$top_builddir" ]; then
	top_builddir="$TS_TOPDIR/.."
fi

OPTS="$OPTS --srcdir=$top_srcdir --builddir=$top_builddir"

if [ -n "$SUBTESTS" ]; then
	# selected tests only
	for s in $SUBTESTS; do
		if [ -d "$top_srcdir/tests/ts/$s" ]; then
			co=$(find $top_srcdir/tests/ts/$s -type f -perm /a+x -regex ".*/[^\.~]*" |  sort)
			comps="$comps $co"
		else
			echo "Unknown test component '$s'"
			exit 1
		fi
	done
else
	if [ ! -f "$top_builddir/test_tt" ]; then
		echo "Tests not compiled! Run 'make check' to fix the problem."
		exit 1
	fi

	comps=$(find $top_srcdir/tests/ts/ -type f -perm /a+x -regex ".*/[^\.~]*" |  sort)
fi


unset LIBMOUNT_DEBUG
unset LIBBLKID_DEBUG
unset LIBFDISK_DEBUG

echo
echo "-------------------- util-linux regression tests --------------------"
echo
echo "                    For development purpose only.                    "
echo "                 Don't execute on production system!                 "
echo

res=0
count=0
for ts in $comps; do
	$ts "$OPTS"
	res=$(( $res + $? ))
	count=$(( $count + 1 ))
done

echo
echo "---------------------------------------------------------------------"
if [ $res -eq 0 ]; then
	echo "  All $count tests PASSED"
	res=0
else
	echo "  $res tests of $count FAILED"
	res=1
fi
echo "---------------------------------------------------------------------"
exit $res
