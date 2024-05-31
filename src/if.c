#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#include <patty/ax25.h>
#include <patty/kiss.h>

patty_ax25_if *patty_ax25_if_new(patty_ax25_if_driver *driver,
                                 patty_ax25_if_info *info) {
    patty_ax25_if *iface;

    if ((iface = malloc(sizeof(*iface))) == NULL) {
        goto error_malloc_iface;
    }

    memset(iface, '\0', sizeof(*iface));

    if ((iface->phy = driver->create(info)) == NULL) {
        goto error_phy_create;
    }

    iface->driver = driver;

    /*
     * TODO: Eventually inherit the half/full-duplex flag from the PHY this
     * interface is bound to
     */
    iface->flags_classes = PATTY_AX25_IF_DEFAULT_CLASSES;

    if ((iface->rx_buf = malloc(PATTY_AX25_IF_DEFAULT_MRU)) == NULL) {
        goto error_malloc_rx_buf;
    } else {
        iface->mru = PATTY_AX25_IF_DEFAULT_MRU;
    }

    if ((iface->tx_buf = malloc(PATTY_AX25_IF_DEFAULT_MTU)) == NULL) {
        goto error_malloc_tx_buf;
    } else {
        iface->mtu = PATTY_AX25_IF_DEFAULT_MTU;
    }

    if ((iface->aliases = patty_list_new()) == NULL) {
        goto error_list_new_aliases;
    }

    if ((iface->promisc_fds = patty_dict_new()) == NULL) {
        goto error_dict_new_promisc_fds;
    }

    iface->status = PATTY_AX25_IF_DOWN;

    return iface;

error_dict_new_promisc_fds:
    patty_list_destroy(iface->aliases);

error_list_new_aliases:
    free(iface->tx_buf);

error_malloc_tx_buf:
    free(iface->rx_buf);

error_malloc_rx_buf:
    driver->destroy(iface->phy);

error_phy_create:
    free(iface);

error_malloc_iface:
    return NULL;
}

int patty_ax25_if_addr_set(patty_ax25_if *iface,
                           patty_ax25_addr *addr) {
    return patty_ax25_addr_copy(&iface->addr, addr, 0);
}

void patty_ax25_if_destroy(patty_ax25_if *iface) {
    if (iface->driver->destroy) {
        iface->driver->destroy(iface->phy);
    }

    patty_dict_destroy(iface->promisc_fds);
    patty_list_destroy(iface->aliases);

    free(iface->tx_buf);
    free(iface->rx_buf);
    free(iface);
}

void patty_ax25_if_up(patty_ax25_if *iface) {
    iface->status = PATTY_AX25_IF_UP;
}

void patty_ax25_if_down(patty_ax25_if *iface) {
    iface->status = PATTY_AX25_IF_DOWN;
}

void patty_ax25_if_error(patty_ax25_if *iface) {
    iface->status = PATTY_AX25_IF_ERROR;
}

int patty_ax25_if_addr_each(patty_ax25_if *iface,
                            int (*callback)(patty_ax25_addr *, void *),
                            void *ctx) {
    patty_list_item *item = iface->aliases->first;

    if (callback(&iface->addr, ctx) < 0) {
        goto error_callback;
    }

    while (item) {
        patty_ax25_addr *addr = item->value;

        if (callback(addr, ctx) < 0) {
            goto error_callback;
        }

        item = item->next;
    }

    return 0;

error_callback:
    return -1;
}

static patty_ax25_addr *find_addr(patty_ax25_if *iface,
                                  patty_ax25_addr *addr) {
    patty_list_item *item = iface->aliases->first;

    while (item) {
        patty_ax25_addr *cur = item->value;

        if (memcmp(&addr->callsign,
                   &cur->callsign,
                   sizeof(addr->callsign)) == 0) {
            return addr;
        }

        item = item->next;
    }

    return NULL;
}

int patty_ax25_if_addr_add(patty_ax25_if *iface,
                           patty_ax25_addr *addr) {
    patty_ax25_addr *alias;

    if (find_addr(iface, addr) != NULL) {
        errno = EADDRINUSE;

        goto error_exists;
    }

    if ((alias = malloc(sizeof(*alias))) == NULL) {
        goto error_malloc_alias;
    }

    memcpy(&alias->callsign, &addr->callsign, sizeof(alias->callsign));

    addr->ssid = 0;

    if ((patty_list_append(iface->aliases, alias)) == NULL) {
        goto error_list_append;
    }

    return 0;

error_list_append:
    free(alias);

error_malloc_alias:
error_exists:
    return -1;
}

int patty_ax25_if_addr_delete(patty_ax25_if *iface, patty_ax25_addr *addr) {
    patty_list_item *item = iface->aliases->first;
    int i = 0;

    while (item) {
        patty_ax25_addr *alias = item->value;

        if (memcmp(&addr->callsign,
                   &alias->callsign,
                   sizeof(addr->callsign)) == 0) {
            if (patty_list_splice(iface->aliases, i) == NULL) {
                goto error_list_splice;
            }
        }

        item = item->next;
        i++;
    }

    return 0;

error_list_splice:
    return -1;
}

int patty_ax25_if_addr_match(patty_ax25_if *iface,
                             patty_ax25_addr *addr) {
    patty_list_item *item;

    if (memcmp(&iface->addr.callsign,
               &addr->callsign,
               sizeof(addr->callsign)) == 0) {
        return 1;
    }

    item = iface->aliases->first;

    while (item) {
        patty_ax25_addr *alias = item->value;

        if (memcmp(&alias->callsign,
                   &addr->callsign,
                   sizeof(addr->callsign)) == 0) {
            return 1;
        }

        item = item->next;
    }

    return 0;
}

int patty_ax25_if_promisc_add(patty_ax25_if *iface,
                              int fd) {
    if (patty_dict_get(iface->promisc_fds, (uint32_t)fd)) {
        errno = EEXIST;

        goto error_exists;
    }

    if (patty_dict_set(iface->promisc_fds,
                       (uint32_t)fd,
                       NULL) == NULL) {
        errno = ENOMEM;

        goto error_dict_set;
    }

    return 0;

error_dict_set:
error_exists:
    return -1;
}

int patty_ax25_if_promisc_delete(patty_ax25_if *iface,
                                 int fd) {
    return patty_dict_delete(iface->promisc_fds, (uint32_t)fd);
}

struct promisc_frame {
    const void *buf;
    size_t len;
    patty_ax25_if *iface;
};

static int handle_promisc_frame(uint32_t key,
                                void *value,
                                void *ctx) {
    int fd = (int)key;
    struct promisc_frame *frame = ctx;

    return patty_kiss_frame_send(fd, frame->buf, frame->len, 0);
}

void patty_ax25_if_drop(patty_ax25_if *iface) {
    iface->driver->stats(iface->phy)->dropped++;
}

int patty_ax25_if_fd(patty_ax25_if *iface) {
    return iface->driver->fd(iface->phy);
}

int patty_ax25_if_ready(patty_ax25_if *iface, fd_set *fds) {
    return iface->status == PATTY_AX25_IF_UP?
           iface->driver->ready(iface->phy, fds): 0;
}

int patty_ax25_if_reset(patty_ax25_if *iface) {
    int ret;

    if ((ret = iface->driver->reset(iface->phy)) < 0) {
        iface->status = PATTY_AX25_IF_ERROR;
    } else {
        iface->status = PATTY_AX25_IF_UP;
    }

    return ret;
}

ssize_t patty_ax25_if_fill(patty_ax25_if *iface) {
    return iface->driver->fill(iface->phy);
}

ssize_t patty_ax25_if_drain(patty_ax25_if *iface, void *buf, size_t len) {
    return iface->driver->drain(iface->phy, buf, len);
}

int patty_ax25_if_pending(patty_ax25_if *iface) {
    return iface->driver->pending(iface->phy);
}

ssize_t patty_ax25_if_flush(patty_ax25_if *iface) {
    ssize_t len = iface->driver->flush(iface->phy);

    if (len > 0) {
        patty_ax25_if_stats *stats = iface->driver->stats(iface->phy);

        struct promisc_frame frame = {
            .buf   = iface->rx_buf,
            .len   = len,
            .iface = iface
        };

        stats->rx_frames++;
        stats->rx_bytes += len;

        if (patty_dict_each(iface->promisc_fds,
                            handle_promisc_frame,
                            &frame) < 0) {
            goto error_handle_promisc_frame;
        }
    }

    return len;

error_handle_promisc_frame:
    return -1;
}

ssize_t patty_ax25_if_recv(patty_ax25_if *iface, void *buf, size_t len) {
    while (!patty_ax25_if_pending(iface)) {
        ssize_t drained;

        if ((drained = patty_ax25_if_drain(iface, buf, len)) < 0) {
            goto error_drain;
        } else if (drained == 0) {
            ssize_t filled;

            if ((filled = patty_ax25_if_fill(iface)) < 0) {
                goto error_fill;
            } else if (filled == 0) {
                return 0;
            }
        }
    }

    return patty_ax25_if_flush(iface);

error_drain:
error_fill:
    return -1;
}

ssize_t patty_ax25_if_send(patty_ax25_if *iface, const void *buf, size_t len) {
    struct promisc_frame frame;
    patty_ax25_if_stats *stats;

    ssize_t wrlen;

    if ((wrlen = iface->driver->send(iface->phy, buf, len)) < 0) {
        goto error_driver_send;
    }

    stats = iface->driver->stats(iface->phy);

    stats->tx_frames++;
    stats->tx_bytes += wrlen;

    frame.buf   = buf;
    frame.len   = wrlen;
    frame.iface = iface;

    if (patty_dict_each(iface->promisc_fds,
                        handle_promisc_frame,
                        &frame) < 0) {
        goto error_handle_promisc_frame;
    }

    return wrlen;

error_handle_promisc_frame:
error_driver_send:
    return -1;
}
