#!/bin/sh
# This script reproduces the output of the Solaris hostid program.
# It might be put in /usr/bin/hostid or so.
# Note that the hostid program does not have any known uses
# and does not exist on other architectures.
# Copyright 1999 Peter Jones, <pjones@redhat.com> .  
# GPL and all that good stuff apply.
(
idprom=`cat /proc/openprom/idprom`
echo $idprom|dd bs=1 skip=2 count=2
echo $idprom|dd bs=1 skip=27 count=6
echo
) 2>/dev/null
