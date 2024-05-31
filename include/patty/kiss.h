#ifndef _PATTY_KISS_H
#define _PATTY_KISS_H

#include <sys/types.h>

#define PATTY_KISS_FEND  0xc0
#define PATTY_KISS_FESC  0xdb
#define PATTY_KISS_TFEND 0xdc
#define PATTY_KISS_TFESC 0xdd

#define PATTY_KISS_COMMAND(cmd) \
    ((cmd & 0x0f))

#define PATTY_KISS_COMMAND_PORT(cmd) \
    ((cmd & 0xf0) >> 4)

enum patty_kiss_command {
    PATTY_KISS_DATA        = 0x00,
    PATTY_KISS_TXDELAY     = 0x01,
    PATTY_KISS_PERSISTENCE = 0x02,
    PATTY_KISS_SLOT_TIME   = 0x03,
    PATTY_KISS_TX_TAIL     = 0x04,
    PATTY_KISS_FULL_DUPLEX = 0x05,
    PATTY_KISS_HW_SET      = 0x06,
    PATTY_KISS_RETURN      = 0xff
};

ssize_t patty_kiss_frame_send(int fd,
                              const void *buf,
                              size_t len,
                              int port);

#endif /* _PATTY_KISS_H */
