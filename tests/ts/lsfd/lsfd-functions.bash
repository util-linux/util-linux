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

# The exit-status used in a test target.
readonly ENOSYS=17
readonly EPERM=18
readonly ENOPROTOOPT=19
readonly EPROTONOSUPPORT=20
readonly EACCESS=21

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

lsfd_strip_type_stream()
{
    # lsfd changes the output of NAME column for a unix stream socket
    # whether the kernel reports it is a "UNIX-STREAM" socket or a
    # "UNIX" socket. For "UNIX", lsfd appends "type=stream" to the
    # NAME column. Let's delete the appended string before comparing.
    sed -e 's/ type=stream//'
}

lsfd_make_state_connected()
{
    # Newer kernels report the states of unix dgram sockets created by
    # sockerpair(2) are "connected" via /proc/net/unix though Older
    # kernels report "unconnected".
    #
    # Newer kernels report the states of unix dgram sockets already
    # connect(2)'ed are "connected", too.
    #
    # https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=83301b5367a98c17ec0d76c7bc0ccdc3c7e7ad6d
    #
    # This rewriting adjusts the output of lsfd running on older kernels
    # to that on newer kernels.
    sed -e 's/state=unconnected/state=connected/'
}
