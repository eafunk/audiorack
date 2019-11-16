# TOP PROJECT LEVEL Makefile
# This will recursivly call the Makefile inside each of the following subdirectories

SUBDIRS = arPlayer4 arRecorder4 arServer4
MAKE = make

all:
	@for i in $(SUBDIRS); do \
	echo "make $$i..."; \
	(cd $$i; $(MAKE) all); done

install:
	@for i in $(SUBDIRS); do \
	echo "Installing $$i..."; \
	(cd $$i; $(MAKE) install); done

clean:
	@for i in $(SUBDIRS); do \
	echo "Clearing $$i..."; \
	(cd $$i; $(MAKE) clean); done
