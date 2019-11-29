## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

if HAVE_PGSQL

if BUILDDOCS
htdocdir = $(docdir)/dbd_pgsql
doc_DATA = dbd_pgsql.pdf
htdoc_DATA = dbd_pgsql/* 
else
htdocdir =
doc_DATA =
htdoc_DATA =
endif

pgsql_ltlibs = libdbdpgsql.la
pgsql_ldflags = -module -avoid-version
pgsql_sources = dbd_pgsql.c

else

htdocdir =
doc_DATA =
htdoc_DATA =

pgsql_ltlibs =
pgsql_ldflags =
pgsql_sources =

endif

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/include @DBI_INCLUDE@ @PGSQL_INCLUDE@

libdir = $(driverdir)
lib_LTLIBRARIES = $(pgsql_ltlibs)
libdbdpgsql_la_LDFLAGS = $(pgsql_ldflags) @PGSQL_LDFLAGS@ @PGSQL_LIBS@ @LIBADD_LIBDBI@
libdbdpgsql_la_SOURCES = $(pgsql_sources)
libdbdpgsql_la_DEPENDENCIES = dbd_pgsql.h

EXTRA_DIST = dbd_pgsql.c dbd_pgsql.h AUTHORS TODO README dbd_pgsql.sgml dbd_pgsql.pdf dbd_pgsql/*

# jadetex spews out tons of useless crap. shut it up:
dbd_pgsql.pdf: dbd_pgsql.sgml
	@echo "Converting dbd_pgsql.sgml to PDF...";
	openjade -t tex -V tex-backend -d $(top_srcdir)/doc/include/doc-print.dsl dbd_pgsql.sgml
# run jadetex three times to get the references right
	pdftex "&pdfjadetex" dbd_pgsql.tex
	pdftex "&pdfjadetex" dbd_pgsql.tex
	pdftex "&pdfjadetex" dbd_pgsql.tex

dbd_pgsql/*: dbd_pgsql.sgml
	@echo "Converting dbd_pgsql.sgml to HTML...";
	mkdir -p dbd_pgsql && cd dbd_pgsql && openjade -t sgml -d $(top_srcdir)/../doc/include/doc-html.dsl ../dbd_pgsql.sgml


maintainer-clean: clean
	rm -f dbd_pgsql.pdf dbd_pgsql/*

