#!/usr/bin/env bash
#
# Copyright (C) 2026 Karel Zak <kzak@redhat.com>
#
# This file is part of util-linux.
#
# This file is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This file is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#

# Duplicate lines have been observed in lslocks output. /proc/locks is
# a seq_file whose iterator addresses entries by their position in the
# per-CPU file_lock_list; see locks_start() in <kernel>/fs/locks.c. The
# list locks are dropped at the end of every chunk the kernel hands out
# (1KiB, as stdio reads /proc/locks in st_blksize steps), and the next
# read() re-walks the lists from the beginning to skip that many entries
# again. A lock released before the current position shifts all the
# following entries down, and a record is reported twice. lslocks cannot
# avoid this.
#
# Remove duplicates to avoid false test failures, but warn about it.
# The queries have to be specific enough to never report two identical
# lines on their own; add MODE to tell a read and a write lock on the
# same file apart.
LSLOCKS_DUP_FOUND=""

do_lslocks()
{
    local raw rc
    raw=$("$TS_CMD_LSLOCKS" "$@")
    rc=$?
    if [ -z "$raw" ]; then
	return $rc
    fi
    local dedup
    dedup=$(echo "$raw" | uniq)
    if [ "$raw" != "$dedup" ]; then
	LSLOCKS_DUP_FOUND="yes"
    fi
    echo "$dedup"
    return $rc
}
