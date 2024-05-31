#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include <patty/ax25.h>
#include <patty/kiss.h>
#include <patty/kiss/tnc.h>
#include <patty/util.h>

#include "config.h"

enum tnc_opts {
    TNC_NONE             = 0,
    TNC_CLOSE_ON_DESTROY = 1 << 0
};

enum state {
    KISS_NONE,
    KISS_FRAME_COMMAND,
    KISS_FRAME_BODY,
    KISS_FRAME_ESCAPE
};

struct _patty_kiss_tnc {
    patty_ax25_if_stats stats;

    struct termios attrs,
                   attrs_old;

    int fd,
        opts;

    void *buf;

    enum state state;
    enum patty_kiss_command command;
    int port;

    size_t bufsz,
           readlen,
           offset_i,
           offset_o;
};

static int init_sock(patty_kiss_tnc *tnc, patty_kiss_tnc_info *info) {
    struct sockaddr_un addr;

    if (strlen(info->device) > sizeof(addr.sun_path)) {
        errno = EOVERFLOW;

        goto error_overflow;
    }

    if ((tnc->fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
        goto error_socket;
    }

    memset(&addr, '\0', sizeof(addr));
    addr.sun_family = AF_UNIX;
    patty_strlcpy(addr.sun_path, info->device, sizeof(addr.sun_path));

    if (connect(tnc->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        goto error_connect;
    }

    return 0;

error_connect:
    (void)close(tnc->fd);

error_socket:
error_overflow:
    return -1;
}

static int init_termios(patty_kiss_tnc *tnc, patty_kiss_tnc_info *info) {
    if (tcgetattr(tnc->fd, &tnc->attrs) < 0) {
        goto error_tcgetattr;
    }

    memcpy(&tnc->attrs_old, &tnc->attrs, sizeof(tnc->attrs_old));

    cfmakeraw(&tnc->attrs);

    if (info->flags & PATTY_KISS_TNC_BAUD) {
        cfsetspeed(&tnc->attrs, info->baud);
    }

    if (info->flags & PATTY_KISS_TNC_FLOW) {
        switch (info->flow) {
            case PATTY_KISS_TNC_FLOW_NONE:
                break;

            case PATTY_KISS_TNC_FLOW_CRTSCTS:
                tnc->attrs.c_cflag |= CRTSCTS;

                break;

            case PATTY_KISS_TNC_FLOW_XONXOFF:
                tnc->attrs.c_iflag |= IXON | IXOFF;

                break;
        }
    }

    if (tcflush(tnc->fd, TCIOFLUSH) < 0) {
        goto error_tcflush;
    }

    if (tcsetattr(tnc->fd, TCSANOW, &tnc->attrs) < 0) {
        goto error_tcsetattr;
    }

    return 0;

error_tcsetattr:
error_tcflush:
error_tcgetattr:
    return -1;
}

static int init_device(patty_kiss_tnc *tnc, patty_kiss_tnc_info *info) {
    struct stat st;

    if (strcmp(info->device, "/dev/ptmx") == 0) {
        int ptysub;

        if (openpty(&tnc->fd, &ptysub, NULL, NULL, NULL) < 0) {
            goto error;
        }
    } else if (stat(info->device, &st) < 0) {
        goto error;
    } else if ((st.st_mode & S_IFMT) == S_IFSOCK) {
        if (init_sock(tnc, info) < 0) {
            goto error;
        }
    } else {
        if ((tnc->fd = open(info->device, O_RDWR | O_NOCTTY)) < 0) {
            goto error;
        }
    }

    tnc->opts |= TNC_CLOSE_ON_DESTROY;

    return 0;

error:
    return -1;
}

patty_kiss_tnc *patty_kiss_tnc_new(patty_kiss_tnc_info *info) {
    patty_kiss_tnc *tnc;

    if ((tnc = malloc(sizeof(*tnc))) == NULL) {
        goto error_malloc_tnc;
    }

    if ((tnc->buf = malloc(PATTY_KISS_TNC_BUFSZ)) == NULL) {
        goto error_malloc_buf;
    }

    if (info->flags & PATTY_KISS_TNC_DEVICE) {
        if (init_device(tnc, info) < 0) {
            goto error_init_device;
        }
    } else if (info->flags & PATTY_KISS_TNC_FD) {
        tnc->fd = info->fd;
    } else {
        errno = EINVAL;

        goto error_invalid;
    }

    if (isatty(tnc->fd)) {
        if (init_termios(tnc, info) < 0) {
            goto error_init_termios;
        }
    }

    memset(&tnc->stats, '\0', sizeof(tnc->stats));

    tnc->opts     = TNC_NONE;
    tnc->state    = KISS_NONE;
    tnc->command  = PATTY_KISS_RETURN;
    tnc->port     = 0;
    tnc->bufsz    = PATTY_KISS_TNC_BUFSZ;
    tnc->offset_i = 0;
    tnc->offset_o = 0;
    tnc->readlen  = 0;

    return tnc;

error_init_termios:
    if (info->flags & PATTY_KISS_TNC_DEVICE) {
        (void)close(tnc->fd);
    }

error_init_device:
error_invalid:
    free(tnc->buf);

error_malloc_buf:
    free(tnc);

error_malloc_tnc:
    return NULL;
}

void patty_kiss_tnc_destroy(patty_kiss_tnc *tnc) {
    if (isatty(tnc->fd) && ptsname(tnc->fd) == NULL) {
        (void)tcsetattr(tnc->fd, TCSANOW, &tnc->attrs_old);
    }

    if (tnc->opts & TNC_CLOSE_ON_DESTROY) {
        (void)close(tnc->fd);
    }

    free(tnc->buf);
    free(tnc);
}

patty_ax25_if_stats *patty_kiss_tnc_stats(patty_kiss_tnc *tnc) {
    return &tnc->stats;
}

int patty_kiss_tnc_fd(patty_kiss_tnc *tnc) {
    return tnc->fd;
}

int patty_kiss_tnc_ready(patty_kiss_tnc *tnc, fd_set *fds) {
    return FD_ISSET(tnc->fd, fds);
}

int patty_kiss_tnc_reset(patty_kiss_tnc *tnc) {
    errno = ENOSYS;

    return -1;
}

static void tnc_drop(patty_kiss_tnc *tnc) {
    tnc->state    = KISS_NONE;
    tnc->command  = PATTY_KISS_RETURN;
    tnc->port     = 0;
    tnc->offset_i = 0;
    tnc->offset_o = 0;
    tnc->readlen  = 0;

    tnc->stats.dropped++;
}

ssize_t patty_kiss_tnc_fill(patty_kiss_tnc *tnc) {
    if ((tnc->readlen = read(tnc->fd, tnc->buf, tnc->bufsz)) < 0) {
        goto error_read;
    }

    tnc->offset_i = 0;

    return tnc->readlen;

error_read:
    return -1;
}

ssize_t patty_kiss_tnc_drain(patty_kiss_tnc *tnc, void *buf, size_t len) {
    size_t offset_start = tnc->offset_i;

    while (tnc->offset_i < tnc->readlen) {
        uint8_t c;

        if (tnc->offset_o == len) {
            tnc_drop(tnc);
        }

        c = ((uint8_t *)tnc->buf)[tnc->offset_i++];

        switch (tnc->state) {
            case KISS_NONE:
                if (c == PATTY_KISS_FEND) {
                    tnc->state = KISS_FRAME_COMMAND;
                }

                break;

            case KISS_FRAME_COMMAND: {
                uint8_t command = PATTY_KISS_COMMAND(c),
                        port    = PATTY_KISS_COMMAND_PORT(c);

                if (command == PATTY_KISS_FEND) {
                    break;
                }

                switch (command) {
                    case PATTY_KISS_DATA:
                    case PATTY_KISS_TXDELAY:
                    case PATTY_KISS_PERSISTENCE:
                    case PATTY_KISS_SLOT_TIME:
                    case PATTY_KISS_TX_TAIL:
                    case PATTY_KISS_FULL_DUPLEX:
                    case PATTY_KISS_HW_SET:
                    case PATTY_KISS_RETURN:
                        break;

                    default:
                        errno = EIO;

                        goto error_io;
                }

                tnc->state   = KISS_FRAME_BODY;
                tnc->command = command;
                tnc->port    = port;

                break;
            }

            case KISS_FRAME_BODY:
                if (c == PATTY_KISS_FESC) {
                    tnc->state = KISS_FRAME_ESCAPE;
                } else if (c == PATTY_KISS_FEND) {
                    tnc->state = KISS_FRAME_COMMAND;

                    goto done;
                } else {
                    switch (tnc->command) {
                        case PATTY_KISS_DATA:
                            ((uint8_t *)buf)[tnc->offset_o++] = c;

                        default:
                            break;
                    }
                }

                break;

            case KISS_FRAME_ESCAPE:
                if (c == PATTY_KISS_TFEND) {
                    ((uint8_t *)buf)[tnc->offset_o++] = PATTY_KISS_FEND;
                } else if (c == PATTY_KISS_TFESC) {
                    ((uint8_t *)buf)[tnc->offset_o++] = PATTY_KISS_FESC;
                } else {
                    errno = EIO;

                    goto error_io;
                }

                tnc->state = KISS_FRAME_BODY;
        }
    }

done:
    return tnc->offset_i - offset_start;

error_io:
    return -1;
}

int patty_kiss_tnc_pending(patty_kiss_tnc *tnc) {
    return tnc->state   == KISS_FRAME_COMMAND
        && tnc->command == PATTY_KISS_DATA
        && tnc->port    == 0
        && tnc->offset_o > 0? 1: 0;
}

ssize_t patty_kiss_tnc_flush(patty_kiss_tnc *tnc) {
    ssize_t ret = tnc->offset_o;

    tnc->state    = KISS_NONE;
    tnc->command  = PATTY_KISS_RETURN;
    tnc->offset_o = 0;

    return ret;
}

ssize_t patty_kiss_tnc_recv(patty_kiss_tnc *tnc, void *buf, size_t len) {
    while (!patty_kiss_tnc_pending(tnc)) {
        ssize_t drained;

        if ((drained = patty_kiss_tnc_drain(tnc, buf, len)) < 0) {
            goto error_drain;
        } else if (drained == 0) {
            ssize_t filled;

            if ((filled = patty_kiss_tnc_fill(tnc)) < 0) {
                goto error_fill;
            } else if (filled == 0) {
                return 0;
            }
        }
    }

    return patty_kiss_tnc_flush(tnc);

error_drain:
error_fill:
    return -1;
}

ssize_t patty_kiss_tnc_send(patty_kiss_tnc *tnc,
                            const void *buf,
                            size_t len) {
    return patty_kiss_frame_send(tnc->fd, buf, len, PATTY_KISS_TNC_PORT);
}

patty_ax25_if_driver *patty_kiss_tnc_driver() {
    static patty_ax25_if_driver driver = {
        .create  = (patty_ax25_if_driver_create *)patty_kiss_tnc_new,
        .destroy = (patty_ax25_if_driver_destroy *)patty_kiss_tnc_destroy,
        .stats   = (patty_ax25_if_driver_stats *)patty_kiss_tnc_stats,
        .fd      = (patty_ax25_if_driver_fd *)patty_kiss_tnc_fd,
        .ready   = (patty_ax25_if_driver_ready *)patty_kiss_tnc_ready,
        .reset   = (patty_ax25_if_driver_reset *)patty_kiss_tnc_reset,
        .fill    = (patty_ax25_if_driver_fill *)patty_kiss_tnc_fill,
        .drain   = (patty_ax25_if_driver_drain *)patty_kiss_tnc_drain,
        .pending = (patty_ax25_if_driver_pending *)patty_kiss_tnc_pending,
        .flush   = (patty_ax25_if_driver_flush *)patty_kiss_tnc_flush,
        .send    = (patty_ax25_if_driver_send *)patty_kiss_tnc_send
    };

    return &driver;
}
