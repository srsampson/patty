include ../mk/build.mk

CC		= $(CROSS)cc

MANPAGES	= pattyd.conf.5

all: $(MANPAGES)

install: $(MANPAGES)
	$(INSTALL) -d -m 0755 $(MANDIR)/man5
	$(INSTALL) -c -m 0644 pattyd.conf.5 $(MANDIR)/man5
