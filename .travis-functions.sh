#!/bin/bash

#
# .travis-functions.sh:
#   - helper functions to be sourced from .travis.yml
#   - designed to respect travis' environment but testing locally is possible
#

if [ ! -f "configure.ac" ]; then
	echo ".travis-functions.sh must be sourced from source dir" >&2
	return 1 || exit 1
fi

## some config settings
# travis docs say we get 1.5 CPUs
MAKE="make -j2"
DUMP_CONFIG_LOG="short"
export TS_OPT_parsable="yes"

# workaround ugly warning on travis OSX,
# see https://github.com/direnv/direnv/issues/210
shell_session_update() { :; }

function xconfigure
{
	which "$CC"
	"$CC" --version

	./configure "$@" $OSX_CONFOPTS
	err=$?
	if [ "$DUMP_CONFIG_LOG" = "short" ]; then
		grep -B1 -A10000 "^## Output variables" config.log | grep -v "_FALSE="
	elif [ "$DUMP_CONFIG_LOG" = "full" ]; then
		cat config.log
	fi
	return $err
}

# TODO: integrate checkusage into our regular tests and remove this function
function make_checkusage
{
	local tmp
	if ! tmp=$($MAKE checkusage 2>&1) || test -n "$tmp"; then
		echo "$tmp"
		echo "make checkusage failed" >&2
		return 1
	fi
}

function check_nonroot
{
	local opts="$MAKE_CHECK_OPTS --show-diff"

	xconfigure \
		--disable-use-tty-group \
		--disable-makeinstall-chown \
		--enable-all-programs \
		|| return
	$MAKE || return

	osx_prepare_check
	$MAKE check TS_OPTS="$opts" || return

	make_checkusage || return

	$MAKE install DESTDIR=/tmp/dest || return
}

function check_root
{
	local opts="$MAKE_CHECK_OPTS --show-diff"

	xconfigure \
		--enable-all-programs \
		|| return
	$MAKE || return

	$MAKE check TS_COMMAND="true" || return
	osx_prepare_check
	sudo -E $MAKE check TS_OPTS="$opts" || return

	# root on osx has not enough permission for make install ;)
	[ "$TRAVIS_OS_NAME" = "osx" ] && return

	# keep PATH to make sure sudo would find $CC
	sudo env "PATH=$PATH" $MAKE install || return
}

function check_dist
{
	xconfigure \
		|| return
	$MAKE distcheck || return
}

function travis_install_script
{
	if [ "$TRAVIS_OS_NAME" = "osx" ]; then
		osx_install_script
		return
	fi

	# install required packages
	sudo apt-get -qq update --fix-missing
	sudo apt-get install -qq >/dev/null \
		bc \
		btrfs-tools \
		dnsutils \
		libcap-ng-dev \
		libncursesw5-dev \
		libpam-dev \
		libudev-dev \
		gtk-doc-tools \
		mdadm \
		ntp \
		socat \
		|| return

	# install only if available (e.g. Ubuntu Trusty)
	sudo apt-get install -qq >/dev/null \
		libsystemd-daemon-dev \
		libsystemd-journal-dev \
		|| true
}

function osx_install_script
{
	brew update >/dev/null

	brew install gettext ncurses socat xz
	brew link --force gettext
	brew link --force ncurses

	OSX_CONFOPTS="
		--disable-ipcrm \
		--disable-ipcs \
	"

	# workaround: glibtoolize could not find sed
	export SED="sed"
}

function osx_prepare_check
{
	[ "$TRAVIS_OS_NAME" = "osx" ] || return 0

	# these ones only need to be gnu for our test-suite
	brew install coreutils findutils gnu-tar gnu-sed

	# symlink minimally needed gnu commands into PATH
	mkdir ~/bin
	for cmd in readlink seq timeout truncate find xargs tar sed; do
		ln -s /usr/local/bin/g$cmd $HOME/bin/$cmd
	done
	hash -r

	export TS_OPT_col_multibyte_known_fail=yes
	export TS_OPT_colcrt_regressions_known_fail=yes
	export TS_OPT_column_invalid_multibyte_known_fail=yes
}

function travis_before_script
{
	set -o xtrace

	./autogen.sh
	ret=$?

	set +o xtrace
	return $ret
}

function travis_script
{
	local ret
	set -o xtrace

	case "$MAKE_CHECK" in
	nonroot)
		check_nonroot
		;;
	root)
		check_root
		;;
	dist)
		check_dist
		;;
	*)
		echo "error, check environment (travis.yml)" >&2
		false
		;;
	esac

	# We exit here with case-switch return value!
	ret=$?
	set +o xtrace
	return $ret
}

function travis_after_script
{
	local diff_dir
	local tmp

	# find diff dir from check as well as from distcheck
	diff_dir=$(find . -type d -name "diff" | grep "tests/diff" | head -n 1)
	if [ -d "$diff_dir" ]; then
		tmp=$(find "$diff_dir" -type f | sort)
		echo -en "dump test diffs:\n${tmp}\n"
		echo "$tmp" | xargs cat
	fi
}
