
TS_OUTDIR="output"
TS_DIFFDIR="diff"
TS_EXPECTEDDIR="expected"
TS_INPUTDIR="input"

function ts_skip {
	echo " IGNORE ($1)"
	exit 0
}

function ts_skip_nonroot {
	if [ $UID != 0 ]; then
		ts_skip "not root permissions"
	fi
}

function ts_failed {
	if [ x"$1" == x"" ]; then
		echo " FAILED ($TS_NAME)"
	else
		echo " FAILED ($1)"
	fi
	exit 1
}

function ts_ok {
	if [ x"$1" == x"" ]; then
		echo " OK"
	else
		echo " OK ($1)"
	fi
	exit 0
}

function ts_init {
	export LANG="en_US.UTF-8":
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
	TS_INPUT="$TS_INPUTDIR/$TS_NAME"

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
			res=1
		fi
	else
		echo " IGNORE (expected output undefined)"
		exit 0
	fi
	if [ $res -eq 0 ]; then
		ts_ok $1
	else
		ts_failed $1
	fi
}



