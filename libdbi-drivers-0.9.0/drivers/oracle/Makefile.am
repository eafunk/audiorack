## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

if HAVE_ORACLE

if BUILDDOCS
htdocdir = $(docdir)/dbd_oracle
doc_DATA = dbd_oracle.pdf
htdoc_DATA = dbd_oracle/* 
else
htdocdir =
doc_DATA =
htdoc_DATA =
endif

oracle_ltlibs = libdbdoracle.la
oracle_ldflags = -module -avoid-version
oracle_sources = dbd_oracle.c

else

htdocdir =
doc_DATA =
htdoc_DATA =

oracle_ltlibs =
oracle_ldflags =
oracle_sources =

endif

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/include @DBI_INCLUDE@ @ORACLE_INCLUDE@

libdir = $(driverdir)
lib_LTLIBRARIES = $(oracle_ltlibs)
libdbdoracle_la_LDFLAGS = $(oracle_ldflags) @ORACLE_LDFLAGS@ @ORACLE_LIBS@ @LIBADD_LIBDBI@
libdbdoracle_la_SOURCES = $(oracle_sources)
libdbdoracle_la_DEPENDENCIES = dbd_oracle.h

EXTRA_DIST = dbd_oracle.c dbd_oracle.h oracle_charsets.h AUTHORS TODO README dbd_oracle.sgml dbd_oracle.pdf dbd_oracle/*

# jadetex spews out tons of useless crap. shut it up:
dbd_oracle.pdf: dbd_oracle.sgml
	@echo "Converting dbd_oracle.sgml to PDF...";
	openjade -t tex -V tex-backend -d $(top_srcdir)/doc/include/doc-print.dsl dbd_oracle.sgml
# run jadetex three times to get the references right
	pdftex "&pdfjadetex" dbd_oracle.tex
	pdftex "&pdfjadetex" dbd_oracle.tex
	pdftex "&pdfjadetex" dbd_oracle.tex

dbd_oracle/*: dbd_oracle.sgml
	@echo "Converting dbd_oracle.sgml to HTML...";
	mkdir -p dbd_oracle && cd dbd_oracle && openjade -t sgml -d $(top_srcdir)/../doc/include/doc-html.dsl ../dbd_oracle.sgml


maintainer-clean: clean
	rm -f dbd_oracle.pdf dbd_oracle/*

