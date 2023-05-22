#!/bin/sh

sdk="$(realpath $1)"

STAGING_DIR="$(echo "$sdk"/staging_dir/toolchain-*)"

. "$STAGING_DIR/info.mk"

PATH="$sdk/staging_dir/host/bin:$PATH"
CC="$STAGING_DIR/bin/${TARGET_CROSS}gcc"
BISON_PKGDATADIR="$sdk/staging_dir/host/share/bison"
M4="$sdk/staging_dir/host/bin/m4"
HOST_TRIPLET="$("$CC" -dumpmachine)"

echo "Building for $HOST_TRIPLET from $sdk"

export STAGING_DIR PATH CC BISON_PKGDATADIR M4 HOST_TRIPLET
