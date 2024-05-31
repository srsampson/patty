#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

#include <patty/ax25.h>
#include <patty/kiss/tnc.h>

#include "config.h"

struct slot {
    size_t len;
    int ack;
};

static int bind_pty(patty_ax25_sock *sock) {
    int ptysub;
    struct termios t;

    if (openpty(&sock->fd, &ptysub, sock->pty, NULL, NULL) < 0) {
        goto error_openpty;
    }

    if (grantpt(sock->fd) < 0) {
        goto error_grantpt;
    }

    if (unlockpt(sock->fd) < 0) {
        goto error_unlockpt;
    }

    memset(&t, '\0', sizeof(t));
    cfmakeraw(&t);

    if (tcsetattr(sock->fd, TCSANOW, &t) < 0) {
        errno = 0;
    }

    return 0;

error_unlockpt:
error_grantpt:
    (void)close(ptysub);
    (void)close(sock->fd);

error_openpty:
    return -1;
}

static inline size_t tx_bufsz(patty_ax25_sock *sock) {
    return PATTY_AX25_FRAME_OVERHEAD + sock->n_maxlen_tx;
}

static inline size_t io_bufsz(patty_ax25_sock *sock) {
    return PATTY_AX25_FRAME_OVERHEAD + sock->n_maxlen_tx;
}

static inline size_t tx_slots(patty_ax25_sock *sock) {
    return sock->mode == PATTY_AX25_SOCK_SABME? 128: 8;
}

static inline size_t tx_slot_size(patty_ax25_sock *sock) {
    return sizeof(struct slot) + sock->n_maxlen_tx;
}

static inline size_t tx_slots_size(patty_ax25_sock *sock) {
    return tx_slots(sock) * tx_slot_size(sock);
}

static inline int tx_seq(patty_ax25_sock *sock, int seq) {
    return seq % tx_slots(sock);
}

static inline struct slot *tx_slot(patty_ax25_sock *sock, int seq) {
    int i = tx_seq(sock, seq);

    return (struct slot *)
        ((uint8_t *)sock->tx_slots + (i * tx_slot_size(sock)));
}

static inline void tx_slot_save(patty_ax25_sock *sock, void *buf, size_t len) {
    struct slot *slot = tx_slot(sock, sock->vs);

    slot->len = len;
    slot->ack = 0;

    memcpy(slot + 1, buf, len);
}

static int init_bufs(patty_ax25_sock *sock) {
    size_t slots = tx_slots(sock),
           i;

    if ((sock->tx_buf = realloc(sock->tx_buf, tx_bufsz(sock))) == NULL) {
        goto error_realloc_tx_buf;
    }

    if ((sock->io_buf = realloc(sock->io_buf, io_bufsz(sock))) == NULL) {
        goto error_realloc_io_buf;
    }

    if ((sock->tx_slots = realloc(sock->tx_slots, tx_slots_size(sock))) == NULL) {
        goto error_realloc_tx_slots;
    }

    for (i=0; i<slots; i++) {
        struct slot *slot = tx_slot(sock, i);

        slot->len = 0;
        slot->ack = 0;
    }

    return 0;

error_realloc_tx_slots:
    free(sock->io_buf);
    sock->io_buf = NULL;

error_realloc_io_buf:
    free(sock->tx_buf);
    sock->tx_buf = NULL;

error_realloc_tx_buf:
    return -1;
}

static patty_ax25_sock *init_dgram(patty_ax25_sock *sock,
                                   enum patty_ax25_proto proto) {
    sock->proto = proto;
    sock->type  = PATTY_AX25_SOCK_DGRAM;

    return sock;
}

static patty_ax25_sock *init_raw(patty_ax25_sock *sock) {
    patty_kiss_tnc_info info = {
        .flags = PATTY_KISS_TNC_FD,
        .fd    = sock->fd
    };

    if ((sock->raw = patty_kiss_tnc_new(&info)) == NULL) {
        goto error_kiss_tnc_new;
    }

    sock->proto = PATTY_AX25_PROTO_NONE;
    sock->type  = PATTY_AX25_SOCK_RAW;
    sock->state = PATTY_AX25_SOCK_ESTABLISHED;

    return sock;

error_kiss_tnc_new:
    (void)close(sock->fd);

    return NULL;
}

patty_ax25_sock *patty_ax25_sock_new(enum patty_ax25_proto proto,
                                     enum patty_ax25_sock_type type) {
    patty_ax25_sock *sock;

    if ((sock = malloc(sizeof(*sock))) == NULL) {
        goto error_malloc_sock;
    }

    memset(sock, '\0', sizeof(*sock));

    if (bind_pty(sock) < 0) {
        goto error_bind_pty;
    }

    switch (type) {
        case PATTY_AX25_SOCK_DGRAM:
            return init_dgram(sock, proto);

        case PATTY_AX25_SOCK_RAW:
            return init_raw(sock);

        case PATTY_AX25_SOCK_STREAM:
            patty_ax25_sock_init(sock);
    }

    sock->proto         = proto;
    sock->type          = type;
    sock->version       = PATTY_AX25_2_0;
    sock->flags_classes = PATTY_AX25_SOCK_DEFAULT_CLASSES;
    sock->flags_hdlc    = PATTY_AX25_SOCK_DEFAULT_HDLC;

    if (init_bufs(sock) < 0) {
        goto error_init_bufs;
    }

    return sock;

error_init_bufs:
    if (sock->tx_slots) free(sock->tx_slots);
    if (sock->io_buf)   free(sock->io_buf);
    if (sock->tx_buf)   free(sock->tx_buf);

error_bind_pty:
    free(sock);

error_malloc_sock:
    return NULL;
}

void patty_ax25_sock_destroy(patty_ax25_sock *sock) {
    if (sock->type == PATTY_AX25_SOCK_RAW) {
        if (sock->state == PATTY_AX25_SOCK_PROMISC) {
            (void)patty_ax25_if_promisc_delete(sock->iface, sock->fd);
        }

        patty_kiss_tnc_destroy(sock->raw);
    }

    if (sock->fd > 0) {
        (void)close(sock->fd);
    }

    if (sock->assembler) {
        free(sock->assembler);
    }

    if (sock->tx_slots) {
        free(sock->tx_slots);
    }

    if (sock->io_buf) {
        free(sock->io_buf);
    }

    if (sock->tx_buf) {
        free(sock->tx_buf);
    }

    free(sock);
}

void patty_ax25_sock_init(patty_ax25_sock *sock) {
    sock->state       = PATTY_AX25_SOCK_CLOSED;
    sock->mode        = PATTY_AX25_SOCK_DM;
    sock->flow        = PATTY_AX25_SOCK_WAIT;
    sock->n_maxlen_tx = PATTY_AX25_SOCK_DEFAULT_I_LEN;
    sock->n_maxlen_rx = PATTY_AX25_SOCK_DEFAULT_I_LEN;
    sock->n_window_tx = PATTY_AX25_SOCK_DEFAULT_WINDOW;
    sock->n_window_rx = PATTY_AX25_SOCK_DEFAULT_WINDOW;
    sock->n_ack       = PATTY_AX25_SOCK_DEFAULT_ACK;
    sock->n_retry     = PATTY_AX25_SOCK_DEFAULT_RETRY;
    sock->retries     = PATTY_AX25_SOCK_DEFAULT_RETRY;
    sock->rx_pending  = 0;

    patty_timer_init(&sock->timer_t1, sock->n_ack);
    patty_timer_init(&sock->timer_t2, PATTY_AX25_SOCK_DEFAULT_DELAY);
    patty_timer_init(&sock->timer_t3, PATTY_AX25_SOCK_DEFAULT_KEEPALIVE);
}

/*
 * AX.25 v2.2, Section 6.5 "Resetting Procedure"
 */
void patty_ax25_sock_reset(patty_ax25_sock *sock) {
    int i,
        slots = tx_slots(sock);

    sock->flow       = PATTY_AX25_SOCK_READY;
    sock->vs         = 0;
    sock->vr         = 0;
    sock->va         = 0;
    sock->retries    = sock->n_retry;
    sock->rx_pending = 0;

    for (i=0; i<slots; i++) {
        struct slot *slot = tx_slot(sock, i);

        slot->len = 0;
        slot->ack = 0;
    }

    patty_timer_start(&sock->timer_t1);
    patty_timer_clear(&sock->timer_t2);
    patty_timer_clear(&sock->timer_t3);
}

void patty_ax25_sock_mtu_set(patty_ax25_sock *sock, size_t mtu) {
    sock->n_maxlen_tx = mtu;
}

void patty_ax25_sock_ack_set(patty_ax25_sock *sock, time_t ack) {
    sock->n_ack = (time_t)ack;
}

void patty_ax25_sock_window_set(patty_ax25_sock *sock, size_t window) {
    sock->n_window_tx = window;
}

void patty_ax25_sock_retry_set(patty_ax25_sock *sock, size_t retry) {
    sock->n_retry = retry;
}

void patty_ax25_sock_params_upgrade(patty_ax25_sock *sock) {
    if (sock->version >= PATTY_AX25_2_2) {
        return;
    }

    sock->version     = PATTY_AX25_2_2;
    sock->flags_hdlc  = PATTY_AX25_SOCK_2_2_DEFAULT_HDLC;
    sock->n_maxlen_tx = PATTY_AX25_SOCK_2_2_DEFAULT_I_LEN;
    sock->n_maxlen_rx = PATTY_AX25_SOCK_2_2_DEFAULT_I_LEN;
    sock->n_window_tx = PATTY_AX25_SOCK_2_2_DEFAULT_WINDOW;
    sock->n_window_rx = PATTY_AX25_SOCK_2_2_DEFAULT_WINDOW;
}

void patty_ax25_sock_params_max(patty_ax25_sock *sock) {
    sock->version     = PATTY_AX25_2_2;
    sock->flags_hdlc  = PATTY_AX25_SOCK_2_2_MAX_HDLC;
    sock->n_maxlen_tx = PATTY_AX25_SOCK_2_2_MAX_I_LEN;
    sock->n_maxlen_rx = PATTY_AX25_SOCK_2_2_MAX_I_LEN;
    sock->n_window_tx = PATTY_AX25_SOCK_2_2_MAX_WINDOW;
    sock->n_window_rx = PATTY_AX25_SOCK_2_2_MAX_WINDOW;
}

int patty_ax25_sock_params_negotiate(patty_ax25_sock *sock,
                                     patty_ax25_params *params) {
    if (params->flags & PATTY_AX25_PARAM_CLASSES) {
        if (!(params->classes & PATTY_AX25_PARAM_CLASSES_ABM)) {
            goto error_notsup;
        }

        if (!(params->classes & PATTY_AX25_PARAM_CLASSES_HALF_DUPLEX)) {
            goto error_notsup;
        }

        if (params->classes & PATTY_AX25_PARAM_CLASSES_FULL_DUPLEX) {
            goto error_notsup;
        }
    }

    if (params->flags & PATTY_AX25_PARAM_HDLC) {
        if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_REJ)) {
            sock->flags_hdlc &= ~PATTY_AX25_PARAM_HDLC_REJ;
        }

        if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_SREJ)) {
            sock->flags_hdlc &= ~PATTY_AX25_PARAM_HDLC_SREJ;
        }

        if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_XADDR)) {
            goto error_invalid;
        }

        if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_MODULO_8)) {
            if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_MODULO_128)) {
                goto error_invalid;
            }
        } else {
            if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_MODULO_128)) {
                goto error_invalid;
            }

            params->hdlc &= ~PATTY_AX25_PARAM_HDLC_MODULO_8;
            params->hdlc |=  PATTY_AX25_PARAM_HDLC_MODULO_128;
        }

        if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_TEST)) {
            params->hdlc &= ~PATTY_AX25_PARAM_HDLC_TEST;
        }

        if (!(params->hdlc & PATTY_AX25_PARAM_HDLC_SYNC_TX)) {
            goto error_invalid;
        }
    }

    if (params->flags & PATTY_AX25_PARAM_INFO_RX) {
        if (sock->n_maxlen_tx > params->info_rx >> 3) {
            sock->n_maxlen_tx = params->info_rx >> 3;
        }
    }

    if (params->flags & PATTY_AX25_PARAM_WINDOW_RX) {
        if (sock->n_window_tx > params->window_rx) {
            sock->n_window_tx = params->window_rx;
        }
    }

    if (params->flags & PATTY_AX25_PARAM_ACK) {
        if (sock->n_ack < params->ack) {
            sock->n_ack = params->ack;

            patty_timer_init(&sock->timer_t1, params->ack);
        }
    }

    if (params->flags & PATTY_AX25_PARAM_RETRY) {
        if (sock->n_retry < params->retry) {
            sock->n_retry = params->retry;
        }
    }

    return 0;

error_notsup:
    errno = ENOTSUP;

    return -1;

error_invalid:
    errno = EINVAL;

    return -1;
}

int patty_ax25_sock_realloc_bufs(patty_ax25_sock *sock) {
    return init_bufs(sock);
}

char *patty_ax25_sock_pty(patty_ax25_sock *sock) {
    return sock->pty;
}

int patty_ax25_sock_bind_if(patty_ax25_sock *sock,
                            patty_ax25_if *iface) {
    sock->iface = iface;
    sock->flags_classes |= iface->flags_classes;

    if (sock->state == PATTY_AX25_SOCK_PROMISC) {
        if (patty_ax25_if_promisc_add(iface, sock->fd) < 0) {
            goto error_if_promisc_add;
        }
    }

    return 0;

error_if_promisc_add:
    return -1;
}

void patty_ax25_sock_vs_incr(patty_ax25_sock *sock) {
    if (sock->mode == PATTY_AX25_SOCK_SABM) {
        sock->vs = (sock->vs + 1) & 0x07;
    } else {
        sock->vs = (sock->vs + 1) & 0x7f;
    }
}

void patty_ax25_sock_vr_incr(patty_ax25_sock *sock) {
    if (sock->mode == PATTY_AX25_SOCK_SABM) {
        sock->vr = (sock->vr + 1) & 0x07;
    } else {
        sock->vr = (sock->vr + 1) & 0x7f;
    }
}

int patty_ax25_sock_assembler_init(patty_ax25_sock *sock, size_t total) {
    if (total < 2) {
        errno = EINVAL;

        goto error_invalid;
    }

    if (sock->assembler == NULL || sock->assembler->total < total) {
        size_t size = sizeof(patty_ax25_sock_assembler)
                    + total * sock->n_maxlen_rx;

        if ((sock->assembler = malloc(size)) == NULL) {
            goto error_malloc;
        }

        sock->assembler->total     = total;
        sock->assembler->remaining = total;
        sock->assembler->offset    = 0;
    }

    return 0;

error_malloc:
error_invalid:
    return -1;
}

int patty_ax25_sock_assembler_pending(patty_ax25_sock *sock, size_t remaining) {
    if (sock->assembler == NULL || sock->assembler->total == 0) {
        return 0;
    }

    return remaining + 1 == sock->assembler->remaining? 1: 0;
}

void patty_ax25_sock_assembler_stop(patty_ax25_sock *sock) {
    if (sock->assembler == NULL) {
        return;
    }

    sock->assembler->remaining = 0;
    sock->assembler->total     = 0;
}

ssize_t patty_ax25_sock_assembler_save(patty_ax25_sock *sock,
                                       void *buf,
                                       size_t len) {
    uint8_t *dest = (uint8_t *)(sock->assembler + 1);

    if (len > sock->n_maxlen_rx - 1) {
        errno = EOVERFLOW;

        goto error_overflow;
    }

    if (sock->assembler->remaining == 0) {
        errno = EIO;

        goto error_underflow;
    }

    memcpy(dest + sock->assembler->offset, buf, len);

    sock->assembler->offset += len;
    sock->assembler->remaining--;

    return len;

error_underflow:
error_overflow:
    return -1;
}

void *patty_ax25_sock_assembler_read(patty_ax25_sock *sock,
                                     uint8_t *proto,
                                     size_t *len) {
    uint8_t *buf = (uint8_t *)(sock->assembler + 1);

    if (sock->assembler == NULL) {
        if (proto) *proto = 0;
        if (len)   *len   = 0;

        return NULL;
    }

    if (proto) *proto = buf[0];
    if (len)   *len   = sock->assembler->offset - 1;

    return buf + 1;
}

int patty_ax25_sock_flow_left(patty_ax25_sock *sock) {
    /*
     * 6.4.1 Sending I Frames
     *
     * The TNC does not transmit any more I frames if its send state variable
     * V(S) equals the last received N(R) from the other side of the link plus
     * k.  If the TNC sent more I frames, the flow control window would be
     * exceeded and errors could result.
     */
    if (sock->n_window_tx == 1) {
        return 1;
    }

    return sock->vs - tx_seq(sock, sock->va + sock->n_window_tx);
}

static inline int toobig(patty_ax25_sock *sock,
                         size_t infolen) {
    return infolen > PATTY_AX25_FRAME_OVERHEAD + sock->n_maxlen_tx;
}

static ssize_t encode_address(patty_ax25_sock *sock,
                              enum patty_ax25_frame_cr cr,
                              void *dest,
                              size_t len) {
    uint8_t *buf = (uint8_t *)dest;
    size_t offset = 0;

    uint8_t flags_remote = 0x00,
            flags_local  = 0x00;

    unsigned int i;

    if ((2 + sock->hops) * sizeof(patty_ax25_addr) > len) {
        errno = EOVERFLOW;

        goto error_toobig;
    }

    switch (cr) {
        case PATTY_AX25_FRAME_COMMAND:  flags_remote = 0x80; break;
        case PATTY_AX25_FRAME_RESPONSE: flags_local  = 0x80; break;
        case PATTY_AX25_FRAME_OLD: break;
    }

    offset += patty_ax25_addr_copy(buf + offset, &sock->remote, flags_remote);
    offset += patty_ax25_addr_copy(buf + offset, &sock->local,  flags_local);

    for (i=0; i<sock->hops; i++) {
        offset += patty_ax25_addr_copy(buf + offset, &sock->repeaters[i], 0);
    }

    ((uint8_t *)buf)[offset-1] |= 1;

    return offset;

error_toobig:
    return -1;
}

ssize_t patty_ax25_sock_send(patty_ax25_sock *sock,
                             void *buf,
                             size_t len) {
    if (sock->type != PATTY_AX25_SOCK_RAW) {
        errno = EINVAL;

        goto error_invalid_type;
    }

    if (sock->iface == NULL) {
        errno = ENETDOWN;

        goto error_noif;
    }

    return patty_ax25_if_send(sock->iface, buf, len);

error_noif:
error_invalid_type:
    return -1;
}

ssize_t patty_ax25_sock_recv(patty_ax25_sock *sock,
                             void *buf,
                             size_t len) {
    if (sock->type != PATTY_AX25_SOCK_RAW) {
        errno = EINVAL;

        goto error_invalid_type;
    }

    return patty_kiss_tnc_recv(sock->raw, buf, len);

error_invalid_type:
    return -1;
}

static inline uint16_t control_i(patty_ax25_sock *sock, int ns) {
    int flag = patty_ax25_sock_flow_left(sock) == 1? 1: 0;

    switch (sock->mode) {
        case PATTY_AX25_SOCK_SABM:
            return ((sock->vr & 0x07) << 5)
                 | ((      ns & 0x07) << 1)
                 | (flag << 4);

        case PATTY_AX25_SOCK_SABME:
            return ((sock->vr & 0x7f) << 9)
                 | ((      ns & 0x7f) << 1)
                 | (flag << 8);

        default:
            return 0;
    }
}

static inline uint16_t control_ui(int flag) {
    return PATTY_AX25_FRAME_UI | (flag << 4);
}

static inline uint16_t control_s(patty_ax25_sock *sock,
                          enum patty_ax25_frame_type type,
                          int flag) {
    switch (sock->mode) {
        case PATTY_AX25_SOCK_SABM:
            return ((sock->vr & 0x07) << 5)
                 | (type & 0x0f)
                 | (flag << 4);

        case PATTY_AX25_SOCK_SABME:
            return ((sock->vr & 0x7f) << 9)
                 | (type & 0x0f)
                 | (flag << 8);

        default:
            return 0;
    }
}

static inline uint16_t control_u(enum patty_ax25_frame_type type,
                          int pf) {
    return (type & PATTY_AX25_FRAME_U_MASK)
         | (pf << 4);
}

static ssize_t frame_send(patty_ax25_sock *sock,
                          enum patty_ax25_frame_cr cr,
                          uint16_t control,
                          uint8_t proto,
                          void *info,
                          size_t infolen) {
     size_t offset = 0;
    ssize_t encoded;

    uint8_t *buf = sock->tx_buf;

    if (sock->iface == NULL) {
        errno = ENETDOWN;

        goto error_noif;
    }

    if (sock->remote.callsign[0] == '\0') {
        errno = EBADF;

        goto error_nopeer;
    }

    if (toobig(sock, infolen)) {
        goto error_toobig;
    }

    if ((encoded = encode_address(sock, cr, buf, tx_bufsz(sock))) < 0) {
        goto error_encode_address;
    } else {
        offset += encoded;
    }

    buf[offset++] = control & 0x00ff;

    if (sock->mode == PATTY_AX25_SOCK_SABME && !PATTY_AX25_FRAME_CONTROL_U(control)) {
        buf[offset++] = (control & 0xff00) >> 8;
    }

    if (info && infolen) {
        if (PATTY_AX25_FRAME_CONTROL_I(control)) {
            buf[offset++] = proto;
        }

        memcpy(buf + offset, info, infolen);

        offset += infolen;
    }

    return patty_ax25_if_send(sock->iface, buf, offset);

error_encode_address:
error_toobig:
error_nopeer:
error_noif:
    return -1;
}

ssize_t patty_ax25_sock_resend(patty_ax25_sock *sock, int seq) {
    struct slot *slot = tx_slot(sock, seq);

    return slot->len > 0? frame_send(sock,
                                     PATTY_AX25_FRAME_COMMAND,
                                     control_i(sock, seq),
                                     sock->proto,
                                     slot + 1,
                                     slot->len): 0;
}

ssize_t patty_ax25_sock_resend_pending(patty_ax25_sock *sock) {
    struct slot *slot = tx_slot(sock, sock->vs);

    if (slot->len > 0 && !slot->ack) {
        ssize_t len;

        if ((len = patty_ax25_sock_resend(sock, sock->vs)) < 0) {
            goto error_resend;
        }

        sock->vs = tx_seq(sock, sock->vs + 1);

        return len;
    }

    return 0;

error_resend:
    return -1;
}

int patty_ax25_sock_ack(patty_ax25_sock *sock, int nr) {
    int ret = 0,
        min = sock->va,
        max = nr,
        i;

    if (min == max) {
        return 1;
    }

    if (max < min) {
        max += sock->mode == PATTY_AX25_SOCK_SABME? 128: 8;
    }

    for (i=min; i<max; i++) {
        struct slot *slot = tx_slot(sock, i);

        if (slot->len > 0 && !slot->ack) {
            slot->ack = 1;

            sock->va = tx_seq(sock, i + 1);

            ret++;
        }
    }

    return ret;
}

int patty_ax25_sock_ack_pending(patty_ax25_sock *sock) {
    int ret = 0;

    size_t slots = tx_slots(sock),
           i;

    for (i=0; i<slots; i++) {
        struct slot *slot = tx_slot(sock, i);

        if (slot->len > 0 && !slot->ack) {
            ret++;
        }
    }

    return ret;
}

ssize_t patty_ax25_sock_send_rr(patty_ax25_sock *sock,
                                enum patty_ax25_frame_cr cr,
                                int pf) {
    return frame_send(sock,
                      cr,
                      control_s(sock, PATTY_AX25_FRAME_RR, pf),
                      0,
                      NULL,
                      0);
}

ssize_t patty_ax25_sock_send_rnr(patty_ax25_sock *sock,
                                 enum patty_ax25_frame_cr cr,
                                 int pf) {
    return frame_send(sock,
                      cr,
                      control_s(sock, PATTY_AX25_FRAME_RNR, pf),
                      0,
                      NULL,
                      0);
}

ssize_t patty_ax25_sock_send_rej(patty_ax25_sock *sock,
                                 enum patty_ax25_frame_cr cr,
                                 int pf) {
    return frame_send(sock,
                      cr,
                      control_s(sock, PATTY_AX25_FRAME_REJ, pf),
                      0,
                      NULL,
                      0);
}

ssize_t patty_ax25_sock_send_srej(patty_ax25_sock *sock,
                                  enum patty_ax25_frame_cr cr) {
    return frame_send(sock,
                      cr,
                      control_s(sock, PATTY_AX25_FRAME_SREJ, 1),
                      0,
                      NULL,
                      0);
}

ssize_t patty_ax25_sock_send_sabm(patty_ax25_sock *sock, int pf) {
    enum patty_ax25_frame_type type = (sock->mode == PATTY_AX25_SOCK_SABME)?
        PATTY_AX25_FRAME_SABME:
        PATTY_AX25_FRAME_SABM;

    return frame_send(sock,
                      PATTY_AX25_FRAME_COMMAND,
                      control_u(type, pf),
                      0,
                      NULL,
                      0);
}

ssize_t patty_ax25_sock_send_disc(patty_ax25_sock *sock, int pf) {
    return frame_send(sock,
                      PATTY_AX25_FRAME_COMMAND,
                      control_u(PATTY_AX25_FRAME_DISC, pf),
                      0,
                      NULL,
                      0);
}

ssize_t patty_ax25_sock_send_xid(patty_ax25_sock *sock,
                                 enum patty_ax25_frame_cr cr) {
    patty_ax25_params params;
    char buf[256];

    ssize_t encoded;

    if (sock->iface == NULL) {
        errno = ENETDOWN;

        goto error_noif;
    }

    params.flags = (1 << PATTY_AX25_PARAM_CLASSES)
                 | (1 << PATTY_AX25_PARAM_HDLC)
                 | (1 << PATTY_AX25_PARAM_INFO_RX)
                 | (1 << PATTY_AX25_PARAM_WINDOW_RX)
                 | (1 << PATTY_AX25_PARAM_ACK)
                 | (1 << PATTY_AX25_PARAM_RETRY);

    params.classes   = sock->flags_classes;
    params.hdlc      = PATTY_AX25_SOCK_2_2_MAX_HDLC;
    params.info_rx   = PATTY_AX25_SOCK_2_2_MAX_I_LEN << 3;
    params.window_rx = PATTY_AX25_SOCK_2_2_MAX_WINDOW;
    params.ack       = sock->n_ack;
    params.retry     = sock->n_retry;

    if ((encoded = patty_ax25_frame_encode_xid(&params, buf, sizeof(buf))) < 0) {
        goto error_frame_encode_xid;
    }

    return frame_send(sock,
                      cr,
                      control_u(PATTY_AX25_FRAME_XID, 0),
                      0,
                      buf,
                      encoded);

error_frame_encode_xid:
error_noif:
    return -1;
}

ssize_t patty_ax25_sock_send_test(patty_ax25_sock *sock,
                                  enum patty_ax25_frame_cr cr,
                                  void *info,
                                  size_t infolen) {
    return frame_send(sock,
                      PATTY_AX25_FRAME_COMMAND,
                      control_u(PATTY_AX25_FRAME_TEST, 1),
                      0,
                      info,
                      infolen);
}

static ssize_t write_segmented(patty_ax25_sock *sock,
                               void *buf,
                               size_t len) {
    size_t segments,
           seglen;

    size_t i = 0;

    int first = 1;

    if (sock->n_maxlen_tx < 2) {
        errno = EOVERFLOW;

        goto error_toobig;
    }

    seglen   = sock->n_maxlen_tx - 1;
    segments = (len + 1) / seglen;

    if (len % seglen) {
        segments++;
    }

    if (segments > PATTY_AX25_SOCK_SEGMENTS_MAX) {
        errno = EOVERFLOW;

        goto error_toobig;
    }

    while (segments--) {
        uint8_t *dest  = (uint8_t *)sock->tx_buf;
        uint8_t header = (uint8_t)(segments & 0xff);

        size_t copylen = segments == 0?
            len - (len % seglen):
            seglen & 0xff;

        size_t o = 0;

        uint16_t control = sock->type == PATTY_AX25_SOCK_STREAM?
            control_i(sock, sock->vs):
            control_ui(0);

        if (first) {
            header |= 0x80;
        }

        dest[o++] = header;

        if (first) {
            dest[o++] = sock->proto;
            copylen--;
            first = 0;
        }

        tx_slot_save(sock, buf + i, copylen);

        memcpy(dest + o, (uint8_t *)buf + i, copylen);

        i += copylen;
        o += copylen;

        if (frame_send(sock,
                       PATTY_AX25_FRAME_COMMAND,
                       control,
                       PATTY_AX25_PROTO_FRAGMENT,
                       sock->tx_buf,
                       o) < 0) {
            goto error_frame_send;
        }

        if (sock->type == PATTY_AX25_SOCK_STREAM) {
            patty_ax25_sock_vs_incr(sock);
        }
    }

    return len;

error_frame_send:
error_toobig:
    return -1;
}

ssize_t patty_ax25_sock_write(patty_ax25_sock *sock,
                              void *buf,
                              size_t len) {
    if (sock->mode == PATTY_AX25_SOCK_DM) {
        errno = EBADF;

        goto error_invalid_mode;
    }

    switch (sock->type) {
        case PATTY_AX25_SOCK_STREAM:{
            if (len > sock->n_maxlen_tx) {
                return write_segmented(sock, buf, len);
            }

            tx_slot_save(sock, buf, len);

            if (frame_send(sock,
                           PATTY_AX25_FRAME_COMMAND,
                           control_i(sock, sock->vs),
                           sock->proto,
                           buf,
                           len) < 0) {
                goto error_frame_send;
            }

            patty_ax25_sock_vs_incr(sock);

            break;
        }

        case PATTY_AX25_SOCK_DGRAM:
            if (len > sock->n_maxlen_tx) {
                return write_segmented(sock, buf, len);
            }

            if (frame_send(sock,
                           PATTY_AX25_FRAME_COMMAND,
                           control_ui(0),
                           sock->proto,
                           buf,
                           len) < 0) {
                goto error_frame_send;
            }

            break;

        case PATTY_AX25_SOCK_RAW:
            return patty_ax25_if_send(sock->iface, buf, len);
    }

    return len;

error_frame_send:
error_invalid_mode:
    return -1;
}
