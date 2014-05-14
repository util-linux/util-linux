#
# Copyright (C) 2007 Karel Zak <kzak@redhat.com>
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


function ts_abspath {
	cd $1
	pwd
}

function ts_canonicalize {
	P="$1"
	C=$(readlink -f $P)

	if [ -n "$C" ]; then
		echo "$C"
	else
		echo "$P"
	fi
}

function ts_report {
	if [ "$TS_PARALLEL" == "yes" ]; then
		echo "$TS_TITLE $1"
	else
		echo "$1"
	fi
}

function ts_check_test_command {
	if [ ! -x "$1" ]; then
		ts_skip "${1##*/} not found"
	fi
}

function ts_skip_subtest {
	ts_report " IGNORE ($1)"
}

function ts_skip {
	ts_skip_subtest "$1"
	if [ -n "$2" -a -b "$2" ]; then
		ts_device_deinit "$2"
	fi
	exit 0
}

function ts_skip_nonroot {
	if [ $UID -ne 0 ]; then
		ts_skip "not root permissions"
	fi
}

function ts_failed_subtest {
	local msg="FAILED"
	local ret=1
	if [ "$TS_KNOWN_FAIL" = "yes" ]; then
		msg="KNOWN FAILED"
		ret=0
	fi

	if [ x"$1" == x"" ]; then
		ts_report " $msg ($TS_NS)"
	else
		ts_report " $msg ($1)"
	fi

	return $ret
}

function ts_failed {
	ts_failed_subtest "$1"
	exit $?
}

function ts_ok_subtest {
	if [ x"$1" == x"" ]; then
		ts_report " OK"
	else
		ts_report " OK ($1)"
	fi
}

function ts_ok {
	ts_ok_subtest "$1"
	exit 0
}

function ts_log {
	echo "$1" >> $TS_OUTPUT
	[ "$TS_VERBOSE" == "yes" ] && echo "$1"
}

function ts_has_option {
	NAME="$1"
	ALL="$2"

	# user may set options by env for a single test or whole component
	# e.g. TS_OPT_ipcs_limits2_fake="yes" or TS_OPT_ipcs_fake="yes"
	local v_test=${TS_TESTNAME//[-.]/_}
	local v_comp=${TS_COMPONENT//[-.]/_}
	local v_name=${NAME//[-.]/_}
	eval local env_opt_test=\$TS_OPT_${v_comp}_${v_test}_${v_name}
	eval local env_opt_comp=\$TS_OPT_${v_comp}_${v_name}
	if [ "$env_opt_test" = "yes" \
		-o "$env_opt_comp" = "yes" -a "$env_opt_test" != "no" ]; then
		echo "yes"
		return
	elif [ "$env_opt_test" = "no" \
		-o "$env_opt_comp" = "no" -a "$env_opt_test" != "yes" ]; then
		return
	fi

	# or just check the global command line options
	echo -n $ALL | sed 's/ //g' | awk 'BEGIN { FS="="; RS="--" } /('$NAME'$|'$NAME'=)/ { print "yes" }'
}

function ts_option_argument {
	NAME="$1"
	ALL="$2"
	echo -n $ALL | sed 's/ //g' | awk 'BEGIN { FS="="; RS="--" } /'$NAME'=/ { print $2 }'
}

function ts_init_core_env {
	TS_NS="$TS_COMPONENT/$TS_TESTNAME"
	TS_OUTPUT="$TS_OUTDIR/$TS_TESTNAME"
	TS_VGDUMP="$TS_OUTDIR/$TS_TESTNAME.vgdump"
	TS_DIFF="$TS_DIFFDIR/$TS_TESTNAME"
	TS_EXPECTED="$TS_TOPDIR/expected/$TS_NS"
	TS_MOUNTPOINT="$TS_OUTDIR/${TS_TESTNAME}-mnt"
}

function ts_init_core_subtest_env {
	TS_NS="$TS_COMPONENT/$TS_TESTNAME-$TS_SUBNAME"
	TS_OUTPUT="$TS_OUTDIR/$TS_TESTNAME-$TS_SUBNAME"
	TS_VGDUMP="$TS_OUTDIR/$TS_TESTNAME-$TS_SUBNAME.vgdump"
	TS_DIFF="$TS_DIFFDIR/$TS_TESTNAME-$TS_SUBNAME"
	TS_EXPECTED="$TS_TOPDIR/expected/$TS_NS"
	TS_MOUNTPOINT="$TS_OUTDIR/${TS_TESTNAME}-${TS_SUBNAME}-mnt"

	rm -f $TS_OUTPUT $TS_VGDUMP
	[ -d "$TS_OUTDIR" ]  || mkdir -p "$TS_OUTDIR"

	touch $TS_OUTPUT
	[ -n "$TS_VALGRIND_CMD" ] && touch $TS_VGDUMP
}

function ts_init_env {
	local mydir=$(ts_abspath ${0%/*})
	local tmp

	LANG="POSIX"
	LANGUAGE="POSIX"
	LC_ALL="POSIX"
	CHARSET="UTF-8"

	export LANG LANGUAGE LC_ALL CHARSET

	mydir=$(ts_canonicalize "$mydir")

	# automake directories
	top_srcdir=$(ts_option_argument "srcdir" "$*")
	top_builddir=$(ts_option_argument "builddir" "$*")

	# where is this script
	TS_TOPDIR=$(ts_abspath $mydir/../../)

	# default
	if [ -z "$top_srcdir" ]; then
		top_srcdir="$TS_TOPDIR/.."
	fi
	if [ -z "$top_builddir" ]; then
		top_builddir="$TS_TOPDIR/.."
	fi

	top_srcdir=$(ts_abspath $top_srcdir)
	top_builddir=$(ts_abspath $top_builddir)

	TS_SCRIPT="$mydir/$(basename $0)"
	TS_SUBDIR=$(dirname $TS_SCRIPT)
	TS_TESTNAME=$(basename $TS_SCRIPT)
	TS_COMPONENT=$(basename $TS_SUBDIR)

	TS_NSUBTESTS=0
	TS_NSUBFAILED=0

	TS_SELF="$TS_SUBDIR"

	TS_OUTDIR="$top_builddir/tests/output/$TS_COMPONENT"
	TS_DIFFDIR="$top_builddir/tests/diff/$TS_COMPONENT"

	ts_init_core_env

	TS_VERBOSE=$(ts_has_option "verbose" "$*")
	TS_PARALLEL=$(ts_has_option "parallel" "$*")
	TS_KNOWN_FAIL=$(ts_has_option "known-fail" "$*")

	tmp=$( ts_has_option "memcheck" "$*")
	if [ "$tmp" == "yes" -a -f /usr/bin/valgrind ]; then
		TS_VALGRIND_CMD="/usr/bin/valgrind"
	fi

	BLKID_FILE="$TS_OUTDIR/${TS_TESTNAME}.blkidtab"

	declare -a TS_SUID_PROGS
	declare -a TS_SUID_USER
	declare -a TS_SUID_GROUP

	if [ -f $TS_TOPDIR/commands.sh ]; then
		. $TS_TOPDIR/commands.sh
	fi

	export BLKID_FILE

	rm -f $TS_OUTPUT $TS_VGDUMP
	[ -d "$TS_OUTDIR" ]  || mkdir -p "$TS_OUTDIR"

	touch $TS_OUTPUT
	[ -n "$TS_VALGRIND_CMD" ] && touch $TS_VGDUMP

	if [ "$TS_VERBOSE" == "yes" ]; then
		echo
		echo "     script: $TS_SCRIPT"
		echo "    sub dir: $TS_SUBDIR"
		echo "    top dir: $TS_TOPDIR"
		echo "       self: $TS_SELF"
		echo "  test name: $TS_TESTNAME"
		echo "  test desc: $TS_DESC"
		echo "  component: $TS_COMPONENT"
		echo "  namespace: $TS_NS"
		echo "    verbose: $TS_VERBOSE"
		echo "     output: $TS_OUTPUT"
		echo "   valgrind: $TS_VGDUMP"
		echo "   expected: $TS_EXPECTED"
		echo " mountpoint: $TS_MOUNTPOINT"
		echo
	fi
}

function ts_init_subtest {

	TS_SUBNAME="$1"

	ts_init_core_subtest_env

	[ $TS_NSUBTESTS -eq 0 ] && echo
	TS_NSUBTESTS=$(( $TS_NSUBTESTS + 1 ))

	if [ "$TS_PARALLEL" == "yes" ]; then
		TS_TITLE=$(printf "%13s: %-30s ...\n%16s: %-27s ..." "$TS_COMPONENT" "$TS_DESC" "" "$TS_SUBNAME")
	else
		TS_TITLE=$(printf "%16s: %-27s ..." "" "$TS_SUBNAME")
		echo -n "$TS_TITLE"
	fi
}

function ts_init {
	ts_init_env "$*"

	local is_fake=$( ts_has_option "fake" "$*")
	local is_force=$( ts_has_option "force" "$*")

	if [ "$TS_PARALLEL" == "yes" ]; then
		TS_TITLE=$(printf "%13s: %-30s ..." "$TS_COMPONENT" "$TS_DESC")
	else
		TS_TITLE=$(printf "%13s: %-30s ..." "$TS_COMPONENT" "$TS_DESC")
		echo -n "$TS_TITLE"
	fi

	[ "$is_fake" == "yes" ] && ts_skip "fake mode"
	[ "$TS_OPTIONAL" == "yes" -a "$is_force" != "yes" ] && ts_skip "optional"
}

function ts_init_suid {
	PROG="$1"
	ct=${#TS_SUID_PROGS[*]}

	# Save info about original setting
	TS_SUID_PROGS[$ct]=$PROG
	TS_SUID_USER[$ct]=$(stat --printf="%U" $PROG)
	TS_SUID_GROUP[$ct]=$(stat --printf="%G" $PROG)

	chown root.root $PROG &> /dev/null
	chmod u+s $PROG &> /dev/null
}

function ts_init_py {
	LIBNAME="$1"

	[ -f "$TS_TOPDIR/../py${LIBNAME}.la" ] || ts_skip "py${LIBNAME} not compiled"

	export LD_LIBRARY_PATH="$TS_TOPDIR/../.libs"
	export PYTHONPATH="$TS_TOPDIR/../$LIBNAME/python:$TS_TOPDIR/../.libs"

	export PYTHON_VERSION=$(awk '/^PYTHON_VERSION/ { print $3 }' $top_builddir/Makefile)
	export PYTHON_MAJOR_VERSION=$(echo $PYTHON_VERSION | sed 's/\..*//')

	export PYTHON="python${PYTHON_MAJOR_VERSION}"
}

function ts_valgrind {
	if [ -z "$TS_VALGRIND_CMD" ]; then
		$*
	else
		$TS_VALGRIND_CMD --tool=memcheck --leak-check=full \
				 --leak-resolution=high --num-callers=20 \
				 --log-file="$TS_VGDUMP" $*
	fi
}

function ts_gen_diff {
	local res=0

	if [ -s "$TS_OUTPUT" ]; then

		# remove libtool lt- prefixes
		sed --in-place 's/^lt\-\(.*\: \)/\1/g' $TS_OUTPUT

		[ -d "$TS_DIFFDIR" ] || mkdir -p "$TS_DIFFDIR"
		diff -u $TS_EXPECTED $TS_OUTPUT > $TS_DIFF

		if [ -s $TS_DIFF ]; then
			res=1
		else
			rm -f $TS_DIFF;
		fi
	else
		res=1
	fi
	return $res
}

function tt_gen_mem_report {
	[ -z "$TS_VALGRIND_CMD" ] && echo "$1"

	grep -q -E 'ERROR SUMMARY: [1-9]' $TS_VGDUMP &> /dev/null
	if [ $? -eq 0 ]; then
		echo "mem-error detected!"
	fi
}

function ts_finalize_subtest {
	local res=0

	if [ -s "$TS_EXPECTED" ]; then
		ts_gen_diff
		if [ $? -eq 1 ]; then
			ts_failed_subtest "$1"
			res=1
		else
			ts_ok_subtest "$(tt_gen_mem_report "$1")"
		fi
	else
		ts_skip_subtest "output undefined"
	fi

	[ $res -ne 0 ] && TS_NSUBFAILED=$(( $TS_NSUBFAILED + 1 ))

	# reset environment back to parental test
	ts_init_core_env

	return $res
}

function ts_finalize {
	for idx in $(seq 0 $((${#TS_SUID_PROGS[*]} - 1))); do
		PROG=${TS_SUID_PROGS[$idx]}
		chmod a-s $PROG &> /dev/null
		chown ${TS_SUID_USER[$idx]}.${TS_SUID_GROUP[$idx]} $PROG &> /dev/null
	done

	if [ $TS_NSUBTESTS -ne 0 ]; then
		printf "%11s..."
		if [ $TS_NSUBFAILED -ne 0 ]; then
			ts_failed "$TS_NSUBFAILED from $TS_NSUBTESTS sub-tests"
		else
			ts_ok "all $TS_NSUBTESTS sub-tests PASSED"
		fi
	fi

	if [ -s $TS_EXPECTED ]; then
		ts_gen_diff
		if [ $? -eq 1 ]; then
			ts_failed "$1"
		fi
		ts_ok "$1"
	fi

	ts_skip "output undefined"
}

function ts_die {
	ts_log "$1"
	if [ -n "$2" ] && [ -b "$2" ]; then
		ts_device_deinit "$2"
		ts_fstab_clean		# for sure...
	fi
	ts_finalize
}

function ts_image_md5sum {
	local img=${1:-"$TS_OUTDIR/${TS_TESTNAME}.img"}
	echo $(md5sum "$img" | awk '{printf $1}') $(basename "$img")
}

function ts_image_init {
	local mib=${1:-"5"}	# size in MiBs
	local img=${2:-"$TS_OUTDIR/${TS_TESTNAME}.img"}

	dd if=/dev/zero of="$img" bs=1M count=$mib &> /dev/null
	echo "$img"
	return 0
}

function ts_device_init {
	local img=$(ts_image_init $1 $2)
	local dev=$($TS_CMD_LOSETUP --show -f "$img")

	if [ -z "$dev" ]; then
		ts_device_deinit $dev
		return 1		# error
	fi

	echo $dev
	return 0			# succes
}

function ts_device_deinit {
	local DEV="$1"

	if [ -b "$DEV" ]; then
		$TS_CMD_UMOUNT "$DEV" &> /dev/null
		$TS_CMD_LOSETUP -d "$DEV" &> /dev/null
	fi
}

function ts_uuid_by_devname {
	echo $($TS_CMD_BLKID -p -s UUID -o value $1)
}

function ts_label_by_devname {
	echo $($TS_CMD_BLKID -p -s LABEL -o value $1)
}

function ts_fstype_by_devname {
	echo $($TS_CMD_BLKID -p -s TYPE -o value $1)
}

function ts_device_has {
	local TAG="$1"
	local VAL="$2"
	local DEV="$3"
	local vl=""

	case $TAG in
		"TYPE") vl=$(ts_fstype_by_devname $DEV);;
		"LABEL") vl=$(ts_label_by_devname $DEV);;
		"UUID") vl=$(ts_uuid_by_devname $DEV);;
		*) return 1;;
	esac

	if [ "$vl" == "$VAL" ]; then
		return 0
	fi
	return 1
}

function ts_device_has_uuid {
	ts_uuid_by_devname "$1" | egrep -q '^[0-9a-z]{8}-[0-9a-z]{4}-[0-9a-z]{4}-[0-9a-z]{4}-[0-9a-z]{12}$'
	return $?
}

function ts_is_mounted {
	local DEV=$(ts_canonicalize "$1")

	grep -q $DEV /proc/mounts && return 0

	if [ "${DEV#/dev/loop/}" != "$DEV" ]; then
		return grep -q "/dev/loop${DEV#/dev/loop/}" /proc/mounts
	fi
	return 1
}

function ts_fstab_open {
	echo "# <!-- util-linux test entry" >> /etc/fstab
}

function ts_fstab_close {
	echo "# -->" >> /etc/fstab
}

function ts_fstab_addline {
	local SPEC="$1"
	local MNT=${2:-"$TS_MOUNTPOINT"}
	local FS=${3:-"auto"}
	local OPT=${4:-"defaults"}

	echo "$SPEC   $MNT   $FS   $OPT   0   0" >> /etc/fstab
}

function ts_fstab_add {
	ts_fstab_open
	ts_fstab_addline $*
	ts_fstab_close
}

function ts_fstab_clean {
	sed --in-place "
/# <!-- util-linux/!b
:a
/# -->/!{
  N
  ba
}
s/# <!-- util-linux.*-->//;
/^$/d" /etc/fstab
}

function ts_fdisk_clean {
	local DEVNAME=$1

	# remove non comparable parts of fdisk output
	if [ x"${DEVNAME}" != x"" ]; then
	       sed -i -e "s:${DEVNAME}:<removed>:g" $TS_OUTPUT
	fi

	sed -i -e 's/Disk identifier:.*/Disk identifier: <removed>/g' \
	       -e 's/Created a new.*/Created a new <removed>./g' \
	       -e 's/^Device[[:blank:]]*Start/Device             Start/g' \
	       -e 's/^Device[[:blank:]]*Boot/Device     Boot/g' \
	       -e 's/^Device[[:blank:]]*Flag/Device     Flag/g' \
	       -e 's/Welcome to fdisk.*/Welcome to fdisk <removed>./g' \
	       $TS_OUTPUT
}

function ts_scsi_debug_init {

	modprobe --dry-run --quiet scsi_debug
	[ "$?" == 0 ] || ts_skip "missing scsi_debug module"

	rmmod scsi_debug &> /dev/null
	modprobe scsi_debug $*
	[ "$?" == 0 ] || ts_die "Cannot init device"

	DEVNAME=$(grep --with-filename scsi_debug /sys/block/*/device/model | awk -F '/' '{print $4}')
	[ "x${DEVNAME}" == "x" ] && ts_die "Cannot find device"

	DEVICE="/dev/${DEVNAME}"

	sleep 1
	udevadm settle

	echo $DEVICE
}
