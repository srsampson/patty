#ifndef _PATTY_AX25_H
#define _PATTY_AX25_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>

#include <patty/list.h>
#include <patty/dict.h>

#define PATTY_AX25_CALLSTRLEN       6
#define PATTY_AX25_ADDRSTRLEN       (PATTY_AX25_CALLSTRLEN + 3)
#define PATTY_AX25_IF_NAME_SIZE     8
#define PATTY_AX25_MAX_HOPS         8
#define PATTY_AX25_SOCK_PATH_SIZE 256

enum patty_ax25_version {
    PATTY_AX25_OLD,
    PATTY_AX25_2_0,
    PATTY_AX25_2_2
};

enum patty_ax25_proto {
    PATTY_AX25_PROTO_UNKNOWN       = 0x00,
    PATTY_AX25_PROTO_ISO_8208      = 0x01,
    PATTY_AX25_PROTO_TCP_VJ_COMPR  = 0x06,
    PATTY_AX25_PROTO_TCP_VJ        = 0x07,
    PATTY_AX25_PROTO_FRAGMENT      = 0x08,
    PATTY_AX25_PROTO_TEXNET        = 0xc3,
    PATTY_AX25_PROTO_LQP           = 0xc4,
    PATTY_AX25_PROTO_APPLETALK     = 0xca,
    PATTY_AX25_PROTO_APPLETALK_ARP = 0xcb,
    PATTY_AX25_PROTO_INET          = 0xcc,
    PATTY_AX25_PROTO_INET_ARP      = 0xcd,
    PATTY_AX25_PROTO_FLEXNET       = 0xce,
    PATTY_AX25_PROTO_NETROM        = 0xcf,
    PATTY_AX25_PROTO_NONE          = 0xf0,
    PATTY_AX25_PROTO_ESCAPE        = 0xff
};

#pragma pack(push)
#pragma pack(1)

typedef struct _patty_ax25_addr {
    char callsign[PATTY_AX25_CALLSTRLEN];
    uint8_t ssid;
} patty_ax25_addr;

#pragma pack(pop)

enum patty_ax25_param_type {
    PATTY_AX25_PARAM_CLASSES   =  2,
    PATTY_AX25_PARAM_HDLC      =  3,
    PATTY_AX25_PARAM_INFO_TX   =  5,
    PATTY_AX25_PARAM_INFO_RX   =  6,
    PATTY_AX25_PARAM_WINDOW_TX =  7,
    PATTY_AX25_PARAM_WINDOW_RX =  8,
    PATTY_AX25_PARAM_ACK       =  9,
    PATTY_AX25_PARAM_RETRY     = 10
};

/*
 * Note: These values are byte swapped from their listed order as per
 * AX.25 v2.2 Section 4.3.37 "Exchange Identification (XID) Frame"; bits 0-7
 * are in the first byte, 8-15 in the second, and 16-23 in the third.
 * The bit positions are shown in their original order (plus one) in the
 * aforementioned figure in the AX.25 v2.2 spec.
 */
enum patty_ax25_param_classes {
    PATTY_AX25_PARAM_CLASSES_ABM         = (1 << 0) << 8,
    PATTY_AX25_PARAM_CLASSES_HALF_DUPLEX = (1 << 5) << 8,
    PATTY_AX25_PARAM_CLASSES_FULL_DUPLEX = (1 << 6) << 8
};

enum patty_ax25_param_hdlc {
    PATTY_AX25_PARAM_HDLC_REJ        = (1 <<  1) << 16,
    PATTY_AX25_PARAM_HDLC_SREJ       = (1 <<  2) << 16,
    PATTY_AX25_PARAM_HDLC_XADDR      = (1 <<  7) << 16,
    PATTY_AX25_PARAM_HDLC_MODULO_8   = (1 << 10),
    PATTY_AX25_PARAM_HDLC_MODULO_128 = (1 << 11),
    PATTY_AX25_PARAM_HDLC_TEST       = (1 << 13),
    PATTY_AX25_PARAM_HDLC_FCS_16     = (1 << 15),
    PATTY_AX25_PARAM_HDLC_SYNC_TX    = (1 << 17) >> 16,
    PATTY_AX25_PARAM_HDLC_SREJ_MULTI = (1 << 21) >> 16
};

typedef struct _patty_ax25_params {
    uint32_t flags;

    uint32_t classes,
             hdlc;

    size_t info_tx,
           info_rx,
           window_tx,
           window_rx,
           ack,
           retry;
} patty_ax25_params;

typedef struct _patty_ax25_if patty_ax25_if;

#include <patty/client.h>
#include <patty/ax25/frame.h>
#include <patty/ax25/if.h>
#include <patty/ax25/route.h>
#include <patty/ax25/sock.h>
#include <patty/ax25/server.h>

#define PATTY_AX25_ADDR_CHAR_VALID(c) \
    ((c >= 0x20 && c <= 0x7e))

#define PATTY_AX25_ADDR_OCTET_LAST(c) \
    ((c & 0x01) == 0x01)

#define PATTY_AX25_ADDR_SSID_NUMBER(c) \
    ((c & 0x1e) >> 1)

#define PATTY_AX25_ADDR_SSID_C(c) \
    ((c & 0x80) == 0x80)

#define PATTY_AX25_ADDR_SSID_REPEATED(c) \
    ((c & 0x80) == 0x80)

int patty_ax25_pton(const char *callsign,
                    patty_ax25_addr *addr);

int patty_ax25_ntop(const patty_ax25_addr *addr,
                    char *dest,
                    size_t len);

void patty_ax25_addr_hash(uint32_t *hash,
                          const patty_ax25_addr *addr);

size_t patty_ax25_addr_copy(void *buf,
                            patty_ax25_addr *addr,
                            uint8_t ssid_flags);

#endif /* _PATTY_AX25_H */
