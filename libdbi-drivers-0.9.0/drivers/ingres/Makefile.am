AUTOMAKE_OPTIONS = foreign

docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

if HAVE_INGRES

if BUILDDOCS
htdocdir = $(docdir)/dbd_ingres
doc_DATA = dbd_ingres.pdf
htdoc_DATA = dbd_ingres/* 
else
htdocdir =
doc_DATA =
htdoc_DATA =
endif

ingres_ltlibs = libdbdingres.la
ingres_ldflags = -module -avoid-version
ingres_sources = dbd_ingres.c months.gperf.c

else

htdocdir =
doc_DATA =
htdoc_DATA =

ingres_ltlibs =
ingres_ldflags =
ingres_sources =

endif

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/include @DBI_INCLUDE@ @INGRES_INCLUDE@ @LIBADD_LIBDBI@

libdir = $(driverdir)
lib_LTLIBRARIES = $(ingres_ltlibs)
libdbdingres_la_LDFLAGS = $(ingres_ldflags) @INGRES_LDFLAGS@
libdbdingres_la_LIBADD = @INGRES_LIBS@
libdbdingres_la_SOURCES = $(ingres_sources)
libdbdingres_la_DEPENDENCIES = dbd_ingres.h

# FIXME: make complains about a circular dependency (due to implicit rules??)
.INTERMEDIATE : months.gperf.c
months.gperf.c : months.gperf
	gperf --output-file=$@ $<

EXTRA_DIST = dbd_ingres.c dbd_ingres.h months.gperf AUTHORS TODO README dbd_ingres.sgml dbd_ingres.pdf dbd_ingres/*

# jadetex spews out tons of useless crap. shut it up:
dbd_ingres.pdf: dbd_ingres.sgml
	@echo "Converting dbd_ingres.sgml to PDF...";
	openjade -t tex -V tex-backend -d $(top_srcdir)/doc/include/doc-print.dsl dbd_ingres.sgml
# run jadetex three times to get the references right
	pdftex "&pdfjadetex" dbd_ingres.tex
	pdftex "&pdfjadetex" dbd_ingres.tex
	pdftex "&pdfjadetex" dbd_ingres.tex

dbd_ingres/*: dbd_ingres.sgml
	@echo "Converting dbd_ingres.sgml to HTML...";
	mkdir -p dbd_ingres && cd dbd_ingres && openjade -t sgml -d $(top_srcdir)/../doc/include/doc-html.dsl ../dbd_ingres.sgml


maintainer-clean: clean
	rm -f dbd_ingres.pdf dbd_ingres/*

