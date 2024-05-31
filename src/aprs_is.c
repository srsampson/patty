#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <patty/ax25.h>
#include <patty/ax25/aprs_is.h>

enum state {
    APRS_IS_HEADER,
    APRS_IS_COMMENT,
    APRS_IS_BODY,
    APRS_IS_COMPLETE
};

struct _patty_ax25_aprs_is {
    patty_ax25_if_stats stats;
    patty_ax25_aprs_is_info info;

    int fd;

    char rx_buf[PATTY_AX25_APRS_IS_PACKET_MAX],
         tx_buf[PATTY_AX25_APRS_IS_FRAME_MAX],
         call[PATTY_AX25_ADDRSTRLEN+1],
         body[PATTY_AX25_APRS_IS_PAYLOAD_MAX];

    patty_ax25_frame frame;

    size_t rx_bufsz,
           tx_bufsz,
           bodysz;

    enum state state;

    size_t offset_i,
           offset_call,
           offset_body;

    ssize_t readlen,
            encoded;
};

static ssize_t aprs_is_vprintf(patty_ax25_aprs_is *aprs,
                               const char *fmt,
                               va_list args) {
    int len;

    if ((len = vsnprintf(aprs->tx_buf,
                         PATTY_AX25_APRS_IS_PACKET_MAX,
                         fmt,
                         args)) < 0) {
        goto error_vsnprintf;
    }

    return write(aprs->fd, aprs->tx_buf, (size_t)len);

error_vsnprintf:
    return -1;
}

static ssize_t aprs_is_printf(patty_ax25_aprs_is *aprs, const char *fmt, ...) {
    ssize_t len;
    va_list args;

    va_start(args, fmt);

    if ((len = aprs_is_vprintf(aprs, fmt, args)) < 0) {
        goto error_aprs_is_vprintf;
    }

    va_end(args);

    return len;

error_aprs_is_vprintf:
    return -1;
}

static int aprs_is_connect(patty_ax25_aprs_is *aprs,
                           patty_ax25_aprs_is_info *info) {
    struct addrinfo *ai0,
                    *ai;

    if (getaddrinfo(info->host, info->port, NULL, &ai0) < 0) {
        goto error_getaddrinfo;
    }

    for (ai=ai0; ai; ai=ai->ai_next) {
        if (ai->ai_socktype != SOCK_STREAM) {
            continue;
        }

        if ((aprs->fd = socket(ai->ai_family,
                               ai->ai_socktype,
                               ai->ai_protocol)) < 0) {
            continue;
        }

        if (connect(aprs->fd, ai->ai_addr, ai->ai_addrlen) < 0) {
            close(aprs->fd);

            continue;
        }

        freeaddrinfo(ai0);

        if (aprs_is_printf(aprs,
                           "user %s pass %s vers %s %s filter %s\r\n",
                           info->user,
                           info->pass,
                           info->appname,
                           info->version,
                           info->filter) < 0) {
            goto error_aprs_is_printf;
        }

        return 0;
    }

error_aprs_is_printf:
    close(aprs->fd);

error_getaddrinfo:
    return -1;
}

patty_ax25_aprs_is *patty_ax25_aprs_is_new(patty_ax25_aprs_is_info *info) {
    patty_ax25_aprs_is *aprs;

    if ((aprs = malloc(sizeof(*aprs))) == NULL) {
        goto error_malloc_aprs;
    }

    memset(aprs, '\0', sizeof(*aprs));

    aprs->rx_bufsz = PATTY_AX25_APRS_IS_PACKET_MAX;
    aprs->tx_bufsz = PATTY_AX25_APRS_IS_FRAME_MAX;
    aprs->bodysz   = PATTY_AX25_APRS_IS_PAYLOAD_MAX;
    aprs->state    = APRS_IS_HEADER;

    if (aprs_is_connect(aprs, info) < 0) {
        goto error_connect;
    }

    memcpy(&aprs->info, info, sizeof(aprs->info));

    return aprs;

error_connect:
    close(aprs->fd);

    free(aprs);

error_malloc_aprs:
    return NULL;
}

void patty_ax25_aprs_is_destroy(patty_ax25_aprs_is *aprs) {
    close(aprs->fd);

    free(aprs);
}

patty_ax25_if_stats *patty_ax25_aprs_is_stats(patty_ax25_aprs_is *aprs) {
    return &aprs->stats;
}

int patty_ax25_aprs_is_fd(patty_ax25_aprs_is *aprs) {
    return aprs->fd;
}

int patty_ax25_aprs_is_ready(patty_ax25_aprs_is *aprs, fd_set *fds) {
    return FD_ISSET(aprs->fd, fds);
}

int patty_ax25_aprs_is_reset(patty_ax25_aprs_is *aprs) {
    int attempt;

    close(aprs->fd);

    for (attempt=0; attempt<PATTY_AX25_APRS_IS_ATTEMPTS_MAX; attempt++) {
        if (aprs_is_connect(aprs, &aprs->info) == 0) {
            return aprs->fd;
        }
    }

    return -1;
}

ssize_t patty_ax25_aprs_is_fill(patty_ax25_aprs_is *aprs) {
    if ((aprs->readlen = read(aprs->fd, aprs->rx_buf, aprs->rx_bufsz)) < 0) {
        goto error_read;
    }

    aprs->offset_i = 0;

    return aprs->readlen;

error_read:
    return -1;
}

ssize_t patty_ax25_aprs_is_drain(patty_ax25_aprs_is *aprs,
                                 void *buf,
                                 size_t len) {
    ssize_t offset_start = aprs->offset_i;

    while (aprs->offset_i < aprs->readlen) {
        char c = aprs->rx_buf[aprs->offset_i++];

        switch (aprs->state) {
            case APRS_IS_HEADER: {
                patty_ax25_addr *addr = NULL;

                if (c == '#') {
                    aprs->state = APRS_IS_COMMENT;
                } else if (c == '>') {
                    addr = &aprs->frame.src;
                } else if (c == ',' || c == ':') {
                    if (aprs->frame.dest.callsign[0]) {
                        if (aprs->frame.hops == PATTY_AX25_MAX_HOPS) {
                            goto drop;
                        }

                        addr = &aprs->frame.repeaters[aprs->frame.hops++];
                    } else {
                        addr = &aprs->frame.dest;
                    }

                    if (c == ':') {
                        aprs->state = APRS_IS_BODY;
                    }
                } else if (PATTY_AX25_ADDR_CHAR_VALID(c)) {
                    if (aprs->offset_call == PATTY_AX25_ADDRSTRLEN) {
                        goto drop;
                    }

                    aprs->call[aprs->offset_call++] = c;
                } else {
                    goto drop;
                }

                if (addr) {
                    aprs->call[aprs->offset_call] = '\0';

                    if (patty_ax25_pton(aprs->call, addr) < 0) {
                        goto drop;
                    }

                    aprs->offset_call = 0;
                }

                break;
            }

            case APRS_IS_COMMENT:
                if (c == '\r') {
                    break;
                } else if (c == '\n') {
                    aprs->state = APRS_IS_HEADER;

                    goto done;
                }

                break;

            case APRS_IS_BODY:
                if (c == '\r') {
                    break;
                } else if (c == '\n') {
                    aprs->state = APRS_IS_COMPLETE;

                    aprs->frame.control = PATTY_AX25_FRAME_UI;
                    aprs->frame.cr      = PATTY_AX25_FRAME_COMMAND;
                    aprs->frame.proto   = PATTY_AX25_PROTO_NONE;
                    aprs->frame.info    = aprs->body;
                    aprs->frame.infolen = aprs->offset_body;

                    if ((aprs->encoded = patty_ax25_frame_encode(&aprs->frame,
                                                                 buf,
                                                                 len)) < 0) {
                        goto error;
                    }

                    goto done;
                } else {
                    if (aprs->offset_body == aprs->bodysz) {
                        goto drop;
                    }

                    aprs->body[aprs->offset_body++] = c;
                }

                break;

            case APRS_IS_COMPLETE:
                goto done;
        }
    }

done:
    return aprs->offset_i - offset_start;

drop:
    aprs->stats.dropped++;

    (void)patty_ax25_aprs_is_flush(aprs);

    return aprs->offset_i - offset_start;

error:
    return -1;
}

int patty_ax25_aprs_is_pending(patty_ax25_aprs_is *aprs) {
    return aprs->state == APRS_IS_COMPLETE? 1: 0;
}

ssize_t patty_ax25_aprs_is_flush(patty_ax25_aprs_is *aprs) {
    ssize_t ret = aprs->encoded;

    aprs->state       = APRS_IS_HEADER;
    aprs->offset_i    = aprs->readlen;
    aprs->offset_call = 0;
    aprs->offset_body = 0;
    aprs->encoded     = 0;

    memset(&aprs->frame, '\0', sizeof(aprs->frame));

    return ret;
}

ssize_t patty_ax25_aprs_is_send(patty_ax25_aprs_is *aprs,
                                const void *buf,
                                size_t len) {
    patty_ax25_frame frame;

    size_t i = 0,
           o = 0,
           hop;

    ssize_t decoded;

    int formatted,
        attempt;

    char call[PATTY_AX25_ADDRSTRLEN+1];

    if ((decoded = patty_ax25_frame_decode_address(&frame, buf, len)) < 0) {
        goto error;
    } else {
        i += decoded;
    }

    if ((decoded = patty_ax25_frame_decode_control(&frame,
                                                   PATTY_AX25_FRAME_NORMAL,
                                                   buf,
                                                   i,
                                                   len)) < 0) {
        goto error;
    } else {
        i += decoded;
    }

    if (!PATTY_AX25_FRAME_CONTROL_UI(frame.control)) {
        return 0;
    }

    if (patty_ax25_ntop(&frame.src, call, sizeof(call)) < 0) {
        goto error;
    }

    if ((formatted = snprintf(aprs->tx_buf,
                              aprs->tx_bufsz,
                              "%s>",
                              call)) < 0) {
        goto error;
    } else {
        o += formatted;
    }

    if (patty_ax25_ntop(&frame.dest, call, sizeof(call)) < 0) {
        goto error;
    }

    if ((formatted = snprintf(aprs->tx_buf + o,
                              aprs->tx_bufsz - o,
                              "%s",
                              call)) < 0) {
        goto error;
    } else {
        o += formatted;
    }

    for (hop=0; hop<frame.hops; hop++) {
        if (patty_ax25_ntop(&frame.repeaters[hop],
                            call,
                            sizeof(call)) < 0) {
            goto error;
        }

        if ((formatted = snprintf(aprs->tx_buf + o,
                                  aprs->tx_bufsz - o,
                                  ",%s",
                                  call)) < 0) {
            goto error;
        } else {
            o += formatted;
        }
    }

    if (aprs->tx_bufsz < o + frame.infolen + 3) {
        errno = EOVERFLOW;

        goto error;
    }

    aprs->tx_buf[o++] = ':';

    memcpy(aprs->tx_buf + o, frame.info, frame.infolen);

    o += frame.infolen;

    aprs->tx_buf[o++] = '\r';
    aprs->tx_buf[o++] = '\n';

    attempt = 0;

    while (write(aprs->fd, aprs->tx_buf, o) < 0) {
        if (++attempt == PATTY_AX25_APRS_IS_ATTEMPTS_MAX) {
            goto error;
        }

        if (errno == EIO) {
            (void)aprs_is_connect(aprs, &aprs->info);
        } else {
            goto error;
        }
    }

    return i;

error:
    return -1;
}

patty_ax25_if_driver *patty_ax25_aprs_is_driver() {
    static patty_ax25_if_driver driver = {
        .create  = (patty_ax25_if_driver_create *)patty_ax25_aprs_is_new,
        .destroy = (patty_ax25_if_driver_destroy *)patty_ax25_aprs_is_destroy,
        .stats   = (patty_ax25_if_driver_stats *)patty_ax25_aprs_is_stats,
        .fd      = (patty_ax25_if_driver_fd *)patty_ax25_aprs_is_fd,
        .ready   = (patty_ax25_if_driver_ready *)patty_ax25_aprs_is_ready,
        .reset   = (patty_ax25_if_driver_reset *)patty_ax25_aprs_is_reset,
        .fill    = (patty_ax25_if_driver_fill *)patty_ax25_aprs_is_fill,
        .drain   = (patty_ax25_if_driver_drain *)patty_ax25_aprs_is_drain,
        .pending = (patty_ax25_if_driver_pending *)patty_ax25_aprs_is_pending,
        .flush   = (patty_ax25_if_driver_flush *)patty_ax25_aprs_is_flush,
        .send    = (patty_ax25_if_driver_send *)patty_ax25_aprs_is_send
    };

    return &driver;
};
