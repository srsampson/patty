#ifndef _PATTY_AX25_FRAME_H
#define _PATTY_AX25_FRAME_H

#include <stdint.h>
#include <sys/types.h>

enum patty_ax25_frame_format {
    PATTY_AX25_FRAME_NORMAL,
    PATTY_AX25_FRAME_EXTENDED
};

enum patty_ax25_frame_cr {
    PATTY_AX25_FRAME_OLD,
    PATTY_AX25_FRAME_COMMAND,
    PATTY_AX25_FRAME_RESPONSE
};

enum patty_ax25_frame_flag {
    PATTY_AX25_FRAME_POLL  = 1,
    PATTY_AX25_FRAME_FINAL = 1
};

#define PATTY_AX25_FRAME_CONTROL_I(c) \
    ((c & 0x1) == 0x00)

#define PATTY_AX25_FRAME_CONTROL_S(c) \
    ((c & 0x03) == 0x01)

#define PATTY_AX25_FRAME_CONTROL_U(c) \
    ((c & 0x03) == 0x03)

#define PATTY_AX25_FRAME_CONTROL_UI(c) \
    ((c & 0xef) == 0x03)

#define PATTY_AX25_FRAME_CONTROL_FLAG(c) \
    ((c & 0x01) >> 4)

#define PATTY_AX25_FRAME_OVERHEAD 73

#define PATTY_AX25_FRAME_SIZE(format, hops, c, infolen) \
    (((2 + hops) * sizeof(patty_ax25_addr)) \
    + (format == PATTY_AX25_FRAME_EXTENDED? 2: 1) \
    + (PATTY_AX25_FRAME_CONTROL_I(c)? 1 + infolen: 0))

#define PATTY_AX25_FRAME_S_MASK 0x0f
#define PATTY_AX25_FRAME_U_MASK 0xef

enum patty_ax25_frame_type {
    PATTY_AX25_FRAME_I       = 0x00,
    PATTY_AX25_FRAME_RR      = 0x01,
    PATTY_AX25_FRAME_RNR     = 0x05,
    PATTY_AX25_FRAME_REJ     = 0x09,
    PATTY_AX25_FRAME_SREJ    = 0x0d,
    PATTY_AX25_FRAME_SABM    = 0x2f,
    PATTY_AX25_FRAME_SABME   = 0x6f,
    PATTY_AX25_FRAME_DISC    = 0x43,
    PATTY_AX25_FRAME_DM      = 0x0f,
    PATTY_AX25_FRAME_UA      = 0x63,
    PATTY_AX25_FRAME_FRMR    = 0x87,
    PATTY_AX25_FRAME_UI      = 0x03,
    PATTY_AX25_FRAME_XID     = 0xaf,
    PATTY_AX25_FRAME_TEST    = 0xe3
};

typedef struct _patty_ax25_frame {
    patty_ax25_addr dest,
                    src,
                    repeaters[PATTY_AX25_MAX_HOPS];

    unsigned int hops;

    uint16_t control;
    uint8_t nr, ns, pf;
    enum patty_ax25_frame_cr cr;
    enum patty_ax25_frame_type type;
    enum patty_ax25_frame_format format;
    enum patty_ax25_version version;

    uint8_t proto;
    void *info;
    size_t infolen;
} patty_ax25_frame;

enum patty_ax25_frame_xid_group_format {
    PATTY_AX25_FRAME_XID_GROUP_ISO8885 = 0x82
};

enum patty_ax25_frame_xid_group_type {
    PATTY_AX25_FRAME_XID_GROUP_PARAMS = 0x80
};

#pragma pack(push)
#pragma pack(1)

typedef struct _patty_ax25_frame_xid_group {
    uint8_t format,
            type;

    uint16_t len;
} patty_ax25_frame_xid_group;

typedef struct _patty_ax25_frame_xid_param {
    uint8_t id, len;
} patty_ax25_frame_xid_param;

#pragma pack(pop)

ssize_t patty_ax25_frame_decode_address(patty_ax25_frame *frame,
                                        const void *buf,
                                        size_t len);

ssize_t patty_ax25_frame_decode_control(patty_ax25_frame *frame,
                                        enum patty_ax25_frame_format format,
                                        const void *buf,
                                        size_t offset,
                                        size_t len);

ssize_t patty_ax25_frame_decode_xid(patty_ax25_params *params,
                                    const void *data,
                                    size_t offset,
                                    size_t len);

ssize_t patty_ax25_frame_encode(patty_ax25_frame *frame,
                                void *buf,
                                size_t len);

ssize_t patty_ax25_frame_encode_xid(patty_ax25_params *params,
                                    void *buf,
                                    size_t len);

ssize_t patty_ax25_frame_encode_reply_to(patty_ax25_frame *frame,
                                         patty_ax25_frame *reply,
                                         void *buf,
                                         size_t len);

enum patty_ax25_version patty_ax25_frame_version(patty_ax25_frame *frame);

enum patty_ax25_frame_type patty_ax25_frame_type(patty_ax25_frame *frame);

enum patty_ax25_frame_cr patty_ax25_frame_cr(patty_ax25_frame *frame);

ssize_t patty_ax25_frame_info(patty_ax25_frame *frame,
                              void **info);

#endif /* _PATTY_AX25_FRAME_H */
