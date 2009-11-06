#!/bin/sh

#
# Helps generate autoconf/automake stuff, when code is checked out from SCM.
#
# Copyright (C) 2006-2009 - Karel Zak <kzak@redhat.com>
#

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

THEDIR=`pwd`
cd $srcdir
DIE=0
HAS_GTKDOC=1

(autopoint --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have autopoint installed to generate util-linux-ng build system.."
        echo "Download the appropriate package for your distribution,"
        echo "or see http://www.gnu.org/software/gettext"
        DIE=1
}
(autoconf --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoconf installed to generate util-linux-ng build system."
	echo
	echo "Download the appropriate package for your distribution,"
	echo "or see http://www.gnu.org/software/autoconf"
	DIE=1
}

#(libtool --version) < /dev/null > /dev/null 2>&1 || {
#	echo
#	echo "You must have libtool-2 installed to generate util-linux-ng build system."
#	echo "Download the appropriate package for your distribution,"
#	echo "or see http://www.gnu.org/software/libtool"
#	DIE=1
#}

(automake --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have automake installed to generate util-linux-ng build system."
	echo 
	echo "Download the appropriate package for your distribution,"
	echo "or see http://www.gnu.org/software/automake"
	DIE=1
}
(autoheader --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoheader installed to generate util-linux-ng build system."
	echo 
	echo "Download the appropriate package for your distribution,"
	echo "or see http://www.gnu.org/software/autoheader"
	DIE=1
}

if test "$DIE" -eq 1; then
	exit 1
fi

test -f mount/mount.c || {
	echo "You must run this script in the top-level util-linux-ng directory"
	exit 1
}

#ltver=$(libtoolize --version | awk '/^libtoolize/ { print $4 }')
#test ${ltver##2.} == "$ltver" && {
#	echo "You must have libtool version >= 2.x.x, but you have $ltver."
#	exit 1
#}

echo
echo "Generate build-system by:"
echo "   autopoint:  $(autopoint --version | head -1)"
echo "   aclocal:    $(aclocal --version | head -1)"
echo "   autoconf:   $(autoconf --version | head -1)"
echo "   autoheader: $(autoheader --version | head -1)"
echo "   automake:   $(automake --version | head -1)"
#echo "   libtoolize: $(libtoolize --version | head -1)"

set -e
autopoint --force $AP_OPTS
#libtoolize --force --copy $LT_OPTS
aclocal -I m4 $AL_OPTS
autoconf $AC_OPTS
autoheader $AH_OPTS

automake --add-missing $AM_OPTS

cd $THEDIR

echo
echo "Now type '$srcdir/configure' and 'make' to compile."
echo


