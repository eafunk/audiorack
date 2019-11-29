## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

if HAVE_MSQL

if BUILDDOCS
htdocdir = $(docdir)/dbd_msql
doc_data = dbd_msql.pdf
htdoc_DATA = dbd_msql/* 
else
htdocdir =
doc_DATA =
htdoc_DATA =
endif

msql_ltlibs = libdbdmsql.la
msql_ldflags = -module -avoid-version
msql_sources = dbd_msql.c

else

htdocdir =
doc_DATA =
htdoc_DATA =

msql_ltlibs =
msql_ldflags =
msql_sources =

endif

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/include @DBI_INCLUDE@ @MSQL_INCLUDE@

libdir = $(driverdir)
lib_LTLIBRARIES = $(msql_ltlibs)
libdbdmsql_la_LDFLAGS = $(msql_ldflags) @MSQL_LDFLAGS@ @MSQL_LIBS@ @LIBADD_LIBDBI@
libdbdmsql_la_SOURCES = $(msql_sources)
libdbdmsql_la_DEPENDENCIES = dbd_msql.h

EXTRA_DIST = dbd_msql.c dbd_msql.h dbd_msql.sgml dbd_msql.pdf dbd_msql/*

dbd_msql.pdf: dbd_msql.sgml
	@echo "Converting dbd_msql.sgml to PDF...";
	openjade -t tex -V tex-backend -d $(top_srcdir)/doc/include/doc-print.dsl dbd_msql.sgml
# run jadetex three times to get the references right
	pdftex "&pdfjadetex" dbd_msql.tex
	pdftex "&pdfjadetex" dbd_msql.tex
	pdftex "&pdfjadetex" dbd_msql.tex

dbd_msql/*: dbd_msql.sgml
	@echo "Converting dbd_msql.sgml to HTML...";
	mkdir -p dbd_msql && cd dbd_msql && openjade -t sgml -d $(top_srcdir)/../doc/include/doc-html.dsl ../dbd_msql.sgml

maintainer-clean: clean
	rm -f dbd_msql.pdf dbd_msql/*


