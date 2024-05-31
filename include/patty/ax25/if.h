#ifndef _PATTY_AX25_IF_H
#define _PATTY_AX25_IF_H

#include <stdint.h>
#include <sys/types.h>

#define PATTY_AX25_IF_DEFAULT_CLASSES \
    (PATTY_AX25_PARAM_CLASSES_HALF_DUPLEX)

#define PATTY_AX25_IF_DEFAULT_MTU 4096
#define PATTY_AX25_IF_DEFAULT_MRU 4096

enum patty_ax25_if_flags {
    PATTY_AX25_IF_HALF_DUPLEX = (1 << 0),
    PATTY_AX25_IF_FULL_DUPLEX = (1 << 1),
    PATTY_AX25_IF_SYNC_TX     = (1 << 6),
    PATTY_AX25_IF_SEGMENTER   = (1 << 7)
};

enum patty_ax25_if_status {
    PATTY_AX25_IF_DOWN,
    PATTY_AX25_IF_UP,
    PATTY_AX25_IF_ERROR
};

typedef struct _patty_ax25_if_stats {
    size_t rx_frames,
           tx_frames,
           rx_bytes,
           tx_bytes,
           dropped;
} patty_ax25_if_stats;

typedef void *(patty_ax25_if_driver_create)(void *);

typedef void (patty_ax25_if_driver_destroy)(void *);

typedef patty_ax25_if_stats *(patty_ax25_if_driver_stats)(void *);

typedef int (patty_ax25_if_driver_fd)(void *);

typedef int (patty_ax25_if_driver_ready)(void *, fd_set *);

typedef int (patty_ax25_if_driver_reset)(void *);

typedef ssize_t (patty_ax25_if_driver_fill)(void *);

typedef ssize_t (patty_ax25_if_driver_drain)(void *, void *, size_t);

typedef int (patty_ax25_if_driver_pending)(void *);

typedef ssize_t (patty_ax25_if_driver_flush)(void *);

typedef ssize_t (patty_ax25_if_driver_send)(void *, const void *, size_t);

typedef struct _patty_ax25_if_driver {
    patty_ax25_if_driver_create *create;
    patty_ax25_if_driver_destroy *destroy;
    patty_ax25_if_driver_stats *stats;
    patty_ax25_if_driver_fd *fd;
    patty_ax25_if_driver_ready *ready;
    patty_ax25_if_driver_reset *reset;
    patty_ax25_if_driver_fill *fill;
    patty_ax25_if_driver_drain *drain;
    patty_ax25_if_driver_pending *pending;
    patty_ax25_if_driver_flush *flush;
    patty_ax25_if_driver_send *send;
} patty_ax25_if_driver;

typedef void patty_ax25_if_phy;

typedef void patty_ax25_if_info;

typedef struct _patty_ax25_if {
    uint32_t flags_classes;

    void *rx_buf,
         *tx_buf;

    size_t mru,
           mtu;

    enum patty_ax25_if_status status;

    patty_ax25_addr addr;
    patty_list *aliases;
    patty_dict *promisc_fds;

    patty_ax25_if_driver *driver;
    patty_ax25_if_phy *phy;
} patty_ax25_if;

patty_ax25_if *patty_ax25_if_new(patty_ax25_if_driver *driver,
                                 patty_ax25_if_info *info);

void patty_ax25_if_destroy(patty_ax25_if *iface);

void patty_ax25_if_up(patty_ax25_if *iface);

void patty_ax25_if_down(patty_ax25_if *iface);

void patty_ax25_if_error(patty_ax25_if *iface);

int patty_ax25_if_addr_set(patty_ax25_if *iface,
                           patty_ax25_addr *addr);

int patty_ax25_if_addr_each(patty_ax25_if *iface,
                            int (*callback)(patty_ax25_addr *, void *),
                            void *ctx);

int patty_ax25_if_addr_add(patty_ax25_if *iface, patty_ax25_addr *addr);

int patty_ax25_if_addr_delete(patty_ax25_if *iface, patty_ax25_addr *addr);

int patty_ax25_if_addr_match(patty_ax25_if *iface, patty_ax25_addr *addr);

int patty_ax25_if_promisc_add(patty_ax25_if *iface,
                              int fd);

int patty_ax25_if_promisc_delete(patty_ax25_if *iface,
                                 int fd);

void patty_ax25_if_drop(patty_ax25_if *iface);

int patty_ax25_if_fd(patty_ax25_if *iface);

int patty_ax25_if_ready(patty_ax25_if *iface, fd_set *fds);

int patty_ax25_if_reset(patty_ax25_if *iface);

ssize_t patty_ax25_if_fill(patty_ax25_if *iface);

ssize_t patty_ax25_if_drain(patty_ax25_if *iface, void *buf, size_t len);

int patty_ax25_if_pending(patty_ax25_if *iface);

ssize_t patty_ax25_if_flush(patty_ax25_if *iface);

ssize_t patty_ax25_if_recv(patty_ax25_if *iface, void *buf, size_t len);

ssize_t patty_ax25_if_send(patty_ax25_if *iface, const void *buf, size_t len);

#endif /* _PATTY_AX25_IF_H */
