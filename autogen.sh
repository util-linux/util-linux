#!/bin/sh

#
# Helps generate autoconf/automake stuff, when code is checked out from SCM.
#
# Copyright (C) 2006-2010 - Karel Zak <kzak@redhat.com>
#

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

THEDIR=`pwd`
cd $srcdir
DIE=0

# provide simple gettext backward compatibility
autopoint_fun ()
{
	# we have to deal with set -e ...
	ret="0"

	# check against this hardcoded set of alternative gettext versions
	gt_ver=`gettext --version |\
		sed -n -e 's/.* \(0\.17\|0\.18\|0\.18\.[1-2]\)$/\1/p'`

	if [ -n "$gt_ver" ]; then
		echo "warning: forcing autopoint to use old gettext $gt_ver"
		rm -f configure.ac.autogenbak
		sed -i.autogenbak configure.ac \
			-e "s/\(AM_GNU_GETTEXT_VERSION\).*/\1([$gt_ver])/"
	fi

	autopoint "$@" || ret=$?

	if [ -n "$gt_ver" ]; then
		mv configure.ac.autogenbak configure.ac
	fi

	return $ret
}

test -f sys-utils/mount.c || {
	echo
	echo "You must run this script in the top-level util-linux directory."
	echo
	DIE=1
}

(autopoint --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have autopoint installed to generate the util-linux build system."
        echo "The autopoint command is part of the GNU gettext package."
	echo
        DIE=1
}
(autoconf --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoconf installed to generate the util-linux build system."
	echo
	DIE=1
}
(autoheader --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoheader installed to generate the util-linux build system."
	echo "The autoheader command is part of the GNU autoconf package."
	echo
	DIE=1
}

if ! (bison --version) < /dev/null > /dev/null 2>&1; then
	echo
	echo "You must have bison installed to build the util-linux."
	echo
	DIE=1
else
	lexver=$(bison --version | awk '/^bison \(GNU [Bb]ison\)/ { print $4 }')
	case "$lexver" in
		[2-9].*)
			;;
		*)
			echo
			echo "You must have bison version >= 2.x, but you have $lexver."
			echo
			DIE=1
			;;
	esac
fi

LIBTOOLIZE=libtoolize
case `uname` in Darwin*) LIBTOOLIZE=glibtoolize ;; esac
if ! ($LIBTOOLIZE --version) < /dev/null > /dev/null 2>&1; then
	echo
	echo "You must have libtool-2 installed to generate the util-linux build system."
	echo
	DIE=1
else
	ltver=$($LIBTOOLIZE --version | awk '/^[g]*libtoolize/ { print $4 }')
	ltver=${ltver:-"none"}
	test ${ltver##2.} = "$ltver" && {
		echo
		echo "You must have libtool version >= 2.x.x, but you have $ltver."
		echo
		DIE=1
	}
fi

(automake --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have automake installed to generate the util-linux build system."
	echo
	DIE=1
}

if test "$DIE" -eq 1; then
	exit 1
fi

echo
echo "Generating build-system with:"
echo "   autopoint:  $(autopoint --version | head -1)"
echo "   aclocal:    $(aclocal --version | head -1)"
echo "   autoconf:   $(autoconf --version | head -1)"
echo "   autoheader: $(autoheader --version | head -1)"
echo "   automake:   $(automake --version | head -1)"
echo "   libtoolize: $($LIBTOOLIZE --version | head -1)"
echo "   bison:      $(bison --version | head -1)"

rm -rf autom4te.cache

set -e
po/update-potfiles
autopoint_fun --force $AP_OPTS
if ! grep -q datarootdir po/Makefile.in.in; then
	echo autopoint does not honor dataroot variable, patching.
	sed -i -e 's/^datadir *=\(.*\)/datarootdir = @datarootdir@\
datadir = @datadir@/g' po/Makefile.in.in
fi
$LIBTOOLIZE --force $LT_OPTS
aclocal -I m4 $AL_OPTS
autoconf $AC_OPTS
autoheader $AH_OPTS

automake --add-missing $AM_OPTS

cd "$THEDIR"

echo
echo "Now type '$srcdir/configure' and 'make' to compile."
echo


