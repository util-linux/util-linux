#!/usr/bin/env bash

set -ex

export LC_CTYPE=C.UTF-8

export CC=${CC:-clang}
export CXX=${CXX:-clang++}
export LIB_FUZZING_ENGINE=${LIB_FUZZING_ENGINE:--fsanitize=fuzzer}

SANITIZER=${SANITIZER:-address -fsanitize-address-use-after-scope}
flags="-O1 -fno-omit-frame-pointer -gline-tables-only -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -fsanitize=$SANITIZER -fsanitize=fuzzer-no-link"

export CFLAGS=${CFLAGS:-$flags}
export CXXFLAGS=${CXXFLAGS:-$flags}

export OUT=${OUT:-$(pwd)/out}
mkdir -p $OUT

./autogen.sh
./configure --disable-all-programs --enable-fuzzing-engine --enable-libmount --enable-libblkid
make -j$(nproc) V=1 check-programs

find . -maxdepth 1 -type f -executable -name "test_*_fuzz" -exec mv {} $OUT \;
find . -type f -name "fuzz-*.dict" -exec cp {} $OUT \;
