#!/bin/bash
#
# Copyright (C) 2008 Karel Zak <kzak@redhat.com>
#
# This script makes a copy of relevant files from /sys and /proc.
# The files are usefull for lscpu(1) regression tests.
#
# For exmaple:
#
#   # mk-lscpu-input ts-lscpu-i386-coolhw
#   # lscpu --sysroot dumps/ts-lscpu-i386-coolhw
#

progname=$(basename $0)

if [ -z "$1" ]; then
	echo -e "\nusage: $progname <testname>\n"
	exit 1
fi

TS_NAME="$1"
TS_INPUT="dumps/$TS_NAME"
CP="cp -r --parents"

mkdir -p $TS_INPUT/{proc,sys}

$CP /proc/cpuinfo $TS_INPUT

mkdir -p $TS_INPUT/proc/bus/pci
$CP /proc/bus/pci/devices $TS_INPUT

if [ -d "/proc/xen" ]; then
	mkdir -p $TS_INPUT/proc/xen
	if [ -f "/proc/xen/capabilities" ]; then
		$CP /proc/xen/capabilities $TS_INPUT
	fi
fi

for c in $(ls -d /sys/devices/system/cpu/cpu[0-9]*); do
	mkdir -p $TS_INPUT/$c
done

$CP /sys/devices/system/cpu/cpu0/topology/{thread_siblings,core_siblings} $TS_INPUT
$CP /sys/devices/system/cpu/cpu0/cache/*/{type,level,size,shared_cpu_map} $TS_INPUT

$CP /sys/devices/system/node/*/cpumap $TS_INPUT


