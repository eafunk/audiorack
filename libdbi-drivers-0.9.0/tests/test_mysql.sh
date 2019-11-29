#!/bin/sh
#
# test_mysql.sh - runs libdbi test suite for mysql driver using a temporary
# mysql server environment that doesn't disturb any running MySQL server.
#
# Copyright (C) 2010 Clint Byrum <clint@ubuntu.com>
# Copyright (C) 2010 Thomas Goirand <zigo@debian.org>
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
ME=`whoami`

# the mysqld binary is usually not in PATH. Check the usual suspects
# and pick the first one that comes up
# bet this is not going to work on OS X
MYMYSQLD=`find /usr/bin /usr/local/bin /usr/sbin /usr/local/sbin /usr/libexec /usr/local/libexec -name mysqld|head -1`

# voodo commands to turn temp dir into absolute path
D=`dirname "$MYTMPDIR"`
B=`basename "$MYTMPDIR"`
ABSMYTMPDIR="`cd \"$D\" 2>/dev/null && pwd || echo \"$D\"`/$B"

# --force is needed because buildd's can't resolve their own hostnames to ips
mysql_install_db --no-defaults --datadir=${ABSMYTMPDIR} --force --skip-name-resolve --user=${ME}
$MYMYSQLD --no-defaults --skip-grant --user=${ME} --socket=${ABSMYTMPDIR}/mysql.sock --datadir=${ABSMYTMPDIR} --skip-networking &

# mysqld needs some time to come up to speed. This avoids irritating error messages from the subsequent loop
sleep 3

# This sets the path of the MySQL socket for any libmysql-client users, which includes
# the ./tests/test_dbi client
export MYSQL_UNIX_PORT=${ABSMYTMPDIR}/mysql.sock

echo -n pinging mysqld.
attempts=0
while ! mysqladmin --socket=${ABSMYTMPDIR}/mysql.sock ping ; do
	sleep 3
	attempts=$((attempts+1))
	if [ ${attempts} -gt 10 ] ; then
		echo "skipping test, mysql server could not be contacted after 30 seconds"
		exit 0
	fi
done

( echo "i"; \
    echo "n"; \
    echo ../drivers/mysql/.libs; \
    echo mysql; \
    echo root; \
    echo ""; \
    echo ""; \
    echo "libdbitest"; \
    echo ""; \
	) | ./test_dbi

ecode=$?

mysqladmin --socket=${ABSMYTMPDIR}/mysql.sock shutdown
rm -rf ${ABSMYTMPDIR}
exit ${ecode}
