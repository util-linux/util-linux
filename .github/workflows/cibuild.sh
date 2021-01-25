#!/bin/bash
 
PHASES=(${@:-CONFIGURE MAKE INSTALL CHECK DISTCHECK})
COMPILER="${COMPILER:?}"

set -ex

for phase in "${PHASES[@]}"; do
    case $phase in
	CONFIGURE)
		opts = "--disable-use-tty-group \
			--disable-makeinstall-chown \
			--enable-all-programs \
			--enable-asan \
			--enable-ubsan \
			--enable-werror"

		if [[ "$COMPILER" == clang ]]; then
			opts="$opts --enable-fuzzing-engine"
		fi

		echo "## CONFIGURE: git-clean"
		sudo -E git clean -xdf

		echo "## CONFIGURE: autogen.sh"
		./autogen.sh

		echo "## CONFIGURE: $opts --"
		./configure $opts
		;;
        MAKE)
		echo "## MAKE"
		make -j V=1
		make -j check-programs V=1
		;;
	INSTALL)
		echo "## MAKE INSTALL"
		make install DESTDIR=/tmp/dest
		;;
	CHECK)
		echo "## MAKE CHECK"
		./tests/run.h --show-diff
		;;
	DISTCHECK)
		echo "## MAKE DISTCHECK"
		make distcheck
		;;
	
        *)
            echo >&2 "Unknown phase '$phase'"
            exit 1
    esac
done
	

