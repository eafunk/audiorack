AUTOMAKE_OPTIONS = foreign

docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

if HAVE_DB2

if BUILDDOCS
htdocdir = $(docdir)/dbd_db2
doc_DATA = dbd_db2.pdf
htdoc_DATA = dbd_db2/* 
else
htdocdir =
doc_DATA =
htdoc_DATA =
endif

db2_ltlibs = libdbddb2.la
db2_ldflags = -module -avoid-version
db2_sources = dbd_db2.c

else

htdocdir =
doc_DATA =
htdoc_DATA =

db2_ltlibs =
db2_ldflags =
db2_sources =

endif

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/include @DBI_INCLUDE@ @DB2_INCLUDE@

libdir = $(driverdir)
lib_LTLIBRARIES = $(db2_ltlibs)
libdbddb2_la_LDFLAGS = $(db2_ldflags) @DB2_LDFLAGS@ @DB2_LIBS@ @LIBADD_LIBDBI@
libdbddb2_la_SOURCES = $(db2_sources)
libdbddb2_la_DEPENDENCIES = dbd_db2.h

EXTRA_DIST = dbd_db2.c dbd_db2.h AUTHORS TODO README dbd_db2.sgml dbd_db2.pdf dbd_db2/*


dbd_db2.pdf: dbd_db2.sgml
	@echo "Converting dbd_db2.sgml to PDF...";
	openjade -t tex -V tex-backend -d $(top_srcdir)/doc/include/doc-print.dsl dbd_db2.sgml
# run jadetex three times to get the references right
	pdftex "&pdfjadetex" dbd_db2.tex
	pdftex "&pdfjadetex" dbd_db2.tex
	pdftex "&pdfjadetex" dbd_db2.tex

dbd_db2/*: dbd_db2.sgml
	@echo "Converting dbd_db2.sgml to HTML...";
	mkdir -p dbd_db2 && cd dbd_db2 && openjade -t sgml -d $(top_srcdir)/../doc/include/doc-html.dsl ../dbd_db2.sgml


maintainer-clean: clean
	rm -f dbd_db2.pdf dbd_db2/*


#distclean: clean
#	rm -f dbd_db2.pdf dbd_db2/*

# manually install the docs
#install-data-local: 
#if HAVE_DB2
#	$(top_srcdir)/mkinstalldirs $(DESTDIR)$(doc_dir) $(DESTDIR)$(doc_dir)/$(htdoc_subdir)
#	@for f in $(noinst_DATA); do \
#		$(INSTALL_DATA) $(srcdir)/$$f $(DESTDIR)$(doc_dir)/$$f; \
#	done
#endif
