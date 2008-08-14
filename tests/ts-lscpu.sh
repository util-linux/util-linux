#
# Copyright (C) 2008 Cai Qian <qcai@redhat.com>
#
# This file is part of util-linux-ng.
#
# This file is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This file is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
. ./commands.sh
. ./functions.sh

TS_COMPONENT="lscpu"

ts_init "$*"

# Architecture information is not applicable with -s.
"${TS_CMD_LSCPU}" -s "${TS_INPUT}" | grep -v "Architecture" \
 >"${TS_OUTPUT}" 2>&1

echo >>"${TS_OUTPUT}"

"${TS_CMD_LSCPU}" -p -s "${TS_INPUT}" >>"${TS_OUTPUT}" 2>&1

ts_finalize
