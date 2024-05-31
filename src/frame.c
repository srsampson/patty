#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include <patty/ax25.h>
#include "config.h"

static ssize_t decode_station(patty_ax25_addr *addr,
                              const void *data,
                              size_t offset,
                              size_t len) {
    int i, space = 0;

    if (len < offset + sizeof(patty_ax25_addr)) {
        errno = EIO;

        goto error;
    }

    for (i=0; i<PATTY_AX25_CALLSTRLEN; i++) {
        uint8_t b = ((uint8_t *)data + offset)[i];
        uint8_t c = b >> 1;

        if (!PATTY_AX25_ADDR_CHAR_VALID(c) || PATTY_AX25_ADDR_OCTET_LAST(b)) {
            errno = EIO;

            goto error;
        }

        if (c == ' ' && !space) {
            space = 1;
        } else if (c != ' ' && space) {
            errno = EIO;

            goto error;
        }
    }

    memcpy(addr, ((uint8_t *)data) + offset, sizeof(*addr));

    return sizeof(patty_ax25_addr);

error:
    return -1;
}

static ssize_t decode_hops(patty_ax25_frame *frame,
                           const void *data,
                           size_t offset,
                           size_t len) {
    ssize_t start = offset;

    patty_ax25_addr *addr = NULL;

    int i;

    /*
     * Try to count the AX.25-specified maximum number of hops in the current
     * frame.
     */
    for (i=0; i<PATTY_AX25_MAX_HOPS; i++) {
        ssize_t decoded;

        addr = (patty_ax25_addr *)((uint8_t *)data + offset);

        if ((decoded = decode_station(&frame->repeaters[i], data, offset, len)) < 0) {
            goto error;
        } else {
            offset += decoded;
        }

        frame->hops++;

        if (PATTY_AX25_ADDR_OCTET_LAST(addr->ssid)) {
            break;
        }
    }

    /*
     * If the last hop does not have the address extension bit set, then
     * that's a big problem.
     */
    if (addr && !PATTY_AX25_ADDR_OCTET_LAST(addr->ssid)) {
        errno = EIO;

        goto error;
    }

    return offset - start;

error:
    return -1;
}

ssize_t patty_ax25_frame_decode_address(patty_ax25_frame *frame,
                                        const void *buf,
                                        size_t len) {
     size_t offset = 0;
    ssize_t decoded;

    if ((decoded = decode_station(&frame->dest, buf, offset, len)) < 0) {
        goto error;
    } else {
        offset += decoded;
    }

    if ((decoded = decode_station(&frame->src, buf, offset, len)) < 0) {
        goto error;
    } else {
        offset += decoded;
    }

    if (PATTY_AX25_ADDR_SSID_C(frame->dest.ssid) != PATTY_AX25_ADDR_SSID_C(frame->src.ssid)) {
        frame->cr = PATTY_AX25_ADDR_SSID_C(frame->dest.ssid)?
                    PATTY_AX25_FRAME_COMMAND:
                    PATTY_AX25_FRAME_RESPONSE;

        frame->version = PATTY_AX25_2_0;

    } else {
        frame->cr      = PATTY_AX25_FRAME_OLD;
        frame->version = PATTY_AX25_OLD;
    }

    /*
     * If the source address is not the final address in the frame, begin
     * decoding repeater addresses.
     */
    frame->hops = 0;

    if (!PATTY_AX25_ADDR_OCTET_LAST(frame->src.ssid)) {
        if ((decoded = decode_hops(frame, buf, offset, len)) < 0) {
            goto error;
        } else {
            offset += decoded;
        }
    }

    return offset;

error:
    return -1;
}

static inline uint8_t decode_nr(uint16_t control,
                                enum patty_ax25_frame_format format) {
    switch (format) {
        case PATTY_AX25_FRAME_NORMAL:   return (control & 0x00e0) >> 5;
        case PATTY_AX25_FRAME_EXTENDED: return (control & 0x7e00) >> 9;
    }

    return 0;
}

static inline uint8_t decode_ns(uint16_t control,
                                enum patty_ax25_frame_format format) {
    switch (format) {
        case PATTY_AX25_FRAME_NORMAL:   return (control & 0x000e) >> 1;
        case PATTY_AX25_FRAME_EXTENDED: return (control & 0x007e) >> 1;
    }

    return 0;
}

static inline uint8_t decode_pf(uint16_t control,
                                enum patty_ax25_frame_format format) {
    switch (format) {
        case PATTY_AX25_FRAME_NORMAL:   return (control & 0x0010) >> 4;
        case PATTY_AX25_FRAME_EXTENDED: return (control & 0x0100) >> 8;
    }

    return 0;
}

ssize_t patty_ax25_frame_decode_control(patty_ax25_frame *frame,
                                        enum patty_ax25_frame_format format,
                                        const void *data,
                                        size_t offset,
                                        size_t len) {
    const uint8_t *buf = data;
    size_t start = offset;

    frame->control = 0;
    frame->nr      = 0;
    frame->ns      = 0;
    frame->pf      = 0;
    frame->type    = 0xff;
    frame->proto   = PATTY_AX25_PROTO_UNKNOWN;
    frame->info    = NULL;
    frame->infolen = 0;

    switch (format) {
        case PATTY_AX25_FRAME_NORMAL:
            frame->control = (uint16_t)(buf[offset++]);

            break;

        case PATTY_AX25_FRAME_EXTENDED:
            frame->control = (uint16_t)(buf[offset++]);

            if (!PATTY_AX25_FRAME_CONTROL_U(frame->control)) {
                frame->control |= (uint16_t)(buf[offset++] << 8);
            }

            break;
    }

    frame->format = format;

    if (PATTY_AX25_FRAME_CONTROL_I(frame->control)) {
        frame->type = PATTY_AX25_FRAME_I;
        frame->nr   = decode_nr(frame->control, format);
        frame->ns   = decode_ns(frame->control, format);
        frame->pf   = decode_pf(frame->control, format);
    } else if (PATTY_AX25_FRAME_CONTROL_S(frame->control)) {
        uint16_t c = frame->control & PATTY_AX25_FRAME_S_MASK;

        switch (c) {
            case PATTY_AX25_FRAME_RR:
            case PATTY_AX25_FRAME_RNR:
            case PATTY_AX25_FRAME_REJ:
            case PATTY_AX25_FRAME_SREJ:
                frame->type = c;
                frame->nr   = decode_nr(frame->control, format);
                frame->pf   = decode_pf(frame->control, format);

            default:
                break;
        }
    } else if (PATTY_AX25_FRAME_CONTROL_U(frame->control)) {
        uint16_t c = frame->control & PATTY_AX25_FRAME_U_MASK;

        switch (c) {
            case PATTY_AX25_FRAME_SABME:
            case PATTY_AX25_FRAME_SABM:
            case PATTY_AX25_FRAME_DISC:
            case PATTY_AX25_FRAME_DM:
            case PATTY_AX25_FRAME_UA:
            case PATTY_AX25_FRAME_FRMR:
            case PATTY_AX25_FRAME_UI:
            case PATTY_AX25_FRAME_XID:
            case PATTY_AX25_FRAME_TEST:
                frame->type = c;
                frame->pf   = decode_pf(frame->control, PATTY_AX25_FRAME_NORMAL);

            default:
                break;
        }
    } else {
        errno = EIO;

        goto error;
    }

    switch (frame->type) {
        case PATTY_AX25_FRAME_I:
        case PATTY_AX25_FRAME_UI:
            frame->proto   = buf[offset++];

        case PATTY_AX25_FRAME_TEST:
            frame->info    = (uint8_t *)buf + offset;
            frame->infolen = len - offset;

            offset = len;

        default:
            break;
    }

    errno = 0;

    return offset - start;

error:
    return -1;
}

static void save_xid_param(patty_ax25_params *params,
                           enum patty_ax25_param_type type,
                           uint32_t value) {
    switch (type) {
        case PATTY_AX25_PARAM_CLASSES:
            params->classes = value; break;

        case PATTY_AX25_PARAM_HDLC:
            params->hdlc = value; break;

        case PATTY_AX25_PARAM_INFO_TX:
            params->info_tx = (size_t)value; break;

        case PATTY_AX25_PARAM_INFO_RX:
            params->info_rx = (size_t)value; break;

        case PATTY_AX25_PARAM_WINDOW_TX:
            params->window_tx = (size_t)value; break;

        case PATTY_AX25_PARAM_WINDOW_RX:
            params->window_rx = (size_t)value; break;

        case PATTY_AX25_PARAM_ACK:
            params->ack = (size_t)value; break;

        case PATTY_AX25_PARAM_RETRY:
            params->retry = (size_t)value; break;

        default:
            return;
    }

    params->flags |= (1 << type);
}

ssize_t decode_xid_group(patty_ax25_params *params,
                         const void *data,
                         size_t offset,
                         size_t len) {
    patty_ax25_frame_xid_group *group = (patty_ax25_frame_xid_group *)
        ((uint8_t *)data + offset);

    size_t grouplen,
           start = offset;

    if (len - offset < sizeof(*group)) {
        goto error;
    }

    grouplen = be16toh(group->len);

    offset += sizeof(*group);

    if (group->format != PATTY_AX25_FRAME_XID_GROUP_ISO8885
     || group->type   != PATTY_AX25_FRAME_XID_GROUP_PARAMS) {
        offset += grouplen;

        goto done;
    }

    memset(params, '\0', sizeof(*params));

    while (offset - start < sizeof(*group) + grouplen) {
        patty_ax25_frame_xid_param *param = (patty_ax25_frame_xid_param *)
            ((uint8_t *)data + offset);

        uint32_t value = 0;
        size_t i;

        for (i=0; i<param->len; i++) {
            value |= ((uint8_t *)(param + 1))[param->len-1-i] << (i << 3);
        }

        save_xid_param(params, param->id, value);

        offset += sizeof(*param) + param->len;
    }

    if (offset != start + sizeof(*group) + grouplen) {
        goto error;
    }

done:
    return offset - start;

error:
    errno = EIO;

    return -1;
}

ssize_t patty_ax25_frame_decode_xid(patty_ax25_params *params,
                                    const void *data,
                                    size_t offset,
                                    size_t len) {
    size_t start = offset;

    while (offset < len) {
        ssize_t decoded;

        if ((decoded = decode_xid_group(params, data, offset, len)) < 0) {
            goto error_decode_xid_group;
        } else {
            offset += decoded;
        }
    }

    if (offset != len) {
        goto error;
    }

    errno = 0;

    return offset - start;

error:
    errno = EIO;

error_decode_xid_group:
    return -1;
}

static ssize_t encode_address(patty_ax25_frame *frame,
                              void *dest,
                              int reply,
                              size_t len) {
    uint8_t *buf = (uint8_t *)dest;
    size_t offset = 0;

    uint8_t flags_remote = 0x00,
            flags_local  = 0x00;

    unsigned int i;

    if ((2 + frame->hops) * sizeof(patty_ax25_addr) > len) {
        errno = EOVERFLOW;

        goto error_toobig;
    }

    switch (frame->cr) {
        case PATTY_AX25_FRAME_COMMAND:  flags_remote = 0x80; break;
        case PATTY_AX25_FRAME_RESPONSE: flags_local  = 0x80; break;
        case PATTY_AX25_FRAME_OLD: break;
    }

    if (reply) {
        offset += patty_ax25_addr_copy(buf + offset, &frame->src,  flags_local);
        offset += patty_ax25_addr_copy(buf + offset, &frame->dest, flags_remote);

        for (i=0; i<frame->hops; i++) {
            int n = frame->hops - 1 - i;

            offset += patty_ax25_addr_copy(buf + offset, &frame->repeaters[n], 0);
        }
    } else {
        offset += patty_ax25_addr_copy(buf + offset, &frame->dest, flags_remote);
        offset += patty_ax25_addr_copy(buf + offset, &frame->src,  flags_local);

        for (i=0; i<frame->hops; i++) {
            offset += patty_ax25_addr_copy(buf + offset, &frame->repeaters[i], 0);
        }
    }

    ((uint8_t *)buf)[offset-1] |= 1;

    return offset;

error_toobig:
    return -1;
}

ssize_t patty_ax25_frame_encode(patty_ax25_frame *frame,
                                void *buf,
                                size_t len) {
    size_t offset = 0;
    ssize_t encoded;

    if ((encoded = encode_address(frame, buf, 0, len)) < 0) {
        goto error_encode_address;
    } else {
        offset += encoded;
    }

    switch (frame->format) {
        case PATTY_AX25_FRAME_NORMAL:
            ((uint8_t *)buf)[offset++] = frame->control;

            break;

        case PATTY_AX25_FRAME_EXTENDED:
            ((uint8_t *)buf)[offset++] = frame->control & 0x00ff;

            if (!PATTY_AX25_FRAME_CONTROL_U(frame->control)) {
                ((uint8_t *)buf)[offset++] = (frame->control & 0xff00) >> 8;
            }

            break;
    }

    if (frame->info && frame->infolen) {
        if (len < 1 + offset + frame->infolen) {
            errno = EOVERFLOW;

            goto error_toobig;
        }

        if (PATTY_AX25_FRAME_CONTROL_I(frame->control) || PATTY_AX25_FRAME_CONTROL_UI(frame->control)) {
            ((uint8_t *)buf)[offset++] = frame->proto;
        }

        memcpy((uint8_t *)buf + offset, frame->info, frame->infolen);

        offset += frame->infolen;
    }

    return offset;

error_toobig:
error_encode_address:
    return -1;
}

static ssize_t encode_xid_param(void *data,
                                size_t offset,
                                enum patty_ax25_param_type type,
                                size_t bytes,
                                uint32_t value,
                                size_t len) {
    uint8_t *buf = data;

    size_t start = offset,
           i;

    if (offset + 2 + bytes > len || bytes > 4) {
        goto error;
    }

    buf[offset++] = (uint8_t)type;
    buf[offset++] = (uint8_t)bytes;

    for (i=0; i<bytes; i++) {
        uint8_t shift = (bytes-1-i) << 3;

        buf[offset++] = (value & (0xff << shift)) >> shift;
    }

    return offset - start;

error:
    errno = EIO;

    return -1;
}

static inline size_t needbytes(uint32_t value) {
    if (!(value & 0xffffff00)) return 1;
    if (!(value & 0xffff0000)) return 2;
    if (!(value & 0xff000000)) return 3;

    return 4;
}

ssize_t patty_ax25_frame_encode_xid(patty_ax25_params *params,
                                    void *data,
                                    size_t len) {
    size_t offset = 0;
    ssize_t encoded;

    struct {
        uint8_t id;
        size_t bytes;
        uint32_t value;
    } values[] = {
        { PATTY_AX25_PARAM_CLASSES,   2, params->classes },
        { PATTY_AX25_PARAM_HDLC,      3, params->hdlc },
        { PATTY_AX25_PARAM_INFO_RX,   0, params->info_rx },
        { PATTY_AX25_PARAM_WINDOW_RX, 1, params->window_rx },
        { PATTY_AX25_PARAM_ACK,       0, params->ack },
        { PATTY_AX25_PARAM_RETRY,     0, params->retry },
        { 0, 0, 0 }
    };

    int i;

    patty_ax25_frame_xid_group *group = (patty_ax25_frame_xid_group *)data;

    memset(data, '\0', len);

    group->format = PATTY_AX25_FRAME_XID_GROUP_ISO8885;
    group->type   = PATTY_AX25_FRAME_XID_GROUP_PARAMS;

    offset += sizeof(*group);

    for (i=0; values[i].id; i++) {
        size_t bytes = values[i].bytes? values[i].bytes:
                                        needbytes(values[i].value);

        if (!(params->flags & (1 << values[i].id))) {
            continue;
        }

        if ((encoded = encode_xid_param(data,
                                        offset,
                                        values[i].id,
                                        bytes,
                                        values[i].value,
                                        len)) < 0) {
            goto error_encode_xid_param;
        } else {
            offset += encoded;
        }
    }

    group->len = htobe16(offset - sizeof(*group));

    return offset;

error_encode_xid_param:
    errno = EOVERFLOW;

    return -1;
}

ssize_t patty_ax25_frame_encode_reply_to(patty_ax25_frame *frame,
                                         patty_ax25_frame *reply,
                                         void *buf,
                                         size_t len) {
    size_t offset;

    offset = encode_address(frame, buf, 1, len);

    switch (reply->format) {
        case PATTY_AX25_FRAME_NORMAL:
            ((uint8_t *)buf)[offset++] = reply->control;

            break;

        case PATTY_AX25_FRAME_EXTENDED:
            ((uint8_t *)buf)[offset++] = reply->control & 0x00ff;

            if (!PATTY_AX25_FRAME_CONTROL_U(reply->control)) {
                ((uint8_t *)buf)[offset++] = (reply->control & 0xff00) >> 8;
            }

            break;
    }

    if (PATTY_AX25_FRAME_CONTROL_I(reply->control) || PATTY_AX25_FRAME_CONTROL_UI(reply->control)) {
        if (len < 1 + offset + reply->infolen) {
            errno = EOVERFLOW;

            goto error_toobig;
        }

        ((uint8_t *)buf)[offset++] = reply->proto;

        memcpy((uint8_t *)buf + offset, reply->info, reply->infolen);

        offset += reply->infolen;
    }

    return offset;

error_toobig:
    return -1;
}

enum patty_ax25_version patty_ax25_frame_version(patty_ax25_frame *frame) {
    return frame->version;
}

enum patty_ax25_frame_type patty_ax25_frame_type(patty_ax25_frame *frame) {
    return frame->type;
}

enum patty_ax25_frame_cr patty_ax25_frame_cr(patty_ax25_frame *frame) {
    return frame->cr;
}

ssize_t patty_ax25_frame_info(patty_ax25_frame *frame,
                              void **info) {
    if (frame == NULL || frame->info == NULL || info == NULL) {
        errno = EINVAL;

        goto error_invalid_args;
    }

    *info = frame->info;

    return frame->infolen;

error_invalid_args:
    return -1;
}
