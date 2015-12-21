# ===========================================================================
#    http://www.gnu.org/software/autoconf-archive/ax_swig_enable_cxx.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_SWIG_ENABLE_CXX
#
# DESCRIPTION
#
#   Enable SWIG C++ support. This affects all invocations of $(SWIG).
#
# LICENSE
#
#   Copyright (c) 2008 Sebastian Huber <sebastian-huber@web.de>
#   Copyright (c) 2008 Alan W. Irwin
#   Copyright (c) 2008 Rafael Laboissiere <rafael@laboissiere.net>
#   Copyright (c) 2008 Andrew Collier
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation; either version 2 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <http://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 9

AU_ALIAS([SWIG_ENABLE_CXX], [AX_SWIG_ENABLE_CXX])
AC_DEFUN([AX_SWIG_ENABLE_CXX],[
        AC_REQUIRE([AX_PKG_SWIG])
        AC_REQUIRE([AC_PROG_CXX])
        SWIG="$SWIG -c++"
])
