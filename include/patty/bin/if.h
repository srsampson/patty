#ifndef _PATTY_BIN_IF_H
#define _PATTY_BIN_IF_H

#include <patty/ax25/if.h>
#include <patty/error.h>

patty_ax25_if *patty_bin_if_create(int argc,
                                   char **argv,
                                   char **ifname,
                                   patty_error *e);

#endif /* _PATTY_BIN_IF_H */
