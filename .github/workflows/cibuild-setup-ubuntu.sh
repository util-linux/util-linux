#!/bin/bash
     
set -ex

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
)

PACKAGES_OPTIONAL=(
	libsystemd-daemon-dev
	libsystemd-journal-dev
)


COMPILER="${COMPILER:?}"
COMPILER_VERSION="${COMPILER_VERSION:?}"
RELEASE="$(lsb_release -cs)"

bash -c "echo 'deb-src http://archive.ubuntu.com/ubuntu/ $RELEASE main restricted universe multiverse' >>/etc/apt/sources.list"

if [[ "$COMPILER" == clang ]]; then
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


apt-get -y update --fix-missing
apt-get -y build-dep util-linux
apt-get -y install "${PACKAGES[@]}"
apt-get -y install  "${PACKAGES_OPTIONAL[@]}" || true
