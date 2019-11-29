#!/bin/sh
#
# test_mysql.sh - runs libdbi test suite for PGSQL driver using a temporary
# PGSQL server environment that doesn't disturb any running PGSQL server.
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

MYUSER=`whoami`

# initdb refuses to run as root
if [ "${MYUSER}" = "root" ] ; then
	echo dropping root privs..
	exec /bin/su postgres -- "$0" "$@"
fi

MYTMPDIR=`mktemp -d tmpXXXXXX`
BINDIR=`pg_config --bindir`

# voodo commands to turn temp dir into absolute path
D=`dirname "$MYTMPDIR"`
B=`basename "$MYTMPDIR"`
ABSMYTMPDIR="`cd \"$D\" 2>/dev/null && pwd || echo \"$D\"`/$B"

# depends on language-pack-en | language-pack-en
# because initdb acquires encoding from locale
export LC_ALL="en_US.UTF-8"
${BINDIR}/initdb -D ${MYTMPDIR}
# the non-interactive standalone server is not available in older PGSQL versions
#${BINDIR}/postgres -D ${MYTMPDIR} -h '' -k ${MYTMPDIR} &
${BINDIR}/postmaster -D ${MYTMPDIR} -h '' -k ${ABSMYTMPDIR} &
attempts=0
while ! [ -e ${MYTMPDIR}/postmaster.pid ] ; do
	attempts=$((attempts+1))
	if [ "${attempts}" -gt 10 ] ; then
		echo "skipping test, postgres pid file was not created after 30 seconds"
		exit 0
	fi                                        
	sleep 3
	echo `date`: retrying..
done

# Set the env. var so that pgsql client doesn't use networking
# libpq uses this for all host params if not explicitly passed
export PGHOST=${ABSMYTMPDIR}

# Create a new test db in our own temp env to check that everything
# is working as expected.
if ! createdb -e libdbitest ; then
	echo "Skipping postgres test as libdbitest database creation failed"
	exit 0
fi
dropdb -e libdbitest

# Finally, run the libdbi pgsql test app
createdb -e ${MYUSER}

# note to self: what is this command for? it doesn't set a password
psql -d ${MYUSER} -e -c "ALTER USER ${MYUSER} WITH PASSWORD 'abcdefg'"
( echo "i"; \
    echo "n"; \
    echo ../drivers/pgsql/.libs; \
    echo pgsql;
    echo ${MYUSER}; \
    echo ""; \
    echo ${ABSMYTMPDIR}; \
    echo libdbitest; \
    echo ""; \
	) | ./test_dbi
#/bin/sh debian/run_test_driver.sh pgsql ${MYUSER} "abcdefg" "127.0.0.2" libdbitest
ecode=$?

# Kill the postgress process and wait of it to shutdown
$BINDIR/pg_ctl stop -D ${MYTMPDIR}
wait
rm -rf $MYTMPDIR
exit $ecode
