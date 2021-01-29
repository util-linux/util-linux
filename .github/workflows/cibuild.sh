#!/bin/bash
 
PHASES=(${@:-CONFIGURE MAKE INSTALL CHECK DISTCHECK})
COMPILER="${COMPILER:?}"
COMPILER_VERSION="${COMPILER_VERSION}"

if [[ "$COMPILER" == clang ]]; then
    CC="clang${COMPILER_VERSION:+-$COMPILER_VERSION}"
    CXX="clang++${COMPILER_VERSION:+-$COMPILER_VERSION}"
elif [[ "$COMPILER" == gcc ]]; then
    CC="gcc${COMPILER_VERSION:+-$COMPILER_VERSION}"
    CXX="g++${COMPILER_VERSION:+-$COMPILER_VERSION}"
fi

set -ex

export CC="$CC"
export CXX="$CXX"

for phase in "${PHASES[@]}"; do
    case $phase in
	CONFIGURE)
		opts="--disable-use-tty-group \
			--disable-makeinstall-chown \
			--enable-all-programs \
			--enable-asan \
			--enable-ubsan \
			--without-python \
			--enable-werror"

		if [[ "$COMPILER" == clang* ]]; then
			opts="$opts --enable-fuzzing-engine"
		fi

		sudo -E git clean -xdf

		./autogen.sh
		CC=$CC CXX=$CXX ./configure $opts
		;;
        MAKE)
		make -j
		make -j check-programs
		;;
	INSTALL)
		make install DESTDIR=/tmp/dest
		;;
	CHECK)
		./tests/run.sh --show-diff
		;;
	DISTCHECK)
		make distcheck
		;;
	
        *)
            echo >&2 "Unknown phase '$phase'"
            exit 1
    esac
done
	

