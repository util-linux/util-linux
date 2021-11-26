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
    local FINDMNT=$2
    local EXPR=$3

    ts_check_prog "grep"

    local MNTID=$("${LSFD}" --raw -n -o MNTID -Q "${EXPR}")
    echo 'MNTID[RUN]:' $?
    local DEV=$("${LSFD}" --raw -n -o DEV -Q "${EXPR}")
    echo 'DEV[RUN]:' $?
    # "stat -c" %d or "stat -c %D" can be used here instead of "findmnt".
    # "stat" just prints a device id.
    # Unlike "stat", "findmnt" can print the major part and minor part
    # for a given device separately.
    # We can save the code for extracting the major part and minor part
    # if we use findmnt.
    local FINDMNT_MNTID_DEV=$("${FINDMNT}" --raw -n -o ID,MAJ:MIN | grep "^${MNTID}")
    echo 'FINDMNT[RUN]:' $?
    [ "${MNTID} ${DEV}" == "${FINDMNT_MNTID_DEV}" ]
    echo 'DEV[STR]:' $?
}
