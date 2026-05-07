#!/usr/bin/env bash
#
# Copyright (C) 2022,2026 Masatake YAMATO <yamato@redhat.com>
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
ts_check_test_command "$TS_CMD_LSFD"
ts_check_test_command "$TS_HELPER_MKFDS"
ts_check_native_byteorder

udp_common()
{
    local ip=$1
    local lite=$2
    local index=$lite

    if [[ "$ip" == 4 ]]; then
	ip=
    else
	ts_check_ipv6
	ts_skip_docker

	index=$((index + 2))
    fi

    ts_cd "$TS_OUTDIR"

    local PID=
    local FDS=3
    local FDC=4
    local -a EXPR=(
	'(TYPE == "UDP") and (FD >= 3) and (FD <= 4)'
	'(TYPE == "UDP-Lite") and (FD >= 3) and (FD <= 4)'
	'(TYPE == "UDPv6") and (FD >= 3) and (FD <= 4)'
	'(TYPE == "UDPLITEv6") and (FD >= 3) and (FD <= 4)'
    )
    local -a EXPR_server=(
	'(TYPE == "UDP") and (FD == 3)'
	'(TYPE == "UDP-Lite") and (FD == 3)'
	'(TYPE == "UDPv6") and (FD == 3)'
	'(TYPE == "UDPLITEv6") and (FD == 3)'
    )
    local -a EXPR_client=(
	'(TYPE == "UDP") and (FD == 4)'
	'(TYPE == "UDP-Lite") and (FD == 4)'
	'(TYPE == "UDPv6") and (FD == 4)'
	'(TYPE == "UDPLITEv6") and (FD == 4)'
    )
    local -a COLNS=(
	'UDP'
	'UDPLite'
    )
    local NAME=
    local LADDR=
    local LPORT=

    coproc MKFDS { "$TS_HELPER_MKFDS" udp$ip $FDS $FDC \
				      server-port=56789 \
				      client-port=45678 \
				      lite=$lite; }
    if read -r -u "${MKFDS[0]}" PID; then
	${TS_CMD_LSFD} -n \
		       -o ASSOC,TYPE,STTYPE,NAME,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.LADDR,${COLNS[$lite]}.LPORT,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT \
		       -p "${PID}" -Q "${EXPR[$index]}"
	echo "ASSOC,TYPE,STTYPE,NAME,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.LADDR,${COLNS[$lite]}.LPORT,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT": $?

	echo DONE >&"${MKFDS[1]}"
    fi
    wait "${MKFDS_PID}"

    coproc MKFDS { "$TS_HELPER_MKFDS" udp$ip $FDS $FDC \
				      server-port=56789 \
				      client-port=45678 \
				      server-do-bind=no \
				      lite=$lite; }
    if read -r -u "${MKFDS[0]}" PID; then
	${TS_CMD_LSFD} -n \
		       -o ASSOC,TYPE,STTYPE,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.LADDR,${COLNS[$lite]}.LPORT,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT \
		       -p "${PID}" -Q "${EXPR_server[$index]}"
	echo "ASSOC,TYPE,STTYPE,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.LADDR,${COLNS[$lite]}.LPORT,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT": $?

	NAME=$(${TS_CMD_LSFD} -n \
			      --raw \
			      -o NAME \
			      -p "${PID}" -Q "${EXPR_server[$index]}")
	if [[ "$NAME" =~ ^socket:\[[[:digit:]]+\]$ ]]; then
	    echo "NAME pattern match: OK"
	else
	    echo "NAME pattern match: FAILED (NAME=$NAME)"
	fi

	echo DONE >&"${MKFDS[1]}"
    fi
    wait "${MKFDS_PID}"

    coproc MKFDS { "$TS_HELPER_MKFDS" udp$ip $FDS $FDC \
				      server-port=56789 \
				      client-port=45678 \
				      client-do-bind=no \
				      lite=$lite; }
    if read -r -u "${MKFDS[0]}" PID; then
	${TS_CMD_LSFD} -n \
		       -o ASSOC,TYPE,STTYPE,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT \
		       -p "${PID}" -Q "${EXPR_client[$index]}"
	echo "ASSOC,TYPE,STTYPE,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT": $?

	LADDR=$(${TS_CMD_LSFD} -n \
			       --raw \
			       -o ${COLNS[$lite]}.LADDR \
			       -p "${PID}" -Q "${EXPR_client[$index]}")
	LPORT=$(${TS_CMD_LSFD} -n \
			       --raw \
			       -o ${COLNS[$lite]}.LPORT \
			       -p "${PID}" -Q "${EXPR_client[$index]}")

	if [[ "$ip" == "" && 127.0.0.1:$LPORT == "${LADDR}" ]]; then
	    echo "LADDR/LPORT pattern match: OK"
	elif [[ "$ip" == 6 && '[::1]':$LPORT == "${LADDR}" ]]; then
	    echo "LADDR/LPORT pattern match: OK"
	else
	    echo "LADDR/LPORT pattern match: FAILED (LADDR=$LADDR, LPORT=$LPORT)"
	fi

	echo DONE >&"${MKFDS[1]}"
    fi
    wait "${MKFDS_PID}"

    coproc MKFDS { "$TS_HELPER_MKFDS" udp$ip $FDS $FDC \
				      server-port=56789 \
				      client-port=45678 \
				      client-do-connect=no \
				      lite=$lite; }
    if read -r -u "${MKFDS[0]}" PID; then
	${TS_CMD_LSFD} -n \
		       -o ASSOC,TYPE,STTYPE,NAME,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.LADDR,${COLNS[$lite]}.LPORT,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT \
		       -p "${PID}" -Q "${EXPR[$index]}"
	echo "ASSOC,TYPE,STTYPE,NAME,SOCK.STATE,SOCK.TYPE,SOCK.LISTENING,INET$ip.LADDR,INET$ip.RADDR,${COLNS[$lite]}.LADDR,${COLNS[$lite]}.LPORT,${COLNS[$lite]}.RADDR,${COLNS[$lite]}.RPORT": $?

	echo DONE >&"${MKFDS[1]}"
    fi
    wait "${MKFDS_PID}"
} > "$TS_OUTPUT" 2>&1
