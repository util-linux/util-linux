#!/bin/sh

sdk="$(realpath $1)"

STAGING_DIR="$(echo "$sdk"/staging_dir/toolchain-*)"

. "$STAGING_DIR/info.mk"

PATH="$sdk/staging_dir/host/bin:$PATH"
LD_LIBRARY_PATH="$STAGING_DIR/lib"
CC="$STAGING_DIR/bin/${TARGET_CROSS}gcc"
DYNAMIC_LINKER="$(echo "$STAGING_DIR"/lib/ld-musl-*)"
BISON_PKGDATADIR="$sdk/staging_dir/host/share/bison"
M4="$sdk/staging_dir/host/bin/m4"
HOST_TRIPLET="$("$CC" -dumpmachine)"

echo "Building for $HOST_TRIPLET from $sdk"

export STAGING_DIR PATH LD_LIBRARY_PATH CC DYNAMIC_LINKER BISON_PKGDATADIR M4 HOST_TRIPLET
