#!/bin/bash

. commands.sh

echo
echo "------------------ Utils-linux-ng regression tests ------------------"
echo

rm -f *~

res=0
count=0
for ts in $(find -maxdepth 1 -regex "\./ts[^\.~]*" |  sort); do
	$TS_TOPDIR/$ts "$1"
	res=$(( $res + $? ))
	count=$(( $count + 1 ))
done

echo
echo "---------------------------------------------------------------------"
if [ $res -eq 0 ]; then
	echo "  All $count tests PASSED"
	res=0
else
	echo "  $res tests of $count FAILED"
	res=1
fi
echo "---------------------------------------------------------------------"
exit $res
