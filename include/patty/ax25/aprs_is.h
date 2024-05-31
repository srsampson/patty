#ifndef _PATTY_AX25_APRS_IS_H
#define _PATTY_AX25_APRS_IS_H

#include <stdint.h>

#define PATTY_AX25_APRS_IS_DEFAULT_APPNAME "patty-aprs-is"
#define PATTY_AX25_APRS_IS_DEFAULT_VERSION "0.0.0"

#define PATTY_AX25_APRS_IS_PAYLOAD_MAX 256
#define PATTY_AX25_APRS_IS_PACKET_MAX  512
#define PATTY_AX25_APRS_IS_FRAME_MAX   PATTY_AX25_APRS_IS_PACKET_MAX

#define PATTY_AX25_APRS_IS_ATTEMPTS_MAX 3

typedef struct _patty_ax25_aprs_is_info {
    const char *host,
               *port,
               *user,
               *pass,
               *appname,
               *version,
               *filter;
} patty_ax25_aprs_is_info;

typedef struct _patty_ax25_aprs_is patty_ax25_aprs_is;

ssize_t patty_ax25_aprs_is_encode(void *dest,
                                  const char *src,
                                  size_t len,
                                  size_t size);

patty_ax25_aprs_is *patty_ax25_aprs_is_new(patty_ax25_aprs_is_info *info);

void patty_ax25_aprs_is_destroy(patty_ax25_aprs_is *aprs);

int patty_ax25_aprs_is_fd(patty_ax25_aprs_is *aprs);

int patty_ax25_aprs_is_ready(patty_ax25_aprs_is *aprs, fd_set *fds);

int patty_ax25_aprs_is_reset(patty_ax25_aprs_is *aprs);

ssize_t patty_ax25_aprs_is_fill(patty_ax25_aprs_is *aprs);

ssize_t patty_ax25_aprs_is_drain(patty_ax25_aprs_is *aprs,
                                 void *buf,
                                 size_t len);

int patty_ax25_aprs_is_pending(patty_ax25_aprs_is *aprs);

ssize_t patty_ax25_aprs_is_flush(patty_ax25_aprs_is *aprs);

ssize_t patty_ax25_aprs_is_send(patty_ax25_aprs_is *aprs,
                                const void *buf,
                                size_t len);

patty_ax25_if_driver *patty_ax25_aprs_is_driver();

#endif /* _PATTY_AX25_APRS_IS_H */
