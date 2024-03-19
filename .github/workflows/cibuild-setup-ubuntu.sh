#!/bin/bash
     
set -ex

export DEBIAN_FRONTEND=noninteractive

apt-get -y update --fix-missing

# Xenial uses btrfs-tools, but since Focal it's btrfs-progs
#
PACKAGES=(
	bc
	btrfs-progs
	dnsutils
	libcap-ng-dev
	libncursesw5-dev
	libpam-dev
	libudev-dev
	gtk-doc-tools
	mdadm
	ntp
	socat
	asciidoctor
	meson
	lcov
	gpg-agent
	git
	squashfs-tools
	iproute2
	dmsetup
	python3-dev
	gawk
	bison
	flex
	sqlite3
	libsqlite3-dev
)

PACKAGES_OPTIONAL=(
	libsystemd-daemon-dev
	libsystemd-journal-dev
)

# scsi_debug
if [[ "$QEMU_USER" != "1" ]]; then
	MODULES_PACKAGE="linux-modules-extra-$(uname -r)"
	# may not exist anymore
	if APT_CACHE_OUTPUT=$(apt-cache show "$MODULES_PACKAGE") && [[ -n "$APT_CACHE_OUTPUT" ]]; then
		PACKAGES+=("$MODULES_PACKAGE")
	fi
fi

if [[ "$TRANSLATE_MANPAGES" == "yes" ]];then
	PACKAGES+=(po4a)
fi

apt install -y lsb-release software-properties-common

COMPILER="${COMPILER:?}"
RELEASE="$(lsb_release -cs)"

bash -c "echo 'deb-src http://archive.ubuntu.com/ubuntu/ $RELEASE main restricted universe multiverse' >>/etc/apt/sources.list"

# cov-build fails to compile util-linux when CC is set to gcc-*
# so let's just install and use the default compiler
if [[ "$COMPILER_VERSION" == "" ]]; then
    if [[ "$COMPILER" != "none" ]]; then
	PACKAGES+=("$COMPILER")
    fi
elif [[ "$COMPILER" == clang ]]; then
    # Latest LLVM stack deb packages provided by https://apt.llvm.org/
    # Following snippet was borrowed from https://apt.llvm.org/llvm.sh
    wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
    add-apt-repository -y "deb http://apt.llvm.org/$RELEASE/   llvm-toolchain-$RELEASE-$COMPILER_VERSION  main"
    PACKAGES+=(clang-$COMPILER_VERSION lldb-$COMPILER_VERSION lld-$COMPILER_VERSION clangd-$COMPILER_VERSION)
elif [[ "$COMPILER" == gcc ]]; then
    # Latest gcc stack deb packages provided by
    # https://launchpad.net/~ubuntu-toolchain-r/+archive/ubuntu/test
    add-apt-repository -y ppa:ubuntu-toolchain-r/test
    PACKAGES+=(gcc-$COMPILER_VERSION)
else
    fatal "Unknown compiler: $COMPILER"
fi

# ASAN can crash with the new default of =32
sysctl --write vm.mmap_rnd_bits=28


apt-get -y update --fix-missing
apt-get -y build-dep util-linux
apt-get -y install "${PACKAGES[@]}"
apt-get -y install  "${PACKAGES_OPTIONAL[@]}" || true
