## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign dist-zip

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = drivers tests @docs_subdirs@

EXTRA_DIST = AUTHORS COPYING ChangeLog INSTALL README README.win32 README.osx README.encodings TODO libdbi-drivers.spec.in config.h.in Makefile.w32 autogen.sh fixlt.sh

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"
