#!/bin/bash -x
adm=/tmp/admutil
poe=/tmp/poeigl
tmp=/tmp/$$
diffs=poe.diffs

if [ -e $diffs ]; then rm $diffs; fi
if [ ! -d $tmp ]; then mkdir $tmp; fi

function cmpandcp () {
    dir=$1;
    i=$2;
    name=${i#$poe/};
    name=${name#$adm/};
    target=$dir/$name;
    diff -u $target.c $i.c >> $diffs;
    mv $target.c $tmp/$name.c;
    mv $i.c $target.c;
    for k in man 1 8; do
        if [ -e $i.$k ]; then
            for j in 1 8; do
                if [ -e $target.$j ]; then
                    diff -u $target.$j $i.$k >> $diffs;
                    mv $target.$j $tmp/$name.$j;
                    mv $i.$k $target.$j;
                fi
            done
        fi
    done
}


# login-utils
for i in $poe/agetty $adm/last $poe/login $adm/newgrp $adm/passwd \
        $adm/shutdown $poe/simpleinit; do
    cmpandcp login-utils $i;
done

# misc-utils
cmpandcp misc-utils $poe/hostid;
cmpandcp misc-utils $poe/domainname;

# sys-utils
cmpandcp sys-utils $adm/ctrlaltdel;

# READMEs
diff -u $adm/README login-utils/README.admutil >> $diffs
mv $adm/README login-utils/README.admutil

diff -u $poe/README login-utils/README.poeigl >> $diffs
mv $poe/README login-utils/README.poeigl

diff -u $poe/README.getty login-utils >> $diffs
mv $poe/README.getty login-utils

exit
