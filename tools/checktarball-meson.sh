#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2025 Karel Zak <kzak@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    echo "Usage: $0 TARBALL"
    echo "Compare meson.build files in the current Git with those in the specified tarball."
    exit 0
fi

TARBALL="$1"

if [ -z "$TARBALL" ]; then
    echo "Error: No tarball specified."
    exit 1
elif [ ! -f "$TARBALL" ]; then
    echo "Error: Tarball '$TARBALL' not found."
    exit 1
fi

TAR_DIRNAME=$(tar -tf "$TARBALL" | head -1 | cut -d'/' -f1)
TAR_MESON_FILES=$(tar -tf "$TARBALL" | grep 'meson.build$' | sed "s|^$TAR_DIRNAME/||")
GIT_MESON_FILES=$(git ls-files | grep 'meson.build$')

MISSING_FILES=()
for file in $GIT_MESON_FILES; do
    if ! grep -qx "$file" <<< "$TAR_MESON_FILES"; then
        MISSING_FILES+=("$file")
    fi
done

if [ ${#MISSING_FILES[@]} -gt 0 ]; then
    echo "The following meson.build files are missing in the tarball:"
    for file in "${MISSING_FILES[@]}"; do
        echo "$file"
    done
    exit 1
fi
