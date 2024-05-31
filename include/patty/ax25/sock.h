#ifndef _PATTY_AX25_SOCK_H
#define _PATTY_AX25_SOCK_H

#include <patty/timer.h>

/*
 * Default socket parameters for all versions of AX.25 <=2.0
 */
#define PATTY_AX25_SOCK_DEFAULT_DELAY      3000
#define PATTY_AX25_SOCK_DEFAULT_KEEPALIVE (1000 * 30)

#define PATTY_AX25_SOCK_DEFAULT_CLASSES \
    (PATTY_AX25_PARAM_CLASSES_ABM)

#define PATTY_AX25_SOCK_DEFAULT_HDLC \
    (PATTY_AX25_PARAM_HDLC_REJ \
   | PATTY_AX25_PARAM_HDLC_XADDR | PATTY_AX25_PARAM_HDLC_MODULO_8 \
   | PATTY_AX25_PARAM_HDLC_TEST  | PATTY_AX25_PARAM_HDLC_SYNC_TX)

#define PATTY_AX25_SOCK_DEFAULT_I_LEN   127 /* I field total, minus proto */
#define PATTY_AX25_SOCK_DEFAULT_WINDOW    4
#define PATTY_AX25_SOCK_DEFAULT_RETRY    10
#define PATTY_AX25_SOCK_DEFAULT_ACK    3000

/*
 * Default socket parameters for AX.25 v2.2
 */
#define PATTY_AX25_SOCK_2_2_DEFAULT_HDLC \
    (PATTY_AX25_PARAM_HDLC_REJ         | PATTY_AX25_PARAM_HDLC_SREJ \
   | PATTY_AX25_PARAM_HDLC_XADDR       | PATTY_AX25_PARAM_HDLC_MODULO_128 \
   | PATTY_AX25_PARAM_HDLC_TEST        | PATTY_AX25_PARAM_HDLC_FCS_16 \
   | PATTY_AX25_PARAM_HDLC_SYNC_TX)

#define PATTY_AX25_SOCK_2_2_DEFAULT_I_LEN  255
#define PATTY_AX25_SOCK_2_2_DEFAULT_WINDOW  32

/*
 * Default socket parameters for AX.25 v2.2 prior to negotiation
 */
#define PATTY_AX25_SOCK_2_2_MAX_HDLC \
    (PATTY_AX25_PARAM_HDLC_REJ         | PATTY_AX25_PARAM_HDLC_SREJ \
   | PATTY_AX25_PARAM_HDLC_XADDR       | PATTY_AX25_PARAM_HDLC_MODULO_128 \
   | PATTY_AX25_PARAM_HDLC_TEST        | PATTY_AX25_PARAM_HDLC_FCS_16 \
   | PATTY_AX25_PARAM_HDLC_SYNC_TX     | PATTY_AX25_PARAM_HDLC_SREJ_MULTI)

#define PATTY_AX25_SOCK_2_2_MAX_I_LEN  1536
#define PATTY_AX25_SOCK_2_2_MAX_WINDOW  127

/*
 * Parameters flags to be set when calling patty_client_setsockopt() with
 * PATTY_AX25_SOCK_PARAMS
 */
#define PATTY_AX25_SOCK_PARAM_MTU    (1 << PATTY_AX25_PARAM_INFO_TX)
#define PATTY_AX25_SOCK_PARAM_WINDOW (1 << PATTY_AX25_PARAM_WINDOW_TX)
#define PATTY_AX25_SOCK_PARAM_ACK    (1 << PATTY_AX25_PARAM_ACK)
#define PATTY_AX25_SOCK_PARAM_RETRY  (1 << PATTY_AX25_PARAM_RETRY)

/*
 * Segmenter/reassembler information
 */
#define PATTY_AX25_SOCK_SEGMENTS_MAX 128

enum patty_ax25_sock_type {
    PATTY_AX25_SOCK_STREAM,
    PATTY_AX25_SOCK_DGRAM,
    PATTY_AX25_SOCK_RAW
};

enum patty_ax25_sock_state {
    PATTY_AX25_SOCK_CLOSED,
    PATTY_AX25_SOCK_LISTENING,
    PATTY_AX25_SOCK_PENDING_ACCEPT,
    PATTY_AX25_SOCK_PENDING_CONNECT,
    PATTY_AX25_SOCK_PENDING_DISCONNECT,
    PATTY_AX25_SOCK_ESTABLISHED,
    PATTY_AX25_SOCK_PROMISC
};

enum patty_ax25_sock_mode {
    PATTY_AX25_SOCK_DM,
    PATTY_AX25_SOCK_SABM,
    PATTY_AX25_SOCK_SABME
};

enum patty_ax25_sock_flow {
    PATTY_AX25_SOCK_WAIT,
    PATTY_AX25_SOCK_READY
};

enum patty_ax25_sock_opt {
    PATTY_AX25_SOCK_PARAMS,
    PATTY_AX25_SOCK_IF
};

typedef struct _patty_ax25_sock_assembler {
    size_t total,
           remaining,
           offset;
} patty_ax25_sock_assembler;

typedef struct _patty_ax25_sock {
    /*
     * Socket attributes
     */
    enum patty_ax25_proto      proto;
    enum patty_ax25_sock_type  type;
    enum patty_ax25_version    version;
    enum patty_ax25_sock_state state;
    enum patty_ax25_sock_mode  mode;
    enum patty_ax25_sock_flow  flow;

    /*
     * Network interface, bound addresses and source routing information
     */
    patty_ax25_if *iface;

    patty_ax25_addr local,
                    remote,
                    repeaters[PATTY_AX25_MAX_HOPS];

    int hops;

    /*
     * Reader for raw packets for PATTY_AX25_SOCK_RAW type
     */
    struct _patty_kiss_tnc *raw;

    /*
     * File descriptor information
     */
    int fd;
    char pty[PATTY_AX25_SOCK_PATH_SIZE];

    /*
     * Transmit and receive buffers
     */
    void *tx_buf,
         *io_buf;

    void *tx_slots;

    patty_ax25_sock_assembler *assembler;

    /*
     * Socket runtime parameter flags and values
     */
    uint32_t flags_classes,
             flags_hdlc;

    size_t n_maxlen_tx,
           n_maxlen_rx,
           n_window_tx,
           n_window_rx,
           n_ack,
           n_retry;

    /*
     * Socket timers
     */
    patty_timer timer_t1,
                timer_t2,
                timer_t3;

    /*
     * AX.25 v2.2 Section 4.2.4 "Frame Variables and Sequence Numbers"
     */
    int vs,
        vr,
        va;

    size_t retries,
           rx_pending;
} patty_ax25_sock;

/*
 * Socket instantiation, destruction and initialization
 */
patty_ax25_sock *patty_ax25_sock_new(enum patty_ax25_proto proto,
                                     enum patty_ax25_sock_type type);

void patty_ax25_sock_destroy(patty_ax25_sock *sock);

void patty_ax25_sock_init(patty_ax25_sock *sock);

void patty_ax25_sock_reset(patty_ax25_sock *sock);

/*
 * Setters for specific parameters
 */
void patty_ax25_sock_mtu_set(patty_ax25_sock *sock, size_t mtu);

void patty_ax25_sock_ack_set(patty_ax25_sock *sock, time_t ack);

void patty_ax25_sock_window_set(patty_ax25_sock *sock, size_t window);

void patty_ax25_sock_retry_set(patty_ax25_sock *sock, size_t retry);

/*
 * AX.25 version-specific parameter defaults and negotiation
 */
void patty_ax25_sock_params_upgrade(patty_ax25_sock *sock);

void patty_ax25_sock_params_max(patty_ax25_sock *sock);

int patty_ax25_sock_params_negotiate(patty_ax25_sock *sock,
                                     patty_ax25_params *params);

/*
 * TX/RX buffer allocation
 */
int patty_ax25_sock_realloc_bufs(patty_ax25_sock *sock);

char *patty_ax25_sock_pty(patty_ax25_sock *sock);

int patty_ax25_sock_bind_if(patty_ax25_sock *sock,
                            patty_ax25_if *iface);

/*
 * Stream-oriented state management
 */
void patty_ax25_sock_vs_incr(patty_ax25_sock *sock);

void patty_ax25_sock_vr_incr(patty_ax25_sock *sock);

int patty_ax25_sock_flow_left(patty_ax25_sock *sock);

/*
 * Frame segment reassembly state machine
 */
int patty_ax25_sock_assembler_init(patty_ax25_sock *sock, size_t total);

void patty_ax25_sock_assembler_stop(patty_ax25_sock *sock);

int patty_ax25_sock_assembler_pending(patty_ax25_sock *sock, size_t remaining);

ssize_t patty_ax25_sock_assembler_save(patty_ax25_sock *sock,
                                       void *buf,
                                       size_t len);

void *patty_ax25_sock_assembler_read(patty_ax25_sock *sock,
                                     uint8_t *proto,
                                     size_t *len);

/*
 * Socket I/O methods
 */
ssize_t patty_ax25_sock_send(patty_ax25_sock *sock,
                             void *buf,
                             size_t len);

ssize_t patty_ax25_sock_recv(patty_ax25_sock *sock,
                             void *buf,
                             size_t len);

ssize_t patty_ax25_sock_resend(patty_ax25_sock *sock, int seq);

ssize_t patty_ax25_sock_resend_pending(patty_ax25_sock *sock);

int patty_ax25_sock_ack(patty_ax25_sock *sock, int nr);

int patty_ax25_sock_ack_pending(patty_ax25_sock *sock);

ssize_t patty_ax25_sock_send_rr(patty_ax25_sock *sock,
                                enum patty_ax25_frame_cr cr,
                                int pf);

ssize_t patty_ax25_sock_send_rnr(patty_ax25_sock *sock,
                                 enum patty_ax25_frame_cr cr,
                                 int pf);

ssize_t patty_ax25_sock_send_rej(patty_ax25_sock *sock,
                                 enum patty_ax25_frame_cr cr,
                                 int pf);

ssize_t patty_ax25_sock_send_srej(patty_ax25_sock *sock,
                                  enum patty_ax25_frame_cr cr);

ssize_t patty_ax25_sock_send_sabm(patty_ax25_sock *sock, int pf);

ssize_t patty_ax25_sock_send_disc(patty_ax25_sock *sock, int pf);

ssize_t patty_ax25_sock_send_xid(patty_ax25_sock *sock,
                                 enum patty_ax25_frame_cr cr);

ssize_t patty_ax25_sock_send_test(patty_ax25_sock *sock,
                                  enum patty_ax25_frame_cr cr,
                                  void *info,
                                  size_t infolen);

ssize_t patty_ax25_sock_write(patty_ax25_sock *sock,
                              void *buf,
                              size_t len);

#endif /* _PATTY_AX25_SOCK_H */
