#ifndef _PATTY_BIN_KISS_H
#define _PATTY_BIN_KISS_H

#include <patty/error.h>

#include <patty/kiss/tnc.h>

int patty_bin_kiss_config(int argc,
                          char **argv,
                          patty_kiss_tnc_info *info,
                          patty_error *e);

#endif /* _PATTY_BIN_KISS_H */
