#!/bin/sh
# This script calls test_dbi_dlopen which simulates using drivers from 
# a plugin that is dlopened.

homedir=`dirname $0`

set -e

rm -rf .plugins
mkdir .plugins

olddir=`pwd`
cd ..  
rootdir=`pwd`
for f in `find drivers -name 'libdbd*.so'` ; do
  ln -vs $rootdir/$f $olddir/.plugins
done
cd $olddir

. $homedir/plugin_settings.sh

export LD_LIBRARY_PATH=${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}${LIBDBI_LIBDIR}

$homedir/test_dbi_dlopen G .plugins
$homedir/test_dbi_dlopen N .plugins || echo Failure is ok.

rm -rf .plugins
