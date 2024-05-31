include ../mk/build.mk

CC		= $(CROSS)cc

INCLUDE_PATH	= ../include
HEADERS_SUBDIR	= patty/bin

CFLAGS		+= -I$(INCLUDE_PATH)
LDFLAGS		+= -L../src -lpatty

PROGRAMS	= pattyd ax25dump tncd
MAN8PAGES	= pattyd.8 ax25dump.8 tncd.8
MANPAGES	= $(MAN8PAGES)

HEADERS		= kiss.h if.h
HEADERS_BUILD	= $(addprefix $(INCLUDE_PATH)/$(HEADERS_SUBDIR)/, $(HEADERS))

OBJS		= pattyd.o ax25dump.o kiss.o if.o tncd.o

all: $(PROGRAMS)

install: $(PROGRAMS) $(MANPAGES)
	$(INSTALL) -d -m 0755 $(MANDIR)/man8
	$(INSTALL) -c -m 0644 $(MAN8PAGES) $(MANDIR)/man8
	$(INSTALL) -c -m 0755 $(PROGRAMS) $(PREFIX)/bin

pattyd: pattyd.o kiss.o if.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ax25dump: ax25dump.o kiss.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tncd: tncd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): %.o: %.c $(HEADERS_BUILD)
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) -f $(PROGRAMS) $(OBJS)
