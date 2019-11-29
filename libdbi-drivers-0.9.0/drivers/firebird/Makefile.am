## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

if HAVE_FIREBIRD_INTERBASE

if BUILDDOCS
# don't use docdir, doc_DATA. This way we keep the HTML in separate subdirs
# for each driver
htdocdir = $(docdir)/dbd_firebird
doc_DATA = dbd_firebird.pdf
htdoc_DATA = dbd_firebird/* 
else
htdocdir =
doc_DATA =
htdoc_DATA =
endif

firebird_ltlibs = libdbdfirebird.la
firebird_ldflags = -module -avoid-version
firebird_sources = dbd_firebird.c utility.c 

else

firebird_ltlibs =
firebird_ldflags =
firebird_sources =

endif

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/include @DBI_INCLUDE@ @FIREBIRD_INCLUDE@

libdir = $(driverdir)
lib_LTLIBRARIES = $(firebird_ltlibs)
libdbdfirebird_la_LDFLAGS = $(firebird_ldflags) @FIREBIRD_LDFLAGS@ @FIREBIRD_LIBS@ @LIBADD_LIBDBI@
libdbdfirebird_la_SOURCES = $(firebird_sources)
libdbdfirebird_la_DEPENDENCIES = dbd_firebird.h utility.h firebird_charsets.h

AM_CPPFLAGS=-DDBDIR=\"@dbi_dbdir@/firebird\"

EXTRA_DIST = dbd_firebird.c utility.c dbd_firebird.h utility.h firebird_charsets.h dbd_firebird.sgml dbd_firebird.pdf $(htdoc_data)

# jadetex spews out tons of useless crap. shut it up:
dbd_firebird.pdf: dbd_firebird.sgml
	@echo "Converting dbd_firebird.sgml to PDF...";
	openjade -t tex -V tex-backend -d $(top_srcdir)/doc/include/doc-print.dsl dbd_firebird.sgml
# run jadetex three times to get the references right
	pdftex "&pdfjadetex" dbd_firebird.tex
	pdftex "&pdfjadetex" dbd_firebird.tex
	pdftex "&pdfjadetex" dbd_firebird.tex

dbd_firebird/*: dbd_firebird.sgml
	@echo "Converting dbd_firebird.sgml to HTML...";
	mkdir -p dbd_firebird && cd dbd_firebird && openjade -t sgml -d $(top_srcdir)/../doc/include/doc-html.dsl ../dbd_firebird.sgml


maintainer-clean: clean
	rm -f dbd_firebird.pdf dbd_firebird/*

install-exec-local:
	$(mkinstalldirs) $(DESTDIR)@dbi_dbdir@/firebird
	chmod 1777 $(DESTDIR)@dbi_dbdir@/firebird
