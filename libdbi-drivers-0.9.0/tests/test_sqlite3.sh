#!/bin/sh
#
# test_sqlite3.sh - runs libdbi test suite for sqlite3 driver
#
# Copyright (C) 2010 Clint Byrum <clint@ubuntu.com>
# Copyright (C) 2010 Thomas Goirand <zigo@debian.org>
# Copyright (C) 2010 Markus Hoenicka <markus@mhoenicka.de>
#
# This script is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 2.1 of the License, or (at your option)
# any later version.
#
# This script is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library; if not, write to:
# The Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA

set -e

MYTMPDIR=`mktemp -d tmpXXXXXX`

( echo "i"; \
    echo "n"; \
    echo ../drivers/sqlite3/.libs; \
    echo sqlite3; \
    echo ${MYTMPDIR}; \
    echo "libdbitest"; \
    echo ""; \
	) | ./test_dbi

ecode=$?

rm -rf ${MYTMPDIR}
exit ${ecode}
