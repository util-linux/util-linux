
TS_OUTDIR="$TS_TOPDIR/output"
TS_DIFFDIR="$TS_TOPDIR/diff"
TS_EXPECTEDDIR="$TS_TOPDIR/expected"

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
	fi
	if [ $res -eq 0 ]; then
		echo " OK"
		exit 0
	else
		echo " FAILED ($TS_NAME)"
		exit 1
	fi
}

