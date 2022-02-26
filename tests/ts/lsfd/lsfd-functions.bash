#!/bin/bash
#
# Copyright (C) 2021 Masatake YAMATO <yamato@redhat.com>
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

function lsfd_wait_for_pausing {
	ts_check_prog "sleep"

	local PID=$1
	until [[ $(ps --no-headers -ostat "${PID}") =~ S.* ]]; do
		sleep 1
	done
}

function lsfd_compare_dev {
    local LSFD=$1
    local FILE=$2
    local EXPR=$3

    ts_check_prog "grep"
    ts_check_prog "expr"
    ts_check_prog "stat"

    local DEV=$("${LSFD}" --raw -n -o DEV -Q "${EXPR}")
    echo 'DEV[RUN]:' $?
    local MAJ=${DEV%:*}
    local MIN=${DEV#*:}
    local DEVNUM=$(( ( MAJ << 8 ) + MIN ))
    local STAT_DEVNUM=$(stat -c "%d" "$FILE")
    echo 'STAT[RUN]:' $?
    if [ "${DEVNUM}" == "${STAT_DEVNUM}" ]; then
	echo 'DEVNUM[STR]:' 0
    else
	echo 'DEVNUM[STR]:' 1
	# Print more information for debugging
	echo 'DEV:' "${DEV}"
	echo 'MAJ:MIN' "${MAJ}:${MIN}"
	echo 'DEVNUM:' "${DEVNUM}"
	echo 'STAT_DEVNUM:' "${STAT_DEVNUM}"
    fi
}
