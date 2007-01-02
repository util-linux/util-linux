#!/bin/bash

. commands.sh

echo
echo "------------------ Utils-linux-ng regression tests ------------------"
echo

rm -f *~

res=0
count=0
for ts in `ls ts-*`; do
	$TS_TOPDIR/$ts
	res=$(( $res + $? ))
	count=$(( $count + 1 ))
done

echo
echo "---------------------------------------------------------------------"
if [ $res -eq 0 ]; then
	echo "  All $count tests PASSED"
else
	echo "  $res tests of $count FAILED"
fi
echo "---------------------------------------------------------------------"

