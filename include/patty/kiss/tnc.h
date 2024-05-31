#ifndef _PATTY_KISS_TNC_H
#define _PATTY_KISS_TNC_H

#include <sys/types.h>
#include <termios.h>

#define PATTY_KISS_TNC_BUFSZ 4096
#define PATTY_KISS_TNC_PORT     0

typedef struct _patty_kiss_tnc patty_kiss_tnc;

#define PATTY_KISS_TNC_DEVICE (1 << 0)
#define PATTY_KISS_TNC_FD     (1 << 1)
#define PATTY_KISS_TNC_BAUD   (1 << 2)
#define PATTY_KISS_TNC_FLOW   (1 << 3)

enum patty_kiss_tnc_flow {
    PATTY_KISS_TNC_FLOW_NONE,
    PATTY_KISS_TNC_FLOW_CRTSCTS,
    PATTY_KISS_TNC_FLOW_XONXOFF
};

typedef struct _patty_kiss_tnc_info {
    int flags;

    const char *device;
    int fd;
    speed_t baud;
    enum patty_kiss_tnc_flow flow;
} patty_kiss_tnc_info;

patty_ax25_if_driver *patty_kiss_tnc_driver();

patty_kiss_tnc *patty_kiss_tnc_new(patty_kiss_tnc_info *info);

int patty_kiss_tnc_fd(patty_kiss_tnc *tnc);

int patty_kiss_tnc_ready(patty_kiss_tnc *tnc, fd_set *fds);

int patty_kiss_tnc_reset(patty_kiss_tnc *tnc);

void patty_kiss_tnc_destroy(patty_kiss_tnc *tnc);

ssize_t patty_kiss_tnc_fill(patty_kiss_tnc *tnc);

ssize_t patty_kiss_tnc_drain(patty_kiss_tnc *tnc, void *buf, size_t len);

int patty_kiss_tnc_pending(patty_kiss_tnc *tnc);

ssize_t patty_kiss_tnc_flush(patty_kiss_tnc *tnc);

ssize_t patty_kiss_tnc_recv(patty_kiss_tnc *tnc, void *buf, size_t len);

ssize_t patty_kiss_tnc_send(patty_kiss_tnc *tnc,
                            const void *buf,
                            size_t len);

#endif /* _PATTY_KISS_H */
