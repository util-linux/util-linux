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

# some config settings
MAKE="make -j4"
DUMP_CONFIG_LOG="short"

# We could test (exotic) out-of-tree build dirs using relative or abs paths.
# After sourcing this script we are living in build dir. Tasks for source dir
# have to use $SOURCE_DIR.
SOURCE_DIR="."
BUILD_DIR="."
CONFIGURE="$SOURCE_DIR/configure"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || return 1 || exit 1

function configure_travis
{
	"$CONFIGURE" "$@"
	err=$?
	if [ "$DUMP_CONFIG_LOG" = "short" ]; then
		grep -B1 -A10000 "^## Output variables" config.log | grep -v "_FALSE="
	elif [ "$DUMP_CONFIG_LOG" = "full" ]; then
		cat config.log
	fi
	return $err
}

function check_nonroot
{
	local opts="$MAKE_CHECK_OPTS"

	configure_travis \
		--disable-use-tty-group \
		--with-python \
		--enable-all-programs \
		--enable-gtk-doc \
		|| return
	$MAKE || return
	$MAKE check TS_OPTS="$opts" || return
	$MAKE install DESTDIR=/tmp/dest || return
}

function check_root
{
	local opts="$MAKE_CHECK_OPTS --parallel=1"

	configure_travis \
		--with-python \
		--enable-all-programs \
		|| return
	$MAKE || return
	$MAKE check TS_COMMAND="true" || return
	sudo -E $MAKE check TS_OPTS="$opts" || return
	sudo $MAKE install || return
}

function check_dist
{
	configure_travis \
		|| return
	$MAKE distcheck || return
}

function travis_install_script
{
	# install some packages from Ubuntu's default sources
	sudo apt-get -qq update || return
	sudo apt-get install -qq >/dev/null \
		bc \
		dnsutils \
		libcap-ng-dev \
		libpam-dev \
		libudev-dev \
		gtk-doc-tools \
		ntp \
		|| return

	# install/upgrade custom stuff from non-official sources
	sudo add-apt-repository -y ppa:malcscott/socat || return
	sudo apt-get -qq update || return
	sudo apt-get install -qq >/dev/null \
		socat \
		|| return
}

function travis_before_script
{
	pushd "$SOURCE_DIR" || return
	set -o xtrace

	./autogen.sh
	ret=$?

	# workaround for broken pylibmount install relink
	[ $ret -eq 0 ] && \
		sed -i 's/\(link_all_deplibs\)=no/\1=unknown/' ./configure

	set +o xtrace
	popd
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
	diff_dir=$(find -type d -a -name "diff" | grep "tests/diff" | head -n 1)
	if [ -d "$diff_dir" ]; then
		tmp=$(find "$diff_dir" -type f | sort)
		echo -en "dump test diffs:\n${tmp}\n"
		echo "$tmp" | xargs -r cat
	fi
}
