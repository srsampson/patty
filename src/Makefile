include ../mk/build.mk

INCLUDE_PATH	= ../include
HEADERS_SUBDIR	= patty

CC		 = $(CROSS)cc
CFLAGS		+= -I$(INCLUDE_PATH)
LDFLAGS		+= -lutil

HEADERS		= kiss.h kiss/tnc.h ax25/aprs_is.h ax25.h client.h ax25/if.h \
		  ax25/frame.h ax25/sock.h ax25/route.h ax25/server.h daemon.h \
		  error.h list.h hash.h dict.h timer.h print.h util.h conf.h

OBJS		= kiss.o tnc.o aprs_is.o ax25.o client.o if.o \
		  frame.o sock.o route.o server.o daemon.o \
		  error.o list.o hash.o dict.o timer.o print.o util.o conf.o

VERSION_MAJOR	= 0
VERSION_MINOR	= 0.1
VERSION		= $(VERSION_MAJOR).$(VERSION_MINOR)

LIBNAME		= patty

HEADERS_BUILD	= $(addprefix $(INCLUDE_PATH)/$(HEADERS_SUBDIR)/, $(HEADERS))

all: $(STATIC) $(SONAME_FULL) $(SONAME) $(SONAME_SHORT)

$(STATIC): $(OBJS)
	$(AR) rc $(STATIC) $(OBJS)
	$(RANLIB) $(STATIC)

$(SONAME_FULL): $(OBJS)
	$(CC) $(LDFLAGS_SO) -o $(SONAME_FULL) $(OBJS) $(LDFLAGS)

$(SONAME): $(SONAME_FULL)
	$(LN) -s $< $@

$(SONAME_SHORT): $(SONAME_FULL)
	$(LN) -s $< $@

$(OBJS): %.o: %.c $(HEADERS_BUILD)
	$(CC) $(CFLAGS) -c $<

install: $(SONAME_FULL) $(STATIC)
	$(INSTALL) -c -m 0644 $(STATIC) $(PREFIX)/lib
	$(INSTALL) -c -m 0755 $(SONAME_FULL) $(PREFIX)/lib
	$(LN) -s -f $(SONAME_FULL) $(PREFIX)/lib/$(SONAME)
	$(LN) -s -f $(SONAME_FULL) $(PREFIX)/lib/$(SONAME_SHORT)
	$(INSTALL) -d -m 0755 $(PREFIX)/include/$(HEADERS_SUBDIR)
	$(INSTALL) -c -m 0644 $(HEADERS_BUILD) $(PREFIX)/include/$(HEADERS_SUBDIR)

clean:
	$(RM) -f $(SONAME_SHORT) $(SONAME) $(SONAME_FULL) $(STATIC) $(OBJS)
