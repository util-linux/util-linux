#!/bin/bash
 
PHASES=(${@:-CONFIGURE MAKE INSTALL CHECK DISTCHECK})
COMPILER="${COMPILER:?}"
COMPILER_VERSION="${COMPILER_VERSION}"
CFLAGS=(-O1 -g)
CXXFLAGS=(-O1 -g)

if [[ "$COMPILER" == clang ]]; then
    CC="clang${COMPILER_VERSION:+-$COMPILER_VERSION}"
    CXX="clang++${COMPILER_VERSION:+-$COMPILER_VERSION}"
elif [[ "$COMPILER" == gcc ]]; then
    CC="gcc${COMPILER_VERSION:+-$COMPILER_VERSION}"
    CXX="g++${COMPILER_VERSION:+-$COMPILER_VERSION}"
fi

set -ex

for phase in "${PHASES[@]}"; do
    case $phase in
    CONFIGURE)
        opts=(
            --disable-use-tty-group
            --disable-makeinstall-chown
            --enable-all-programs
            --without-python
            --enable-werror
        )

        if [[ "$SANITIZE" == "yes" ]]; then
            opts+=(--enable-asan --enable-ubsan)
            CFLAGS+=(-fno-omit-frame-pointer)
            CXXFLAGS+=(-fno-omit-frame-pointer)
        fi

        if [[ "$COMPILER" == clang* && "$SANITIZE" == "yes" ]]; then
            opts+=(--enable-fuzzing-engine)
            CFLAGS+=(-shared-libasan)
            CXXFLAGS+=(-shared-libasan)
        fi

        sudo -E git clean -xdf

        ./autogen.sh
        CC="$CC" CXX="$CXX" CFLAGS="${CFLAGS[@]}" CXXFLAGS="${CXXFLAGS[@]}" ./configure "${opts[@]}"
        ;;
    MAKE)
        make -j
        make -j check-programs
        ;;
    INSTALL)
        make install DESTDIR=/tmp/dest
        ;;
    CHECK)
        if [[ "$SANITIZE" == "yes" ]]; then
            # All the following black magic is to make test/eject/umount work, since
            # eject execl()s the uninstrumented /bin/umount binary, which confuses
            # ASan. The workaround for this is to set $LD_PRELOAD to the ASan's
            # runtime DSO, which works well with gcc without any additional hassle.
            # However, since clang, by default, links ASan statically, we need to
            # explicitly state we want dynamic linking (see -shared-libasan above).
            # That, however, introduces another issue - clang's ASan runtime is in
            # a non-standard path, so all binaries compiled in such way refuse
            # to start. That's what the following blob of code is for - it detects
            # the ASan's runtime path and adds the respective directory to
            # the dynamic linker cache.
            #
            # The actual $LD_PRELOAD sheanigans are done directly in
            # tests/ts/eject/umount.
            asan_rt_name="$(ldd ./kill | awk '/lib.+asan.*.so/ {print $1; exit}')"
            asan_rt_path="$($CC --print-file-name "$asan_rt_name")"
            echo "Detected ASan runtime: $asan_rt_name ($asan_rt_path)"
            if [[ -z "$asan_rt_name" || -z "$asan_rt_path" ]]; then
                echo >&2 "Couldn't detect ASan runtime, can't continue"
                exit 1
            fi

            if [[ "$COMPILER" == clang* ]]; then
                mkdir -p /etc/ld.so.conf.d/
                echo "${asan_rt_path%/*}" > /etc/ld.so.conf.d/99-clang-libasan.conf
                ldconfig
            fi
        fi

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
