##
## originally by Christian Hammers <ch@westend.com>,
## who borrowed ideas from Stephan Kulow.
## then this file was tweaked by David Parker for libdbi.
## and finally it was ripped out and modified by Markus
## Hoenicka who then used for the libdbi-drivers project
##

dnl AC_SEARCH_LIBS_VAR(FUNCTION, SEARCH-LIBS [, ACTION-IF-FOUND
dnl            [, ACTION-IF-NOT-FOUND [, OTHER-LIBRARIES [, VAR]]]])
dnl Search for a library defining FUNC, if it's not already available.
dnl If a library is needed, add it to VAR (typically prog_LDADD) but
dnl not to LIBS

AC_DEFUN([AC_SEARCH_LIBS_VAR],
[AC_PREREQ([2.13])
AC_CACHE_CHECK([for library containing $1], [ac_cv_search_$1],
[ac_func_search_save_LIBS="$LIBS"
ac_cv_search_$1="no"
AC_TRY_LINK_FUNC([$1], [ac_cv_search_$1="none required"])
test "$ac_cv_search_$1" = "no" && for i in $2; do
LIBS="-l$i $5 $ac_func_search_save_LIBS"
AC_TRY_LINK_FUNC([$1],
[ac_cv_search_$1="-l$i"
break])
done
LIBS="$ac_func_search_save_LIBS"])
if test "$ac_cv_search_$1" != "no"; then
  test "$ac_cv_search_$1" = "none required" || $6="$$6 $ac_cv_search_$1"
  $3
else :
  $4
fi])

dnl try to get rid of this
AC_DEFUN([AC_FIND_FILE],
[
$3=no
for i in $2; do
	for j in $1; do
		if test -r "$i/$j"; then
			$3=$i
			break 2
		fi
	done
done
])

## DBI

AC_DEFUN([AC_CHECK_DBI],
[
AM_CONDITIONAL(HAVE_DBI, false)
ac_dbi_incdir="no"

# exported variables
DBI_INCLUDE=""

AC_MSG_CHECKING(for libdbi framework)

AC_ARG_WITH(dbi-incdir,
	[  --with-dbi-incdir       Specifies where the libdbi include files are.],
	[  ac_dbi_incdir="$withval" ])

if test "$ac_dbi_incdir" = "no"; then
	AC_CHECK_HEADERS([dbi/dbi.h])
	DBI_INCLUDE=""
else
	DBI_INCLUDE=-I$ac_dbi_incdir
fi
	
AC_SUBST(DBI_INCLUDE)
])

## MYSQL

AC_DEFUN([AC_CHECK_MYSQL],
[
AM_CONDITIONAL(HAVE_MYSQL, false)
ac_mysql="no"
ac_mysql_incdir="no"
ac_mysql_libdir="no"

# exported variables
MYSQL_LIBS=""
MYSQL_LDFLAGS=""
MYSQL_INCLUDE=""
MYSQL_TEST=""

AC_MSG_CHECKING(for MySQL support)

AC_ARG_WITH(mysql,
	[  --with-mysql            Include MySQL support.],
	[  ac_mysql="$withval" ])
AC_ARG_WITH(mysql-incdir,
	[  --with-mysql-incdir     Specifies where the MySQL include files are.],
	[  ac_mysql_incdir="$withval" ])
AC_ARG_WITH(mysql-libdir,
	[  --with-mysql-libdir     Specifies where the MySQL libraries are.],
	[  ac_mysql_libdir="$withval" ])

ac_mysql_threadsafe="NO"
AC_ARG_ENABLE(mysql-threadsafe,
	[  --enable-mysql-threadsafe	  use threadsafe version of libmysqlclient.],
	[  if test "$enable_mysql_threadsafe" = "yes"; then
	       ac_mysql_threadsafe="YES"
	   fi])

if test "$ac_mysql" = "yes"; then
   	AC_MSG_RESULT(yes)
	if test "$ac_mysql_incdir" = "no" || test "$ac_mysql_libdir" = "no"; then
	   	AC_CHECK_PROG([MYSQL_CONFIG], [mysql_config], [yes], [no])
		if test "$MYSQL_CONFIG" = "no"; then
		   AC_MSG_ERROR([cannot auto-configure MySQL without mysql_config])
		fi
	fi

	if test "$ac_mysql_incdir" = "no"; then
		MYSQL_INCLUDE=`mysql_config --include`
	else
		MYSQL_INCLUDE=-I$ac_mysql_incdir
	fi
	if test "$ac_mysql_libdir" = "no"; then
	   	if test "$ac_mysql_threadsafe" = "YES"; then
			MYSQL_LIBS=`mysql_config --libs_r`
		else
			MYSQL_LIBS=`mysql_config --libs`
		fi
	else
	   	if test "$ac_mysql_threadsafe" = "YES"; then
			MYSQL_LIBS="-L$ac_mysql_libdir -lmysqlclient_r"
		else
			MYSQL_LIBS="-L$ac_mysql_libdir -lmysqlclient"
		fi
	fi

	AM_CONDITIONAL(HAVE_MYSQL, true)
	MYSQL_TEST="test_mysql.sh"
	
	AC_SUBST(MYSQL_LIBS)
	AC_SUBST(MYSQL_INCLUDE)
	AC_MSG_CHECKING(for MySQL includes)
	AC_MSG_RESULT($MYSQL_INCLUDE)
	AC_SUBST(MYSQL_LDFLAGS)
	AC_MSG_CHECKING(for MySQL libraries)
	AC_MSG_RESULT($MYSQL_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(MYSQL_TEST)
])

## PGSQL

AC_DEFUN([AC_CHECK_PGSQL],
[
AM_CONDITIONAL(HAVE_PGSQL, false)
ac_pgsql="no"
ac_pgsql_incdir="no"
ac_pgsql_libdir="no"

# exported variables
PGSQL_LIBS=""
PGSQL_LDFLAGS=""
PGSQL_INCLUDE=""
PGSQL_TEST=""

AC_MSG_CHECKING(for PostgreSQL support)

AC_ARG_WITH(pgsql,
	[  --with-pgsql            Include PostgreSQL support.],
	[  ac_pgsql="$withval" ])
AC_ARG_WITH(pgsql-incdir,
	[  --with-pgsql-incdir     Specifies where the PostgreSQL include files are.],
	[  ac_pgsql_incdir="$withval" ])
AC_ARG_WITH(pgsql-libdir,
	[  --with-pgsql-libdir     Specifies where the PostgreSQL libraries are.],
	[  ac_pgsql_libdir="$withval" ])

if test "$ac_pgsql" = "yes"; then
	AC_MSG_RESULT([yes])
	if test "$ac_pgsql_incdir" = "no" || test "$ac_pgsql_libdir" = "no"; then
	   	AC_CHECK_PROG([PG_CONFIG], [pg_config], [yes], [no])
		if test "$PG_CONFIG" = "no"; then
		   AC_MSG_ERROR([cannot auto-configure PostgreSQL without pg_config])
		fi
	fi
	if test "$ac_pgsql_incdir" = "no"; then
		PGSQL_INCLUDE="-I"`pg_config --includedir`
	else
		PGSQL_INCLUDE=-I$ac_pgsql_incdir
	fi
	if test "$ac_pgsql_libdir" = "no"; then
		PGSQL_LDFLAGS=`pg_config --libdir`
	else
		PGSQL_LDFLAGS=-L$ac_pgsql_libdir
	fi

	PGSQL_LIBS=-lpq
	PGSQL_TEST="test_pgsql.sh"


	AM_CONDITIONAL(HAVE_PGSQL, true)
	
	AC_SUBST(PGSQL_LIBS)
	AC_SUBST(PGSQL_INCLUDE)
	AC_MSG_CHECKING(for PostgreSQL includes)
	AC_MSG_RESULT($PGSQL_INCLUDE)
	AC_SUBST(PGSQL_LDFLAGS)
	AC_MSG_CHECKING(for PostgreSQL libraries)
	AC_MSG_RESULT($PGSQL_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(PGSQL_TEST)
])

## SQLITE

AC_DEFUN([AC_CHECK_SQLITE],
[
AM_CONDITIONAL(HAVE_SQLITE, false)
ac_sqlite="no"
ac_sqlite_incdir="no"
ac_sqlite_libdir="no"

# exported variables
SQLITE_LIBS=""
SQLITE_LDFLAGS=""
SQLITE_INCLUDE=""
SQLITE_TEST=""

AC_MSG_CHECKING(for SQLite support)

AC_ARG_WITH(sqlite,
	[  --with-sqlite           Include SQLite support.],
	[  ac_sqlite="$withval" ])
AC_ARG_WITH(sqlite-incdir,
	[  --with-sqlite-incdir    Specifies where the SQLite include files are.],
	[  ac_sqlite_incdir="$withval" ])
AC_ARG_WITH(sqlite-libdir,
	[  --with-sqlite-libdir    Specifies where the SQLite libraries are.],
	[  ac_sqlite_libdir="$withval" ])

if test "$ac_sqlite" = "yes"; then
	AC_MSG_RESULT([yes])
	if test "$ac_sqlite_incdir" = "no"; then
		AC_CHECK_HEADER([sqlite.h])
		SQLITE_INCLUDE=""
	else
		SQLITE_INCLUDE=-I$ac_sqlite_incdir
	fi
	if test "$ac_sqlite_libdir" = "no"; then
		AC_SEARCH_LIBS_VAR(sqlite_exec, sqlite, , , , SQLITE_LIBS)
		SQLITE_LDFLAGS=""
	else
		SQLITE_LIBS=-lsqlite
		SQLITE_LDFLAGS=-L$ac_sqlite_libdir
	fi

	SQLITE_TEST="test_sqlite.sh"

	AM_CONDITIONAL(HAVE_SQLITE, true)
	
	AC_SUBST(SQLITE_LIBS)
	AC_SUBST(SQLITE_INCLUDE)
	AC_SUBST(SQLITE_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(SQLITE_TEST)
])

## SQLITE3

AC_DEFUN([AC_CHECK_SQLITE3],
[
AM_CONDITIONAL(HAVE_SQLITE3, false)
ac_sqlite3="no"
ac_sqlite3_incdir="no"
ac_sqlite3_libdir="no"

# exported variables
SQLITE3_LIBS=""
SQLITE3_LDFLAGS=""
SQLITE3_INCLUDE=""
SQLITE3_TEST=""

AC_MSG_CHECKING(for SQLite3 support)

AC_ARG_WITH(sqlite3,
	[  --with-sqlite3           Include SQLite3 support.],
	[  ac_sqlite3="$withval" ])
AC_ARG_WITH(sqlite3-incdir,
	[  --with-sqlite3-incdir    Specifies where the SQLite3 include files are.],
	[  ac_sqlite3_incdir="$withval" ])
AC_ARG_WITH(sqlite3-libdir,
	[  --with-sqlite3-libdir    Specifies where the SQLite3 libraries are.],
	[  ac_sqlite3_libdir="$withval" ])

if test "$ac_sqlite3" = "yes"; then
	AC_MSG_RESULT([yes])
	if test "$ac_sqlite3_incdir" = "no"; then
		AC_CHECK_HEADER([sqlite3.h])
		SQLITE3_INCLUDE=""
	else
		SQLITE3_INCLUDE=-I$ac_sqlite3_incdir
	fi
	if test "$ac_sqlite3_libdir" = "no"; then
		AC_SEARCH_LIBS_VAR([sqlite3_exec], sqlite3, , , , SQLITE3_LIBS)
		SQLITE3_LDFLAGS=""
	else
		SQLITE3_LIBS=-lsqlite
		SQLITE3_LDFLAGS=-L$ac_sqlite3_libdir
	fi

	SQLITE3_TEST="test_sqlite3.sh"
	AM_CONDITIONAL(HAVE_SQLITE3, true)
	
	AC_SUBST(SQLITE3_LIBS)
	AC_SUBST(SQLITE3_INCLUDE)
	AC_SUBST(SQLITE3_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(SQLITE3_TEST)
])


## mSQL

AC_DEFUN([AC_CHECK_MSQL],
[
AM_CONDITIONAL(HAVE_MSQL, false)
ac_msql="no"
ac_msql_incdir="no"
ac_msql_libdir="no"

# exported variables
MSQL_LIBS=""
MSQL_LDFLAGS=""
MSQL_INCLUDE=""
MSQL_TEST=""

AC_MSG_CHECKING(for Msql support)

AC_ARG_WITH(msql,
	[  --with-msql           Include MiniSQL (mSQL) support.],
	[  ac_msql="$withval" ])
AC_ARG_WITH(msql-incdir,
	[  --with-msql-incdir    Specifies where the mSQL include files are.],
	[  ac_msql_incdir="$withval" ])
AC_ARG_WITH(msql-libdir,
	[  --with-msql-libdir    Specifies where the mSQL libraries are.],
	[  ac_msql_libdir="$withval" ])

if test "$ac_msql" = "yes"; then
	AC_MSG_RESULT([yes])
	if test "$ac_msql_incdir" = "no"; then
		AC_CHECK_HEADER([msql.h])
		MSQL_INCLUDE=""
	else
		MSQL_INCLUDE=-I$ac_msql_incdir
	fi
	if test "$ac_msql_libdir" = "no"; then
		AC_SEARCH_LIBS_VAR(msql_exec, msql, , , , MSQL_LIBS)
		MSQL_LDFLAGS=""
	else
		MSQL_LIBS=-lmsql
		MSQL_LDFLAGS=-L$ac_msql_libdir
	fi

	MSQL_TEST="test_msql.sh"

	AM_CONDITIONAL(HAVE_MSQL, true)
	
	AC_SUBST(MSQL_LIBS)
	AC_SUBST(MSQL_INCLUDE)
	AC_SUBST(MSQL_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(MSQL_TEST)
])


## Oracle

AC_DEFUN([AC_CHECK_ORACLE],
[
AM_CONDITIONAL(HAVE_ORACLE, false)
ac_oracle="no"
ac_oracle_incdir="no"
ac_oracle_libdir="no"

# exported variables
ORACLE_LIBS=""
ORACLE_LDFLAGS=""
ORACLE_INCLUDE=""
ORACLE_TEST=""

AC_MSG_CHECKING(for Oracle support)

AC_ARG_WITH(oracle,
	[  --with-oracle           Include Oracle support.],
	[  ac_oracle="$withval" ])
AC_ARG_WITH(oracle-dir,
	[  --with-oracle-dir       Specifies ORACLE_HOME.],
	[  ac_oracle_incdir="$withval"/rdbms/demo
	   ac_oracle_libdir="$withval"/lib ])
AC_ARG_WITH(oracle-incdir,
	[  --with-oracle-incdir    Specifies where the Oracle include files are.],
	[  ac_oracle_incdir="$withval" ])
AC_ARG_WITH(oracle-libdir,
	[  --with-oracle-libdir    Specifies where the Oracle libraries are.],
	[  ac_oracle_libdir="$withval" ])

if test "$ac_oracle" = "yes"; then
	if test "$ac_oracle_incdir" = "no" || test "$ac_oracle_libs" = "no"; then
		oracle_incdirs="$ORACLE_HOME/rdbms/demo $ORACLE_HOME/rdbms/public"
		AC_FIND_FILE(oci.h, $oracle_incdirs, ac_oracle_incdir)
		oracle_libdirs="$ORACLE_HOME/lib"
		AC_FIND_FILE(libclntsh.so, $oracle_libdirs, ac_oracle_libdir)
		if test "$ac_oracle_incdir" = "no"; then
			AC_MSG_RESULT(no)
			AC_MSG_ERROR([Invalid Oracle directory - include files not found.])
		fi
		if test "$ac_oracle_libdir" = "no"; then
			AC_MSG_RESULT(no)
			AC_MSG_ERROR([Invalid Oracle directory - libraries not found.])
		fi
	fi
	AC_MSG_RESULT([yes: libs in $ac_oracle_libdir, headers in $ac_oracle_incdir])
	AM_CONDITIONAL(HAVE_ORACLE, true)
	
	ORACLE_LIBS=-lclntsh
	ORACLE_INCLUDE="-I$ac_oracle_incdir -I$ORACLE_HOME/rdbms/public"
	ORACLE_LDFLAGS=-L$ac_oracle_libdir
	ORACLE_TEST="test_oracle.sh"
	
	AC_SUBST(ORACLE_LIBS)
	AC_SUBST(ORACLE_INCLUDE)
	AC_SUBST(ORACLE_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(ORACLE_TEST)
])

## Firebird

AC_DEFUN([AC_CHECK_FIREBIRD_INTERBASE],
[
AM_CONDITIONAL(HAVE_FIREBIRD_INTERBASE, false)
ac_firebird="no"
ac_firebird_incdir="no"
ac_firebird_libdir="no"

# exported variables
FIREBIRD_LIBS=""
FIREBIRD_LDFLAGS=""
FIREBIRD_INCLUDE=""
FIREBIRD_TEST=""

AC_MSG_CHECKING(for Firebird/Interbase support)

AC_ARG_WITH(firebird,
	[  --with-firebird           Include Firebird/Interbase support.],
	[  ac_firebird="$withval" ])
AC_ARG_WITH(firebird-incdir,
	[  --with-firebird-incdir    Specifies where the Firebird/Interbase include files are.],
	[  ac_firebird_incdir="$withval" ])
AC_ARG_WITH(firebird-libdir,
	[  --with-firebird-libdir    Specifies where the Firebird/Interbase libraries are.],
	[  ac_firebird_libdir="$withval" ])

if test "$ac_firebird" = "yes"; then
	AC_MSG_RESULT([yes])
	if test "$ac_firebird_incdir" = "no"; then
		AC_CHECK_HEADER([ibase.h])
		FIREBIRD_INCLUDE=""
	else
		FIREBIRD_INCLUDE=-I$ac_firebird_incdir
	fi
	if test "$ac_firebird_libdir" = "no"; then
		AC_SEARCH_LIBS_VAR(isc_dsql_fetch, fbembed, , , , FIREBIRD_LIBS)
		FIREBIRD_LDFLAGS=""
	else
		FIREBIRD_LIBS=-lfbembed
		FIREBIRD_LDFLAGS=-L$ac_firebird_libdir
	fi

	AC_CHECK_PROGS(FIREBIRD_ISQL,[isql-fb isql])
	if test "x$FIREBIRD_ISQL != x"; then
	   	FIREBIRD_ISQL_FLAG="-DFIREBIRD_ISQL=\\\"$FIREBIRD_ISQL\\\""
	else
		FIREBIRD_ISQL_FLAG=""
	fi

	FIREBIRD_TEST="test_firebird.sh"

	AM_CONDITIONAL(HAVE_FIREBIRD_INTERBASE, true)
	
	AC_SUBST(FIREBIRD_LIBS)
	AC_SUBST(FIREBIRD_INCLUDE)
	AC_SUBST(FIREBIRD_LDFLAGS)
	AC_SUBST(FIREBIRD_ISQL_FLAG)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(FIREBIRD_TEST)
])


## Freetds

AC_DEFUN([AC_CHECK_FREETDS],
[
AM_CONDITIONAL(HAVE_FREETDS, false)
ac_freetds="no"
ac_freetds_incdir="no"
ac_freetds_libdir="no"

# exported variables
FREETDS_LIBS=""
FREETDS_LDFLAGS=""
FREETDS_INCLUDE=""
FREETDS_TEST=""

AC_MSG_CHECKING(for Freetds support)

AC_ARG_WITH(freetds,
	[  --with-freetds           Include Freetds support.],
	[  ac_freetds="$withval" ])
AC_ARG_WITH(freetds-incdir,
	[  --with-freetds-incdir    Specifies where the Freetds include files are.],
	[  ac_freetds_incdir="$withval" ])
AC_ARG_WITH(freetds-libdir,
	[  --with-freetds-libdir    Specifies where the Freetds libraries are.],
	[  ac_freetds_libdir="$withval" ])

if test "$ac_freetds" = "yes"; then
	AC_MSG_RESULT([yes])
	if test "$ac_freetds_incdir" = "no"; then
		AC_CHECK_HEADER([tds.h])
		FREETDS_INCLUDE=""
	else
		FREETDS_INCLUDE=-I$ac_freetds_incdir
	fi
	if test "$ac_freetds_libdir" = "no"; then
		AC_SEARCH_LIBS_VAR(ct_command, ct, , , , FREETDS_LIBS)
		FREETDS_LDFLAGS=""
	else
		FREETDS_LIBS=-lct
		FREETDS_LDFLAGS=-L$ac_freetds_libdir
	fi

	FREETDS_TEST="test_freetds.sh"

	AM_CONDITIONAL(HAVE_FREETDS, true)
	
	AC_SUBST(FREETDS_LIBS)
	AC_SUBST(FREETDS_INCLUDE)
	AC_SUBST(FREETDS_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(FREETDS_TEST)
])


## Ingres

AC_DEFUN([AC_CHECK_INGRES],
[
AM_CONDITIONAL(HAVE_INGRES, false)
ac_ingres="no"
ac_ingres_incdir="no"
ac_ingres_libdir="no"

# exported variables
INGRES_LIBS=""
INGRES_LDFLAGS=""
INGRES_INCLUDE=""
INGRES_TEST=""

AC_MSG_CHECKING(for Ingres support)

AC_ARG_WITH(ingres,
	[  --with-ingres           Include Ingres support.],
	[  ac_ingres="$withval" ])
AC_ARG_WITH(ingres-dir,
	[  --with-ingres-dir       Specifies II_SYSTEM.],
	[  ac_ingres_incdir="$withval"/ingres/files
	   ac_ingres_libdir="$withval"/ingres/lib ])
AC_ARG_WITH(ingres-incdir,
	[  --with-ingres-incdir    Specifies where the Ingres include files are.],
	[  ac_ingres_incdir="$withval" ])
AC_ARG_WITH(ingres-libdir,
	[  --with-ingres-libdir    Specifies where the Ingres libraries are.],
	[  ac_ingres_libdir="$withval" ])

if test "$ac_ingres" = "yes"; then
	if test "$ac_ingres_incdir" = "no" || test "$ac_ingres_libs" = "no"; then
		ingres_incdirs="/usr/include /usr/local/include /opt/Ingres/IngresII/ingres/files"
		AC_FIND_FILE(iiapi.h, $ingres_incdirs, ac_ingres_incdir)
		ingres_libdirs="/usr/lib /usr/local/lib /opt/Ingres/IngresII/ingres/lib"
		AC_FIND_FILE(libingres.a, $ingres_libdirs, ac_ingres_libdir)
		if test "$ac_ingres_incdir" = "no"; then
			AC_MSG_RESULT(no)
			AC_MSG_ERROR([Invalid Ingres directory - include files not found.])
		fi
		if test "$ac_ingres_libdir" = "no"; then
			AC_MSG_RESULT(no)
			AC_MSG_ERROR([Invalid Ingres directory - libraries not found.])
		fi
	fi
	AC_MSG_RESULT([yes: libs in $ac_ingres_libdir, headers in $ac_ingres_incdir])
	AM_CONDITIONAL(HAVE_INGRES, true)
	
	INGRES_LIBS="-lingres -lpthread -ldl -lm -lcrypt"
	INGRES_INCLUDE=-I$ac_ingres_incdir
	INGRES_LDFLAGS=-L$ac_ingres_libdir
	INGRES_TEST="test_ingres.sh"
	
	AC_SUBST(INGRES_LIBS)
	AC_SUBST(INGRES_INCLUDE)
	AC_SUBST(INGRES_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(INGRES_TEST)
])

## Db2

AC_DEFUN([AC_CHECK_DB2],
[
AM_CONDITIONAL(HAVE_DB2, false)
ac_db2="no"
ac_db2_incdir="no"
ac_db2_libdir="no"

# exported variables
DB2_LIBS=""
DB2_LDFLAGS=""
DB2_INCLUDE=""
DB2_TEST=""

AC_MSG_CHECKING(for IBM DB2 support)

AC_ARG_WITH(db2,
	[  --with-db2           Include IBM DB2 support.],
	[  ac_db2="$withval" ])
AC_ARG_WITH(db2-dir,
	[  --with-db2-dir       Specifies DB2_HOME.],
	[  ac_db2_incdir="$withval"/include
	   ac_db2_libdir="$withval"/lib ])
AC_ARG_WITH(db2-incdir,
	[  --with-db2-incdir    Specifies where the IBM DB2 include files are.],
	[  ac_db2_incdir="$withval" ])
AC_ARG_WITH(db2-libdir,
	[  --with-db2-libdir    Specifies where the IBM DB2 libraries are.],
	[  ac_db2_libdir="$withval" ])

if test "$ac_db2" = "yes"; then
	if test "$ac_db2_incdir" = "no" || test "$ac_db2_libs" = "no"; then
		db2_incdirs="$DB2_HOME/rdbms/demo $DB2_HOME/rdbms/public"
		AC_FIND_FILE(sqlcli1.h, $db2_incdirs, ac_db2_incdir)
		db2_libdirs="$ORACLE_HOME/lib"
		AC_FIND_FILE(libdb2.so, $db2_libdirs, ac_db2_libdir)
		if test "$ac_db2_incdir" = "no"; then
			AC_MSG_RESULT(no)
			AC_MSG_ERROR([Invalid IBM DB2 directory - include files not found.])
		fi
		if test "$ac_db2_libdir" = "no"; then
			AC_MSG_RESULT(no)
			AC_MSG_ERROR([Invalid IBM DB2 directory - libraries not found.])
		fi
	fi
	AC_MSG_RESULT([yes: libs in $ac_db2_libdir, headers in $ac_db2_incdir])
	AM_CONDITIONAL(HAVE_DB2, true)
	
	DB2_LIBS=-ldb2
	DB2_INCLUDE="-I$ac_db2_incdir -I$DB2_HOME/include"
	DB2_LDFLAGS=-L$ac_db2_libdir
	DB2_TEST="test_db2.sh"
	
	AC_SUBST(DB2_LIBS)
	AC_SUBST(DB2_INCLUDE)
	AC_SUBST(DB2_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(DB2_TEST)
])
