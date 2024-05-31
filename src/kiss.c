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

#include <patty/kiss.h>

#include "config.h"

static inline ssize_t write_byte(int fd, uint8_t c) {
    return write(fd, &c, sizeof(c));
}

static inline ssize_t write_start(int fd,
                                  enum patty_kiss_command command,
                                  int port) {
    uint8_t start[2] = {
        PATTY_KISS_FEND,
        ((port & 0x0f) << 4) | (command & 0x0f)
    };

    return write(fd, start, sizeof(start));
}

static uint8_t escape_fend[2] = { PATTY_KISS_FESC, PATTY_KISS_TFEND };
static uint8_t escape_fesc[2] = { PATTY_KISS_FESC, PATTY_KISS_TFESC };

ssize_t patty_kiss_frame_send(int fd,
                             const void *buf,
                             size_t len,
                             int port) {
    size_t i, start = 0, end = 0;

    if (write_start(fd, PATTY_KISS_DATA, port) < 0) {
        goto error_io;
    }

    for (i=0; i<len; i++) {
        uint8_t c = ((uint8_t *)buf)[i];
        uint8_t *escape = NULL;

        switch (c) {
            case PATTY_KISS_FEND:
                escape = escape_fend;
                break;

            case PATTY_KISS_FESC:
                escape = escape_fesc;
                break;

            default:
                end = i + 1;

                break;
        }

        if (escape) {
            if (write(fd, ((uint8_t *)buf) + start, end - start) < 0) {
                goto error_io;
            }

            if (write(fd, escape, 2) < 0) {
                goto error_io;
            }

            escape = NULL;
            start  = i + 1;
            end    = start;
        }
    }

    if (end - start) {
        if (write(fd, ((uint8_t *)buf) + start, end - start) < 0) {
            goto error_io;
        }
    }

    if (write_byte(fd, PATTY_KISS_FEND) < 0) {
        goto error_io;
    }

    return len;

error_io:
    return -1;
}
