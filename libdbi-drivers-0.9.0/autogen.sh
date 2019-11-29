#!/bin/sh
# autogen.sh - generates configure using the autotools
# $Id: autogen.sh,v 1.7 2010/03/21 16:46:12 mhoenicka Exp $
: ${LIBTOOLIZE=libtoolize}
: ${ACLOCAL=aclocal}
: ${AUTOHEADER=autoheader}
: ${AUTOMAKE=automake}
: ${AUTOCONF=autoconf}


${LIBTOOLIZE} --force --copy
# the -I m4 hack allows aclocal to find the libtool macros
${ACLOCAL} -I m4
${AUTOHEADER}
${AUTOMAKE} --add-missing

echo
echo "Using `${AUTOCONF} --version`, if you have problems"
echo "while building then set the environemnt variable AUTOCONF"
echo "to a different autoconf version and rerun $0"
echo
${AUTOCONF}

## autoconf 2.53 will not work, at least on FreeBSD. Change the following
## line appropriately to call autoconf 2.13 instead. This one works for
## FreeBSD 4.7:
#autoconf213

