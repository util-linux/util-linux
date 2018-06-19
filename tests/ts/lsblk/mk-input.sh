#!/bin/bash
#
# Copyright (C) 2018 Karel Zak <kzak@redhat.com>
#
# This script makes a copy of relevant files from /sys and /proc.
# The files are useful for lscpu(1) regression tests.
#
progname=$(basename $0)

if [ -z "$1" ]; then
	echo -e "\nusage: $progname <testname>\n"
	exit 1
fi

TS_DUMP="$1"
TS_CMD_LSBLK=${TS_CMD_LSBLK:-"lsblk"}

#
# procfs
#
mkdir -p $TS_DUMP/proc
mkdir -p $TS_DUMP/proc/self
cp /proc/self/mountinfo ${TS_DUMP}/proc/self
cp /proc/swaps ${TS_DUMP}/proc/swaps
cp /proc/version ${TS_DUMP}/proc/version


#
# sysfs
#
mkdir -p $TS_DUMP/sys/{block,dev/block}
cp --no-dereference /sys/dev/block/* ${TS_DUMP}/sys/dev/block
cp --no-dereference /sys/block/* ${TS_DUMP}/sys/block

DEVS=$(find /sys/dev/block/ -type l)
for x in ${DEVS}; do
	DEV="/sys/dev/block/$(readlink $x)"

	mkdir -p ${TS_DUMP}/${DEV}

	# attributes
	for f in $(find ${DEV} -type f -not -path '*/trace/*' -not -path '*/uevent'); do
		if [ ! -f ${TS_DUMP}/${f} ]; then
			SUB=$(dirname $f)
			mkdir -p ${TS_DUMP}/${SUB}
			cp $f ${TS_DUMP}/$f 2> /dev/null
		fi
	done

	# symlinks (slave, holders, etc.)
	for f in $(find ${DEV} -type l -not -path '*/subsystem' -not -path '*/bdi'); do
		if [ ! -f ${TS_DUMP}/${f} ]; then
			SUB=$(dirname $f)
			mkdir -p ${TS_DUMP}/${SUB}
			cp --no-dereference $f ${TS_DUMP}/$f
		fi
	done

	# device/ files
	if [ -d ${DEV}/device/ ]; then 
		for f in $(find ${DEV}/device/ -maxdepth 1 -type f -not -path '*/uevent'); do
			if [ ! -f ${TS_DUMP}/${f} ]; then
				SUB=$(dirname $f)
				cp $f ${TS_DUMP}/$f 2> /dev/null
			fi
		done
	fi

done


function mk_output {
	cols="$2"
	name="$1"

	$TS_CMD_LSBLK -o NAME,${cols} > ${TS_DUMP}/lsblk.${name}
}

#
# lsblk info
#
$TS_CMD_LSBLK -V &> ${TS_DUMP}/version

mk_output basic KNAME,MAJ:MIN,RM,SIZE,TYPE,MOUNTPOINT
mk_output vendor MODEL,VENDOR,REV
mk_output state RO,RM,HOTPLUG,RAND,STATE,ROTA,TYPE,PKNAME,SCHED
mk_output rw RA,WSAME
mk_output topo SIZE,ALIGNMENT,MIN-IO,OPT-IO,PHY-SEC,LOG-SEC,RQ-SIZE
mk_output discard DISC-ALN,DISC-GRAN,DISC-MAX,DISC-ZERO
mk_output zone ZONED


tar zcvf lsblk-$TS_DUMP.tar.gz $TS_DUMP
rm -rf $TS_DUMP

echo -e "\nPlease, send lsblk-$TS_DUMP.tar.gz to util-linux upstream. Thanks!\n"


