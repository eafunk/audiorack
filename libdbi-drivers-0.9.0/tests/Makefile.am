## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = cgreen

TESTS = @MYSQL_TEST@ @PGSQL_TEST@ @SQLITE_TEST@ @SQLITE3_TEST@ @MSQL_TEST@ @ORACLE_TEST@ @FIREBIRD_TEST@ @FREETDS_TEST@ @INGRES_TEST@ @DB2_TEST@ @LIBDBI_TEST@
check_PROGRAMS = test_dbi test_dbi_dlopen
check_LTLIBRARIES = libtest_dbi_plugin.la
test_dbi_SOURCES = test_dbi.c
test_dbi_LDADD = $(top_srcdir)/tests/cgreen/libcgreen.a -L@libdir@ -lm -ldbi @LIBADD_LIBDBI@
test_dbi_dlopen_SOURCES = test_dbi_dlopen.c
test_dbi_dlopen_LDFLAGS = @LIBADD_LIBDBI@
libtest_dbi_plugin_la_SOURCES = test_dbi_plugin.c
libtest_dbi_plugin_la_LIBADD = @LIBADD_LIBDBI@
libtest_dbi_plugin_la_LDFLAGS = -module -rpath /nowhere
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/include -I$(top_srcdir)/tests/cgreen/include -I@includedir@
CFLAGS = -DDBI_DRIVER_DIR=\"@driverdir@\" @FIREBIRD_ISQL_FLAG@ -g
AM_CPPFLAGS = -DDBDIR=\"@dbi_dbdir@\" @DBI_INCLUDE@

EXTRA_DIST = test_mysql.sh test_pgsql.sh test_sqlite.sh test_sqlite3.sh test_msql.sh test_oracle.sh test_firebird.sh test_freetds.sh test_ingres.sh test_db2.sh test_libdbi.sh

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

