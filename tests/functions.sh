
TS_OUTDIR="$TS_TOPDIR/output"
TS_DIFFDIR="$TS_TOPDIR/diff"
TS_EXPECTEDDIR="$TS_TOPDIR/expected"

function ts_skip {
	echo " IGNORE ($1)"
	exit 0
}

function ts_init {
	TS_NAME=$(basename $0)
	if [ ! -d $TS_OUTDIR ]; then
		mkdir -p $TS_OUTDIR
	fi
	if [ ! -d $TS_DIFFDIR ]; then
		mkdir -p $TS_DIFFDIR
	fi
	TS_OUTPUT="$TS_OUTDIR/$TS_NAME"
	TS_DIFF="$TS_DIFFDIR/$TS_NAME"
	TS_EXPECTED="$TS_EXPECTEDDIR/$TS_NAME"

	rm -f $TS_OUTPUT

	printf "%15s: %-25s ..." "$TS_COMPONENT" "$TS_DESC"
}

function ts_finalize {
	local res=0

	if [ -s $TS_EXPECTED ]; then
		if [ -s $TS_OUTPUT ]; then
			diff -u $TS_EXPECTED $TS_OUTPUT > $TS_DIFF
			if [ -s $TS_DIFF ]; then
				res=1
			fi
		else
			res=0
		fi
	else
		echo " IGNORE (expected output undefined)"
		exit 0
	fi
	if [ $res -eq 0 ]; then
		if [ x"$1" == x"" ]; then
			echo " OK"
		else
			echo " OK ($1)"
		fi
		exit 0
	else
		echo " FAILED ($TS_NAME)"
		exit 1
	fi
}

