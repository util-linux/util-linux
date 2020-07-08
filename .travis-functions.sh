#!/bin/bash

#
# .travis-functions.sh:
#   - helper functions to be sourced from .travis.yml
#   - designed to respect travis' environment but testing locally is possible
#
# Variables:
#
#   TS_OPT_<name>_<something>=yes
#          - forces tests/functions.sh:ts_has_option() to return "yes" for
#            variable <something> in test <name>
#
#   TESTS_OPTIONS=
#   TESTS_PARALLEL=
#   TESTS_COMMAND=
#          - overwrites default from tests/Makemodule.am
#
#   Do not use TS_* prefix for any travis or build-system stuff. This prefix is
#   exclusively used by tests/ stuff.
#

if [ ! -f "configure.ac" ]; then
	echo ".travis-functions.sh must be sourced from source dir" >&2
	return 1 || exit 1
fi

## some config settings
# travis docs say we get 1.5 CPUs
MAKE="make -j2"
DUMP_CONFIG_LOG="short"

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
	# memory leaks are ignored here. See https://github.com/karelzak/util-linux/issues/1077
	if ! tmp=$(ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0" $MAKE checkusage 2>&1) || test -n "$tmp"; then
		echo "$tmp"
		echo "make checkusage failed" >&2
		return 1
	fi
}

function check_nonroot
{
	local make_opts="$MAKE_CHECK_OPTS --show-diff --parsable"
	local conf_opts="\
		--disable-use-tty-group \
		--disable-makeinstall-chown \
		--enable-all-programs"

	if [ "$TRAVIS_OS_NAME" != "osx" ]; then
		conf_opts="$conf_opts --enable-asan --enable-ubsan"
		make_opts="$make_opts --memcheck-asan --memcheck-ubsan"
	fi

	if [ "$TRAVIS_OS_NAME" != "osx" -a "$TRAVIS_DIST" != "precise" ]; then
		conf_opts="$conf_opts --enable-werror"
	fi

	xconfigure $conf_opts || return
	$MAKE || return

	osx_prepare_check

	# TESTS_* overwrites default from tests/Makemodule.am
	$MAKE check TESTS_OPTIONS="$make_opts" || return

	make_checkusage || return

	$MAKE install DESTDIR=/tmp/dest || return
}

function check_root
{
	local make_opts="$MAKE_CHECK_OPTS --show-diff"
	local conf_opts="--enable-all-programs"

	if [ "$TRAVIS_OS_NAME" != "osx" ]; then
		conf_opts="$conf_opts --enable-asan --enable-ubsan"
		make_opts="$make_opts --memcheck-asan --memcheck-ubsan"
	fi

	if [ "$TRAVIS_OS_NAME" != "osx" -a "$TRAVIS_DIST" != "precise" ]; then
		conf_opts="$conf_opts --enable-werror"
	fi

	xconfigure $conf_opts || return
	$MAKE || return

	# compile tests only
	$MAKE check-programs || return

	# Modify environment for OSX
	osx_prepare_check

	# TESTS_* overwrites default from tests/Makemodule.am
	sudo -E $MAKE check TESTS_PARALLEL="" TESTS_OPTIONS="$make_opts" || return

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
	local ubuntu_release
	local additional_packages=()
	local clang_version gcc_version

	if [ "$TRAVIS_OS_NAME" = "osx" ]; then
		osx_install_script
		return
	fi

	ubuntu_release=$(lsb_release -cs)

	# install required packages
	sudo bash -c "echo 'deb-src http://archive.ubuntu.com/ubuntu/ $ubuntu_release main restricted universe multiverse' >>/etc/apt/sources.list"

	if [[ "$CC" =~ ^clang-([0-9]+)$ ]]; then
		clang_version=${BASH_REMATCH[1]}
		# the following snippet was borrowed from https://apt.llvm.org/llvm.sh
		wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
		sudo add-apt-repository -y "deb http://apt.llvm.org/$ubuntu_release/   llvm-toolchain-$ubuntu_release-$clang_version  main"
		additional_packages+=(clang-$clang_version llvm-$clang_version)
	elif [[ "$CC" =~ ^gcc-([0-9]+)$ ]]; then
		gcc_version=${BASH_REMATCH[1]}
		sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
		additional_packages+=(gcc-$gcc_version)
	fi

	sudo apt-get -qq update --fix-missing
	sudo apt-get build-dep -y util-linux
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
		"${additional_packages[@]}" \
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
