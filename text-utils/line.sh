#!/bin/sh
#
# line		read one line
#
# Version:	$Id: line,v 1.1 2001/08/03 14:54:10 hch Exp hch $
#
# Author:	Christoph Hellwig <hch@caldera.de>
#
#		This program is free software; you can redistribute it and/or
#		modify it under the terms of the GNU General Public License
#		as published by the Free Software Foundation; either version
#		2 of the License, or (at your option) any later version.
#

read line
echo ${line}

if [ -z "${line}" ]; then
    exit 1
else
    exit 0
fi
