include ../mk/build.mk

CC		= $(CROSS)cc

INCLUDE_PATH	= ../include

CFLAGS		+= -I$(INCLUDE_PATH)
LDFLAGS		+= -L../src -lpatty -lutil

EXAMPLES	= connect listen login

all: $(EXAMPLES)

$(EXAMPLES): %: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	$(RM) -f $(EXAMPLES)
