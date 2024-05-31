#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include <patty/ax25.h>
#include <patty/kiss/tnc.h>
#include <patty/hash.h>
#include <patty/util.h>

typedef int (*patty_ax25_server_call)(patty_ax25_server *, int);

typedef struct if_entry {
    int fd;
    char name[10];
    patty_ax25_if *iface;
} if_entry;

struct _patty_ax25_server {
    int fd,     /* fd of UNIX domain socket */
        fd_max;

    struct timespec elapsed;

    fd_set fds_watch, /* fds to monitor with select() */
           fds_r;     /* fds select()ed for reading */

    patty_list *ifaces;
    patty_ax25_route_table *routes;

    patty_dict *socks_by_fd,
               *socks_by_client,
               *socks_local,
               *socks_remote;

    patty_dict *clients,
               *clients_by_sock;
};

patty_ax25_server *patty_ax25_server_new() {
    patty_ax25_server *server;

    if ((server = malloc(sizeof(*server))) == NULL) {
        goto error_malloc_server;
    }

    memset(server, '\0', sizeof(*server));

    if ((server->ifaces = patty_list_new()) == NULL) {
        goto error_list_new_ifaces;
    }

    if ((server->routes = patty_ax25_route_table_new()) == NULL) {
        goto error_route_table_new;
    }

    if ((server->socks_by_fd = patty_dict_new()) == NULL) {
        goto error_dict_new_socks_by_fd;
    }

    if ((server->socks_by_client = patty_dict_new()) == NULL) {
        goto error_dict_new_socks_by_client;
    }

    if ((server->socks_local = patty_dict_new()) == NULL) {
        goto error_dict_new_socks_local;
    }

    if ((server->socks_remote = patty_dict_new()) == NULL) {
        goto error_dict_new_socks_remote;
    }

    if ((server->clients = patty_dict_new()) == NULL) {
        goto error_dict_new_clients;
    }

    if ((server->clients_by_sock = patty_dict_new()) == NULL) {
        goto error_dict_new_clients_by_sock;
    }

    return server;

error_dict_new_clients_by_sock:
    patty_dict_destroy(server->clients);

error_dict_new_clients:
    patty_dict_destroy(server->socks_remote);

error_dict_new_socks_remote:
    patty_dict_destroy(server->socks_local);

error_dict_new_socks_local:
    patty_dict_destroy(server->socks_by_client);

error_dict_new_socks_by_client:
    patty_dict_destroy(server->socks_by_fd);

error_dict_new_socks_by_fd:
    patty_ax25_route_table_destroy(server->routes);

error_route_table_new:
    patty_list_destroy(server->ifaces);

error_list_new_ifaces:
    free(server);

error_malloc_server:
    return NULL;
}

static void destroy_ifaces(patty_list *ifaces) {
    patty_list_item *item = ifaces->first;

    while (item) {
        patty_list_item *next  = item->next;
        struct if_entry *entry = item->value;

        patty_ax25_if_destroy(entry->iface);
        free(entry);
        free(item);

        item = next;
    }

    free(ifaces);
}

static int destroy_socks_by_client_entry(uint32_t key, void *value, void *ctx) {
    patty_dict_destroy((patty_dict *)value);

    return 0;
}

void patty_ax25_server_destroy(patty_ax25_server *server) {
    patty_dict_destroy(server->clients_by_sock);
    patty_dict_destroy(server->clients);
    patty_dict_destroy(server->socks_remote);
    patty_dict_destroy(server->socks_local);
    patty_dict_each(server->socks_by_client, destroy_socks_by_client_entry, NULL);
    patty_dict_destroy(server->socks_by_client);
    patty_dict_destroy(server->socks_by_fd);

    patty_ax25_route_table_destroy(server->routes);
    destroy_ifaces(server->ifaces);

    free(server);
}

static inline void fd_watch(patty_ax25_server *server, int fd) {
    FD_SET(fd, &server->fds_watch);

    if (server->fd_max <= fd) {
        server->fd_max  = fd + 1;
    }
}

static inline void fd_clear(patty_ax25_server *server, int fd) {
    int i;

    FD_CLR(fd, &server->fds_watch);

    if (server->fd_max == fd + 1) {
        for (i=0; i<8*sizeof(&server->fds_watch); i++) {
            if (FD_ISSET(i, &server->fds_watch)) {
                server->fd_max = i;
            }
        }

        server->fd_max++;
    }
}

static patty_ax25_sock *sock_by_fd(patty_dict *dict,
                                   int fd) {
    return patty_dict_get(dict, (uint32_t)fd);
}

static patty_ax25_sock *sock_by_addr(patty_dict *dict,
                                     patty_ax25_addr *addr) {
    uint32_t hash;

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, addr);
    patty_hash_end(&hash);

    return patty_dict_get(dict, hash);
}

static patty_ax25_sock *sock_by_addrpair(patty_dict *dict,
                                         patty_ax25_addr *local,
                                         patty_ax25_addr *remote) {
    uint32_t hash;

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, local);
    patty_ax25_addr_hash(&hash, remote);
    patty_hash_end(&hash);

    return patty_dict_get(dict, hash);
}

static int sock_save_by_fd(patty_dict *dict, patty_ax25_sock *sock) {
    if (patty_dict_set(dict, (uint32_t)sock->fd, sock) == NULL) {
        goto error_dict_set;
    }

    return sock->fd;

error_dict_set:
    return -1;
}

static inline int client_by_sock(patty_ax25_server *server,
                                 patty_ax25_sock *sock) {
    void *value;

    if ((value = patty_dict_get(server->clients_by_sock,
                                (uint32_t)sock->fd)) == NULL) {
        goto error_dict_get;
    }

    return (int)((intptr_t)value);

error_dict_get:
    return -1;
}

static inline int client_save_by_sock(patty_ax25_server *server,
                                      int client,
                                      patty_ax25_sock *sock) {
    if (patty_dict_set(server->clients_by_sock,
                       (uint32_t)sock->fd,
                       NULL + client) == NULL) {
        goto error_dict_set;
    }

    return 0;

error_dict_set:
    return -1;
}

static inline int client_delete_by_sock(patty_ax25_server *server,
                                        patty_ax25_sock *sock) {
    return patty_dict_delete(server->clients_by_sock,
                             (uint32_t)sock->fd);
}

static inline uint32_t hash_addr(patty_ax25_addr *addr) {
    uint32_t hash;

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, addr);
    patty_hash_end(&hash);

    return hash;
}

static inline uint32_t hash_addrpair(patty_ax25_addr *local,
                                     patty_ax25_addr *remote) {
    uint32_t hash;

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, local);
    patty_ax25_addr_hash(&hash, remote);
    patty_hash_end(&hash);

    return hash;
}

static int sock_save_local(patty_ax25_server *server,
                           patty_ax25_sock *sock) {
    uint32_t hash = hash_addr(&sock->local);

    return patty_dict_set(server->socks_local, hash, sock) == NULL?  -1: 0;
}

static int sock_save_remote(patty_ax25_server *server,
                            patty_ax25_sock *sock) {
    uint32_t hash = hash_addrpair(&sock->local, &sock->remote);

    return patty_dict_set(server->socks_remote, hash, sock) == NULL? -1: 0;
}

static int sock_delete_local(patty_ax25_server *server,
                             patty_ax25_sock *sock) {
    uint32_t hash = hash_addr(&sock->local);

    return patty_dict_delete(server->socks_local, hash);
}

static int sock_delete_remote(patty_ax25_server *server,
                              patty_ax25_sock *sock) {
    uint32_t hash = hash_addrpair(&sock->local, &sock->remote);

    return patty_dict_delete(server->socks_remote, hash);
}

static int sock_shutdown(patty_ax25_server *server,
                         patty_ax25_sock *sock) {
    fd_clear(server, sock->fd);

    if (sock->type != PATTY_AX25_SOCK_STREAM) {
        return 0;
    }

    if (sock->state != PATTY_AX25_SOCK_ESTABLISHED) {
        return 0;
    }

    sock->state   = PATTY_AX25_SOCK_PENDING_DISCONNECT;
    sock->retries = sock->n_retry;

    patty_timer_start(&sock->timer_t1);

    return patty_ax25_sock_send_disc(sock, PATTY_AX25_FRAME_POLL);
}

static int sock_save(patty_ax25_server *server,
                     int client,
                     patty_ax25_sock *sock) {
    patty_dict *socks;

    switch (sock->state) {
        case PATTY_AX25_SOCK_LISTENING:
            if (sock_save_local(server, sock) < 0) {
                goto error_sock_save_local;
            }

            break;

        case PATTY_AX25_SOCK_PENDING_ACCEPT:
        case PATTY_AX25_SOCK_PENDING_CONNECT:
        case PATTY_AX25_SOCK_ESTABLISHED:
            if (sock_save_remote(server, sock) < 0) {
                goto error_sock_save_remote;
            }

            break;

        case PATTY_AX25_SOCK_PENDING_DISCONNECT:
        case PATTY_AX25_SOCK_CLOSED:
        case PATTY_AX25_SOCK_PROMISC:
            break;
    }

    if (client_save_by_sock(server, client, sock) < 0) {
        goto error_client_save_by_sock;
    }

    if ((socks = patty_dict_get(server->socks_by_client, client)) == NULL) {
        goto error_dict_get_socks_by_client;
    }

    if (sock_save_by_fd(socks, sock) < 0) {
        goto error_sock_save_by_fd;
    }

    return sock_save_by_fd(server->socks_by_fd, sock);

error_sock_save_by_fd:
error_dict_get_socks_by_client:
error_client_save_by_sock:
error_sock_save_remote:
error_sock_save_local:
    return -1;
}

static int sock_close(patty_ax25_server *server,
                      patty_ax25_sock *sock) {
    int client;
    patty_dict *socks;

    switch (sock->state) {
        case PATTY_AX25_SOCK_LISTENING:
            if (sock_delete_local(server, sock) < 0) {
                goto error_sock_delete_local;
            }

            break;

        case PATTY_AX25_SOCK_PENDING_ACCEPT:
        case PATTY_AX25_SOCK_PENDING_CONNECT:
        case PATTY_AX25_SOCK_PENDING_DISCONNECT:
        case PATTY_AX25_SOCK_ESTABLISHED:
            if (sock_delete_remote(server, sock) < 0) {
                goto error_sock_delete_remote;
            }

            break;

        case PATTY_AX25_SOCK_CLOSED:
        case PATTY_AX25_SOCK_PROMISC:
            break;
    }

    if ((client = client_by_sock(server, sock)) < 0) {
        goto error_client_by_sock;
    }

    if ((socks = patty_dict_get(server->socks_by_client, client)) == NULL) {
        goto error_dict_get_socks_by_client;
    }

    if (patty_dict_delete(socks, (uint32_t)sock->fd) < 0) {
        goto error_dict_delete_by_fd_socks;
    }

    if (patty_dict_delete(server->socks_by_fd, (uint32_t)sock->fd) < 0) {
        goto error_dict_delete_by_fd_socks_by_fd;
    }

    (void)client_delete_by_sock(server, sock);

    fd_clear(server, sock->fd);

    patty_ax25_sock_destroy(sock);

    return 0;

error_dict_delete_by_fd_socks_by_fd:
error_dict_delete_by_fd_socks:
error_dict_get_socks_by_client:
error_client_by_sock:
error_sock_delete_remote:
error_sock_delete_local:
    return -1;
}

static inline void sock_flow_stop(patty_ax25_server *server,
                                  patty_ax25_sock *sock) {
    fd_clear(server, sock->fd);

    sock->flow = PATTY_AX25_SOCK_WAIT;
}

static inline void sock_flow_start(patty_ax25_server *server,
                                   patty_ax25_sock *sock) {
    fd_watch(server, sock->fd);
}

int patty_ax25_server_if_add(patty_ax25_server *server,
                             patty_ax25_if *iface,
                             const char *name) {
    struct if_entry *entry;

    if ((entry = malloc(sizeof(*entry))) == NULL) {
        goto error_malloc_entry;
    }

    if ((entry->fd = patty_ax25_if_fd(iface)) < 0) {
        goto error_if_fd;
    }

    patty_strlcpy(entry->name, name, sizeof(entry->name));

    entry->iface = iface;

    if (patty_list_append(server->ifaces, entry) == NULL) {
        goto error_list_append;
    }

    fd_watch(server, entry->fd);

    return 0;

error_list_append:
error_if_fd:
    free(entry);

error_malloc_entry:
    return -1;
}

int patty_ax25_server_if_delete(patty_ax25_server *server,
                                const char *ifname) {
    patty_list_item *item = server->ifaces->first;

    int i = 0;

    while (item) {
        struct if_entry *entry = item->value;

        if (strncmp(entry->name, ifname, sizeof(entry->name)) == 0) {
            fd_clear(server, entry->fd);

            if (patty_list_splice(server->ifaces, i) == NULL) {
                goto error_list_splice;
            }

            free(entry);

            return 0;
        }

        item = item->next;
        i++;
    }

    return 0;

error_list_splice:
    return -1;
}

patty_ax25_if *patty_ax25_server_if_get(patty_ax25_server *server,
                                        const char *name) {
    patty_list_item *item = server->ifaces->first;

    while (item) {
        struct if_entry *entry = item->value;

        if (strncmp(entry->name, name, sizeof(entry->name)) == 0) {
            return entry->iface;
        }

        item = item->next;
    }

    return NULL;
}

int patty_ax25_server_if_each(patty_ax25_server *server,
                              int (*callback)(char *, patty_ax25_if *, void *),
                              void *ctx) {
    patty_list_item *item = server->ifaces->first;

    while (item) {
        struct if_entry *entry = item->value;

        if (callback(entry->name, entry->iface, ctx) < 0) {
            goto error_callback;
        }

        item = item->next;
    }

    return 0;

error_callback:
    return -1;
}

int patty_ax25_server_route_add(patty_ax25_server *server,
                                patty_ax25_route *route) {
    return patty_ax25_route_table_add(server->routes, route);
}

int patty_ax25_server_route_delete(patty_ax25_server *server,
                                   patty_ax25_addr *dest) {
    return patty_ax25_route_table_delete(server->routes, dest);
}

patty_ax25_route *patty_ax25_server_route_find(patty_ax25_server *server,
                                               patty_ax25_addr *dest) {
    return patty_ax25_route_table_find(server->routes, dest);
}

patty_ax25_route *patty_ax25_server_route_default(patty_ax25_server *server) {
    return patty_ax25_route_table_default(server->routes);
}

int patty_ax25_server_route_each(patty_ax25_server *server,
                                 int (*callback)(patty_ax25_route *, void *),
                                 void *ctx) {
    return patty_ax25_route_table_each(server->routes, callback, ctx);
}

static int respond_accept(int client,
                          int ret,
                          int eno) {
    patty_client_accept_response response = {
        .ret = ret,
        .eno = eno
    };

    return write(client, &response, sizeof(response));
}

static int notify_accept(int local,
                         int remote,
                         patty_ax25_addr *peer,
                         char *path) {
    patty_client_accept_message message;

    memset(&message, '\0', sizeof(message));

    message.fd = remote;

    if (peer) {
        memcpy(&message.peer, peer, sizeof(message.peer));
    }

    if (path) {
        patty_strlcpy(message.path, path, sizeof(message.path));
    }

    return write(local, &message, sizeof(message));
}

static int respond_connect(int client, int ret, int eno) {
    patty_client_connect_response response = {
        .ret = ret,
        .eno = eno
    };

    return write(client, &response, sizeof(response));
}

static int server_ping(patty_ax25_server *server, int client) {
    int pong = 1;

    return write(client, &pong, sizeof(pong));
}

static int server_socket(patty_ax25_server *server, int client) {
    patty_client_socket_request request;
    patty_client_socket_response response;

    patty_ax25_sock *sock;

    if (read(client, &request, sizeof(request)) < 0) {
        goto error_read;
    }

    if ((sock = patty_ax25_sock_new(request.proto, request.type)) == NULL) {
        goto error_sock_new;
    }

    if (sock_save(server, client, sock) < 0) {
        goto error_sock_save;
    }

    response.fd  = sock->fd;
    response.eno = 0;

    memcpy(response.path, patty_ax25_sock_pty(sock), sizeof(response.path));

    return write(client, &response, sizeof(response));

error_sock_save:
    patty_ax25_sock_destroy(sock);

error_sock_new:
error_read:
    return -1;
}

static int server_setsockopt(patty_ax25_server *server,
                             int client) {
    patty_client_setsockopt_request request;
    patty_client_setsockopt_response response;

    patty_ax25_sock *sock;

    if (read(client, &request, sizeof(request)) < 0) {
        goto error_read;
    }

    if ((sock = sock_by_fd(server->socks_by_fd, request.fd)) == NULL) {
        response.ret = -1;
        response.eno = EBADF;

        goto error_sock_by_fd;
    }

    switch (request.opt) {
        case PATTY_AX25_SOCK_PARAMS: {
            patty_client_setsockopt_params data;

            if (read(client, &data, request.len) < 0) {
                goto error_read;
            }

            if (data.flags & PATTY_AX25_SOCK_PARAM_MTU)
                patty_ax25_sock_mtu_set(sock, data.mtu);

            if (data.flags & PATTY_AX25_SOCK_PARAM_WINDOW)
                patty_ax25_sock_window_set(sock, data.window);

            if (data.flags & PATTY_AX25_SOCK_PARAM_ACK)
                patty_ax25_sock_ack_set(sock, data.ack);

            if (data.flags & PATTY_AX25_SOCK_PARAM_RETRY)
                patty_ax25_sock_retry_set(sock, data.retry);

            if (data.flags & (PATTY_AX25_SOCK_PARAM_MTU
                            | PATTY_AX25_SOCK_PARAM_WINDOW)) {
                if (patty_ax25_sock_realloc_bufs(sock) < 0) {
                    goto error_realloc_bufs;
                }
            }

            break;
        }

        case PATTY_AX25_SOCK_IF: {
            patty_client_setsockopt_if data;
            patty_ax25_if *iface;

            if (sock->type != PATTY_AX25_SOCK_RAW) {
                response.ret = -1;
                response.eno = EINVAL;

                goto error_invalid_type;
            }

            if (read(client, &data, request.len) < 0) {
                goto error_read;
            }

            if ((iface = patty_ax25_server_if_get(server, data.name)) == NULL) {
                response.ret = -1;
                response.eno = ENODEV;

                goto error_get_if;
            }

            if (data.state == PATTY_AX25_SOCK_PROMISC) {
                sock->state = PATTY_AX25_SOCK_PROMISC;
            }

            patty_ax25_sock_bind_if(sock, iface);

            fd_watch(server, sock->fd);

            break;
        }

        default:
            response.ret = -1;
            response.eno = EINVAL;

            goto error_invalid_opt;
    }

    response.ret = 0;
    response.eno = 0;

error_sock_by_fd:
error_get_if:
error_invalid_type:
error_invalid_opt:
    return write(client, &response, sizeof(response));

error_realloc_bufs:
error_read:
    return -1;
}

static int server_bind(patty_ax25_server *server,
                       int client) {
    patty_client_bind_request request;
    patty_client_bind_response response;

    patty_ax25_sock *sock;

    if (read(client, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if ((sock = sock_by_fd(server->socks_by_fd, request.fd)) == NULL) {
        response.ret = -1;
        response.eno = EBADF;

        goto error_sock_by_fd;
    }

    if (sock->local.callsign[0] != '\0') {
        response.ret = -1;
        response.eno = EINVAL;

        goto error_bound;
    }

    if (sock_by_addr(server->socks_local, &request.addr) != NULL) {
        response.ret = -1;
        response.eno = EADDRINUSE;

        goto error_exists;
    }

    memcpy(&sock->local, &request.addr, sizeof(request.addr));

    response.ret = 0;
    response.eno = 0;

error_exists:
error_bound:
error_sock_by_fd:
    return write(client, &response, sizeof(response));

error_io:
    return -1;
}

static int server_listen(patty_ax25_server *server,
                         int client) {
    patty_client_listen_request request;
    patty_client_listen_response response;

    patty_ax25_sock *sock;

    if (read(client, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if ((sock = sock_by_fd(server->socks_by_fd, request.fd)) == NULL) {
        response.ret = -1;
        response.eno = EBADF;

        goto error_sock_by_fd;
    }

    if (sock->local.callsign[0] == '\0') {
        response.eno = EINVAL;

        goto error_invalid_fd;
    }

    sock->state = PATTY_AX25_SOCK_LISTENING;

    if (sock_save_local(server, sock) < 0) {
        goto error_sock_save_local;
    }

    response.ret = 0;
    response.eno = 0;

error_invalid_fd:
error_sock_by_fd:
    return write(client, &response, sizeof(response));

error_sock_save_local:
error_io:
    return -1;
}

static int server_accept(patty_ax25_server *server,
                         int client) {
    patty_client_accept_request request;
    patty_ax25_sock *sock;

    if (read(client, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if ((sock = sock_by_fd(server->socks_by_fd, request.fd)) == NULL) {
        return respond_accept(client, -1, EBADF);
    }

    if (sock->type != PATTY_AX25_SOCK_STREAM) {
        return respond_accept(client, -1, EOPNOTSUPP);
    }

    if (sock->state != PATTY_AX25_SOCK_LISTENING) {
        return respond_accept(client, -1, EINVAL);
    }

    return respond_accept(client, 0, 0);

error_io:
    return -1;
}

static int server_connect(patty_ax25_server *server,
                          int client) {
    patty_client_connect_request request;

    patty_ax25_sock *sock;
    patty_ax25_route *route;
    patty_ax25_if *iface;

    if (read(client, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if ((sock = sock_by_fd(server->socks_by_fd, request.fd)) == NULL) {
        return respond_connect(client, -1, EBADF);
    }

    if (sock->type == PATTY_AX25_SOCK_RAW) {
        return respond_connect(client, -1, ENOTSUP);
    }

    switch (sock->state) {
        case PATTY_AX25_SOCK_LISTENING:
            return respond_connect(client, -1, EINVAL);

        case PATTY_AX25_SOCK_ESTABLISHED:
            return respond_connect(client, -1, EISCONN);

        default:
            break;
    }

    if (sock->local.callsign[0]) {
        /*
         * If there is an address already bound to this socket, locate the
         * appropriate route.
         */
        route = patty_ax25_route_table_find(server->routes, &sock->local);
    } else {
        /*
         * Otherwise, locate the default route.
         */
        route = patty_ax25_route_table_default(server->routes);
    }

    /*
     * If no route could be found, then assume the network is down or not
     * configured.
     */
    if (route == NULL) {
        return respond_connect(client, -1, ENETDOWN);
    }

    patty_ax25_sock_bind_if(sock, iface = route->iface);

    /*
     * If there is no local address bound to this sock, then bind the
     * address of the default route interface.
     */
    if (sock->local.callsign[0] == '\0') {
        memcpy(&sock->local, &iface->addr, sizeof(sock->local));
    }

    /*
     * Bind the requested remote address to the socket.
     */
    memcpy(&sock->remote, &request.peer, sizeof(request.peer));

    if (sock_save_remote(server, sock) < 0) {
        goto error_sock_save_remote;
    }

    if (client_save_by_sock(server, client, sock) < 0) {
        goto error_client_save_by_sock;
    }

    switch (sock->type) {
        case PATTY_AX25_SOCK_DGRAM:
            sock->state = PATTY_AX25_SOCK_ESTABLISHED;

            break;

        case PATTY_AX25_SOCK_STREAM:
            sock->state = PATTY_AX25_SOCK_PENDING_CONNECT;

            /*
             * Send an XID frame, to attempt to negotiate AX.25 v2.2 and its
             * default parameters.
             */
            if (patty_ax25_sock_send_xid(sock, PATTY_AX25_FRAME_COMMAND) < 0) {
                return respond_connect(client, -1, errno);
            }

            /*
             * At this point, we will wait for a DM, FRMR or XID response, which
             * will help us determine what version of AX.25 to apply for this
             * socket, or whether the peer is not accepting connections.
             */
            sock->retries = sock->n_retry;

            patty_timer_start(&sock->timer_t1);

        default:
            break;
    }

    return 0;

error_client_save_by_sock:
error_sock_save_remote:
error_io:
    return -1;
}

static int server_close(patty_ax25_server *server,
                        int client) {
    patty_client_close_request request;
    patty_client_close_response response;

    patty_ax25_sock *sock;
    patty_dict *socks;

    if (read(client, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if ((sock = sock_by_fd(server->socks_by_fd, request.fd)) == NULL) {
        response.ret = -1;
        response.eno = EBADF;

        goto error_sock_by_fd;
    }

    if ((socks = patty_dict_get(server->socks_by_client, client)) == NULL) {
        response.ret = -1;
        response.eno = EBADF;

        goto error_dict_get_socks_by_client;
    }

    if (sock_shutdown(server, sock) < 0) {
        response.ret = -1;
        response.eno = errno;

        goto error_sock_shutdown;
    }

    if (sock_close(server, sock) < 0) {
        response.ret = -1;
        response.eno = EBADF;

        goto error_sock_close;
    }

    response.ret = 0;
    response.eno = 0;

error_sock_close:
error_sock_shutdown:
error_dict_get_socks_by_client:
error_sock_by_fd:
    return write(client, &response, sizeof(response));

error_io:
    return -1;
}

static patty_ax25_server_call server_calls[PATTY_CLIENT_CALL_COUNT] = {
    NULL,
    server_ping,
    server_socket,
    server_setsockopt,
    server_bind,
    server_listen,
    server_accept,
    server_connect,
    server_close,
    NULL,
    NULL
};

static int listen_unix(patty_ax25_server *server, const char *path) {
    struct sockaddr_un addr;
    struct stat st;

    if (server->fd) {
        errno = EBUSY;

        goto error_listening;
    }

    if (stat(path, &st) >= 0) {
        unlink(path);
    }

    if ((server->fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
        goto error_socket;
    }

    memset(&addr, '\0', sizeof(addr));
    addr.sun_family = AF_UNIX;
    patty_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if (bind(server->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        goto error_bind;
    }

    if (listen(server->fd, 0) < 0) {
        goto error_listen;
    }

    fd_watch(server, server->fd);

    return 0;

error_listen:
error_bind:
    close(server->fd);

error_socket:
error_listening:
    return -1;
}

static int accept_client(patty_ax25_server *server) {
    int fd;
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    patty_dict *socks;

    memset(&addr, '\0', addrlen);

    if (!FD_ISSET(server->fd, &server->fds_r)) {
        goto done;
    }

    if ((fd = accept(server->fd, &addr, &addrlen)) < 0) {
        goto error_accept;
    }

    if ((socks = patty_dict_new()) == NULL) {
        goto error_dict_new;
    }

    if (patty_dict_set(server->clients, (uint32_t)fd, NULL + fd) == NULL) {
        goto error_dict_set_clients;
    }

    if (patty_dict_set(server->socks_by_client, (uint32_t)fd, socks) == NULL) {
        goto error_dict_set_socks_by_client;
    }

    fd_watch(server, fd);

done:
    return 0;

error_dict_set_socks_by_client:
error_dict_set_clients:
    patty_dict_destroy(socks);

error_dict_new:
    close(fd);

error_accept:
    return -1;
}

static int client_sock_close(uint32_t key, void *value, void *ctx) {
    patty_ax25_server *server = ctx;
    patty_ax25_sock *sock = value;

    if (sock_shutdown(server, sock) < 0) {
        goto error_sock_shutdown;
    }

    return sock_close(server, sock);

error_sock_shutdown:
    return -1;
}

static int handle_client(uint32_t key,
                         void *value,
                         void *ctx) {
    patty_ax25_server *server = ctx;
    int client = (int)key;

    ssize_t readlen;
    enum patty_client_call call;

    if (!FD_ISSET(client, &server->fds_r)) {
        goto done;
    }

    if ((readlen = read(client, &call, sizeof(call))) < 0) {
        goto error_io;
    } else if (readlen == 0) {
        patty_dict *socks;

        fd_clear(server, client);

        if ((socks = patty_dict_get(server->socks_by_client, client)) != NULL) {
            (void)patty_dict_each(socks, client_sock_close, server);
            (void)patty_dict_destroy(socks);
        }

        if (patty_dict_delete(server->socks_by_client, key) < 0) {
            goto error_dict_delete_socks_by_client;
        }

        if (patty_dict_delete(server->clients, key) < 0) {
            goto error_dict_delete_clients;
        }

        if (close(client) < 0) {
            goto error_io;
        }

        goto done;
    }

    if (call <= PATTY_CLIENT_NONE || call >= PATTY_CLIENT_CALL_COUNT) {
        goto done;
    }

    if (server_calls[call] == NULL) {
        goto done;
    }

    return server_calls[call](server, client);

done:
    return 0;

error_dict_delete_socks_by_client:
error_dict_delete_clients:
error_io:
    return -1;
}

static int handle_clients(patty_ax25_server *server) {
    return patty_dict_each(server->clients, handle_client, server);
}

static void save_reply_addr(patty_ax25_sock *sock,
                            patty_ax25_frame *frame) {
    unsigned int i,
                 hops = frame->hops > PATTY_AX25_MAX_HOPS?
                                      PATTY_AX25_MAX_HOPS: sock->hops;

    memcpy(&sock->remote, &frame->src,  sizeof(patty_ax25_addr));
    memcpy(&sock->local,  &frame->dest, sizeof(patty_ax25_addr));

    for (i=0; i<hops; i++) {
        memcpy(&sock->repeaters[i],
               &frame->repeaters[hops-1-i],
               sizeof(patty_ax25_addr));
    }

    sock->hops = hops;
}

static int reply_to(patty_ax25_if *iface,
                    patty_ax25_frame *frame,
                    patty_ax25_frame *reply) {
    ssize_t len;

    if ((len = patty_ax25_frame_encode_reply_to(frame,
                                                reply,
                                                iface->tx_buf,
                                                iface->mtu)) < 0) {
        goto error_toobig;
    }

    return patty_ax25_if_send(iface, iface->tx_buf, len);

error_toobig:
    return -1;
}

static int reply_dm(patty_ax25_if *iface,
                    patty_ax25_frame *frame,
                    int flag) {
    patty_ax25_frame reply = {
        .control = PATTY_AX25_FRAME_DM | (flag << 4),
        .proto   = PATTY_AX25_PROTO_NONE,
        .info    = NULL,
        .infolen = 0
    };

    return reply_to(iface, frame, &reply);
}

static int reply_ua(patty_ax25_if *iface,
                    patty_ax25_frame *frame,
                    int flag) {
    patty_ax25_frame reply = {
        .control = PATTY_AX25_FRAME_UA | (flag << 4),
        .proto   = PATTY_AX25_PROTO_NONE,
        .info    = NULL,
        .infolen = 0
    };

    return reply_to(iface, frame, &reply);
}

static int reply_test(patty_ax25_if *iface,
                      patty_ax25_frame *frame) {
    patty_ax25_frame reply = {
        .control = PATTY_AX25_FRAME_UA | (PATTY_AX25_FRAME_FINAL << 4),
        .proto   = PATTY_AX25_PROTO_NONE,
        .info    = frame->info,
        .infolen = frame->infolen
    };

    return reply_to(iface, frame, &reply);
}

static int frame_ack(patty_ax25_server *server,
                     patty_ax25_sock *sock,
                     patty_ax25_frame *frame) {
    sock->rx_pending = 0;

    patty_timer_start(&sock->timer_t3);

    if (patty_ax25_sock_ack(sock, frame->nr) > 0) {
        /*
         * AX.25 v2.2, Section 6.4.6 "Receiving Acknowledgement"
         *
         * Whenever an I or S frame is correctly received, even in a busy
         * condition, the N(R) of the received frame is checked to see if it
         * includes an acknowledgement of outstanding sent I frames.  Timer
         * T1 is canceled if the received frame actually acknowledges
         * previously unacknowledged frames.  If Timer T1 is canceled and
         * there are still some frames that have been sent that are not
         * acknowledged, Timer T1 is started again.
         */
        patty_timer_stop(&sock->timer_t1);

        if (patty_ax25_sock_ack_pending(sock) > 0) {
            sock->retries = sock->n_retry;

            patty_timer_start(&sock->timer_t1);
        }
    }

    if (PATTY_AX25_FRAME_CONTROL_S(frame->control) && frame->pf) {
        /*
         * AX.25 v2.2, Section 6.4.11 "Waiting Acknowledgement"
         *
         * If the TNC correctly receives a supervisory response frame with the
         * F bit set and an N(R) within the range from the last N(R) received
         * to the last N(S) sent plus one, the TNC restarts Timer T1 and sets
         * its send state variable V(S) to the received N(R).  It may then
         * resume with I frame transmission or retransmission, as appropriate.
         *
         * Errata: It is not necessary, strictly speaking, to start Timer T1
         * until the moment there are I frames ready to be sent to the peer.
         */
        int min = sock->va,
            max = sock->vs;

        if (max < min) {
            max += sock->mode == PATTY_AX25_SOCK_SABME? 128: 8;
        }

        if (frame->nr >= min && frame->nr <= max) {
            sock->vs      = frame->nr;
            sock->retries = sock->n_retry;
        }
    }

    return 0;
}

static int handle_frmr(patty_ax25_server *server,
                       patty_ax25_if *iface,
                       patty_ax25_sock *sock,
                       patty_ax25_frame *frame) {
    if (sock == NULL) {
        return 0;
    }

    switch (sock->state) {
        case PATTY_AX25_SOCK_PENDING_CONNECT:
            if (sock->state == PATTY_AX25_SOCK_PENDING_CONNECT) {
                sock->retries = sock->n_retry;

                patty_timer_start(&sock->timer_t1);

                return patty_ax25_sock_send_sabm(sock, PATTY_AX25_FRAME_POLL);
            }

        case PATTY_AX25_SOCK_ESTABLISHED:
            patty_ax25_sock_reset(sock);

            return 0;

        default:
            break;
    }

    return 0;
}

static int handle_sabm(patty_ax25_server *server,
                       patty_ax25_if *iface,
                       patty_ax25_frame *frame) {
    int client,
        created = 0;

    patty_ax25_sock *local, *remote;

    if ((local = sock_by_addr(server->socks_local,
                              &frame->dest)) == NULL
     || local->type  != PATTY_AX25_SOCK_STREAM
     || local->state != PATTY_AX25_SOCK_LISTENING) {
        goto reply_dm;
    }

    if ((client = client_by_sock(server, local)) < 0) {
        goto error_client_by_sock;
    }

    /*
     * Look to see if there is already a remote socket created based on an XID
     * packet previously received.
     */
    if ((remote = sock_by_addrpair(server->socks_remote,
                                   &frame->dest,
                                   &frame->src)) == NULL) {
        /*
         * If there is no existing remote socket, we should create one, and
         * associate it with the client.
         */
        if ((remote = patty_ax25_sock_new(local->proto, local->type)) == NULL) {
            goto error_sock_new;
        }

        remote->state = PATTY_AX25_SOCK_PENDING_ACCEPT;

        save_reply_addr(remote, frame);

        created = 1;
    }

    switch (remote->state) {
        case PATTY_AX25_SOCK_PENDING_ACCEPT:
            break;

        case PATTY_AX25_SOCK_ESTABLISHED:
            patty_ax25_sock_reset(remote);

            return frame->pf == 1? reply_ua(iface, frame, 1): 0;

        default:
            goto reply_dm;
    }

    remote->state = PATTY_AX25_SOCK_ESTABLISHED;

    if (frame->type == PATTY_AX25_FRAME_SABME) {
        patty_ax25_sock_params_upgrade(remote);

        remote->mode = PATTY_AX25_SOCK_SABME;
   } else {
        remote->mode = PATTY_AX25_SOCK_SABM;
   }

    if (patty_ax25_sock_realloc_bufs(remote) < 0) {
        goto error_sock_realloc_bufs;
    }

    patty_ax25_sock_bind_if(remote, iface);

    if (created) {
        if (sock_save(server, client, remote) < 0) {
            goto error_sock_save;
        }
    }

    fd_watch(server, remote->fd);

    if (notify_accept(local->fd, remote->fd, &remote->remote, remote->pty) < 0) {
        goto error_notify_accept;
    }

    patty_timer_start(&remote->timer_t3);

    return reply_ua(iface, frame, PATTY_AX25_FRAME_FINAL);

reply_dm:
    return reply_dm(iface, frame, PATTY_AX25_FRAME_FINAL);

error_notify_accept:
error_sock_realloc_bufs:
error_sock_save:
    patty_ax25_sock_destroy(remote);

error_sock_new:
error_client_by_sock:
    return -1;
}

static int handle_test(patty_ax25_server *server,
                       patty_ax25_if *iface,
                       patty_ax25_frame *frame) {
    if (!patty_ax25_if_addr_match(iface, &frame->dest)) {
        return 0;
    }

    return reply_test(iface, frame);
}

static int handle_ua(patty_ax25_server *server,
                     patty_ax25_if *iface,
                     patty_ax25_sock *sock,
                     patty_ax25_frame *frame) {
    int client;

    if (!patty_ax25_if_addr_match(iface, &frame->dest)) {
        return 0;
    }

    if (sock == NULL) {
        return reply_dm(iface, frame, PATTY_AX25_FRAME_FINAL);
    }

    switch (sock->state) {
        case PATTY_AX25_SOCK_PENDING_CONNECT:
            break;

        case PATTY_AX25_SOCK_PENDING_DISCONNECT:
            return sock_close(server, sock);

        case PATTY_AX25_SOCK_ESTABLISHED:
            return 0;

        default:
            return reply_dm(iface, frame, PATTY_AX25_FRAME_FINAL);
    }

    if (patty_ax25_sock_realloc_bufs(sock) < 0) {
        goto error_sock_realloc_bufs;
    }

    patty_timer_stop(&sock->timer_t1);
    patty_timer_start(&sock->timer_t3);

    sock->state = PATTY_AX25_SOCK_ESTABLISHED;

    if ((client = client_by_sock(server, sock)) < 0) {
        goto error_client_by_sock;
    }

    if (sock_save(server, client, sock) < 0) {
        goto error_sock_save;
    }

    fd_watch(server, sock->fd);

    return respond_connect(client, 0, 0);

error_sock_save:
error_sock_realloc_bufs:
error_client_by_sock:
    return -1;
}

static int handle_dm(patty_ax25_server *server,
                     patty_ax25_if *iface,
                     patty_ax25_sock *sock,
                     patty_ax25_frame *frame) {
    if (sock == NULL) {
        return 0;
    }

    switch (sock->state) {
        case PATTY_AX25_SOCK_PENDING_CONNECT: {
            int client;

            if ((client = client_by_sock(server, sock)) < 0) {
                goto error_client_by_sock;
            }

            patty_ax25_sock_reset(sock);

            return respond_connect(client, -1, ECONNREFUSED);
        }

        case PATTY_AX25_SOCK_PENDING_DISCONNECT:
            patty_timer_stop(&sock->timer_t1);

            return sock_close(server, sock);

        default:
            break;
    }

    return 0;

error_client_by_sock:
    return -1;
}

static int handle_segment(patty_ax25_server *server,
                          patty_ax25_if *iface,
                          patty_ax25_sock *sock,
                          patty_ax25_frame *frame) {
    uint8_t *info = (uint8_t *)frame->info;

    uint8_t first,
            remaining;

    if (frame->infolen < 2) {
        goto reply_rej;
    }

    first     = info[0] & 0x80;
    remaining = info[0] & 0x7f;

    if (first) {
        if (remaining == 0) {
            goto reply_rej;
        }

        if (patty_ax25_sock_assembler_init(sock, remaining + 1) < 0) {
            goto error_sock_assembler_init;
        }
    } else if (remaining > 0) {
        if (frame->infolen != sock->n_maxlen_rx) {
            goto reply_rej;
        }
    } else if (remaining == 0) {
        if (frame->infolen > sock->n_maxlen_rx) {
            goto reply_rej;
        }
    }

    if (!patty_ax25_sock_assembler_pending(sock, remaining)) {
        patty_ax25_sock_assembler_stop(sock);

        goto reply_rej;
    }

    if (patty_ax25_sock_assembler_save(sock,
                                       info + 1,
                                       frame->infolen - 1) < 0) {
        goto error_sock_assembler_save;
    }

    if (remaining == 0) {
        size_t len;
        void *buf;

        if ((buf = patty_ax25_sock_assembler_read(sock, NULL, &len)) == NULL) {
            goto error_sock_assembler_read;
        }

        if (write(sock->fd, buf, len) < 0) {
            goto error_write;
        }

        patty_ax25_sock_assembler_stop(sock);
    }

    return 0;

reply_rej:
    return patty_ax25_sock_send_srej(sock, PATTY_AX25_FRAME_RESPONSE);

error_write:
error_sock_assembler_read:
error_sock_assembler_save:
error_sock_assembler_init:
    return -1;
}

static int handle_i(patty_ax25_server *server,
                    patty_ax25_if *iface,
                    patty_ax25_sock *sock,
                    patty_ax25_frame *frame) {
    if (sock == NULL || sock->state != PATTY_AX25_SOCK_ESTABLISHED) {
        return frame->pf? reply_dm(iface, frame, PATTY_AX25_FRAME_FINAL): 0;
    }

    frame_ack(server, sock, frame);

    if (frame->ns == sock->vr) {
        patty_ax25_sock_vr_incr(sock);
    } else if (frame->ns == sock->vr + 1) {
        return patty_ax25_sock_send_srej(sock, PATTY_AX25_FRAME_RESPONSE);
    } else if (frame->ns > sock->vr + 1) {
        return patty_ax25_sock_send_rej(sock, PATTY_AX25_FRAME_RESPONSE, 1);
    }

    if (frame->proto == PATTY_AX25_PROTO_FRAGMENT) {
        if (handle_segment(server, iface, sock, frame) < 0) {
            goto error_handle_segment;
        }
    } else {
        if (write(sock->fd, frame->info, frame->infolen) < 0) {
            goto error_write;
        }
    }

    if (frame->pf || ++sock->rx_pending == sock->n_window_rx / 2) {
        /*
         * AX.25 v2.2 Section 6.7.1.2 "Response Delay Timer T2"
         *
         * Timer T2, the Response Delay Timer, may optionally be implemented
         * by the TNC to specify a maximum amount of delay to be introduced
         * between the time an I frame is received and the time the resulting
         * response frame is sent.  This delay is introduced to allow a
         * receiving TNC to wait a short period of time to determine if more
         * than one frame is being sent to it.  If more frames are received,
         * the TNC can acknowledge them at once (up to seven),
         * rather than acknowledging each individual frame.  The use of Timer
         * T2 is not required; it is simply recommended to improve channel
         * efficiency.  Note that to achieve maximum throughput on full-duplex
         * channels, acknowledgements should not be delayed beyond k/2
         * frames.  The k parameter is defined in Section 6.8.2.3.
         */
        sock->rx_pending = 0;

        patty_timer_stop(&sock->timer_t2);

        /*
         * AX.25 v2.2 Section 6.2 "Poll/Final (P/F) Bit Procedures"
         */
        return frame->pf?
            patty_ax25_sock_send_rr(sock, PATTY_AX25_FRAME_RESPONSE, 1): 0;
    }

    patty_timer_start(&sock->timer_t2);
    patty_timer_start(&sock->timer_t3);

    return 0;

error_handle_segment:
error_write:
    return -1;
}

static int handle_ui(patty_ax25_server *server,
                     patty_ax25_if *iface,
                     patty_ax25_sock *sock,
                     patty_ax25_frame *frame) {
    if (sock == NULL || sock->type != PATTY_AX25_SOCK_DGRAM) {
        return 0;
    }

    if (frame->proto == PATTY_AX25_PROTO_FRAGMENT) {
        return handle_segment(server, iface, sock, frame);
    }

    return write(sock->fd, frame->info, frame->infolen);
}

static int handle_disc(patty_ax25_server *server,
                       patty_ax25_if *iface,
                       patty_ax25_sock *sock,
                       patty_ax25_frame *frame) {
    if (sock == NULL) {
        goto reply_dm;
    }

    switch (sock->state) {
        case PATTY_AX25_SOCK_ESTABLISHED:
            (void)sock_close(server, sock);

            return reply_ua(iface, frame, PATTY_AX25_FRAME_FINAL);

        case PATTY_AX25_SOCK_PENDING_DISCONNECT:
            return 0;

        default:
            break;
    }

reply_dm:
    return reply_dm(iface, frame, PATTY_AX25_FRAME_FINAL);
}

static int handle_rr(patty_ax25_server *server,
                     patty_ax25_sock *sock,
                     patty_ax25_frame *frame) {
    if (sock == NULL) {
        return 0;
    }

    frame_ack(server, sock, frame);

    switch (frame->cr) {
        case PATTY_AX25_FRAME_COMMAND:
            return frame->pf?
                patty_ax25_sock_send_rr(sock, PATTY_AX25_FRAME_RESPONSE, 1): 0;

        case PATTY_AX25_FRAME_RESPONSE:
            sock_flow_start(server, sock);

        default:
            break;
    }

    return 0;
}

static int handle_rnr(patty_ax25_server *server,
                      patty_ax25_sock *sock,
                      patty_ax25_frame *frame) {
    if (sock == NULL) {
        return 0;
    }

    frame_ack(server, sock, frame);

    switch (frame->cr) {
        case PATTY_AX25_FRAME_COMMAND:
            return frame->pf?
                patty_ax25_sock_send_rr(sock, PATTY_AX25_FRAME_RESPONSE, 1): 0;

        case PATTY_AX25_FRAME_RESPONSE:
            sock_flow_stop(server, sock);

        default:
            break;
    }

    return 0;
}

static int handle_rej(patty_ax25_server *server,
                      patty_ax25_sock *sock,
                      patty_ax25_frame *frame) {
    if (sock == NULL) {
        return 0;
    }

    frame_ack(server, sock, frame);

    switch (frame->cr) {
        case PATTY_AX25_FRAME_COMMAND:
            return frame->pf?
                patty_ax25_sock_send_rr(sock, PATTY_AX25_FRAME_RESPONSE, 1): 0;

        case PATTY_AX25_FRAME_RESPONSE:
            sock_flow_start(server, sock);

        default:
            break;
    }

    return 0;
}

static int handle_srej(patty_ax25_server *server,
                       patty_ax25_sock *sock,
                       patty_ax25_frame *frame) {
    if (sock == NULL) {
        return 0;
    }

    frame_ack(server, sock, frame);

    /*
     * TODO: Read the fine print of section 4.3.2.4
     */
    if (patty_ax25_sock_resend(sock, frame->nr) < 0) {
        goto error_sock_resend;
    }

    return 0;

error_sock_resend:
    return -1;
}

static int handle_xid(patty_ax25_server *server,
                      patty_ax25_if *iface,
                      patty_ax25_frame *frame,
                      void *buf,
                      size_t offset,
                      size_t len) {
    patty_ax25_params params;

    patty_ax25_sock *local,
                    *remote;

    int ret;

    if (!patty_ax25_if_addr_match(iface, &frame->dest)) {
        return 0;
    }

    if (patty_ax25_frame_decode_xid(&params, buf, offset, len) < 0) {
        goto error_io;
    }

    /*
     * First, check if this XID packet is a response to an XID used to initiate
     * an outbound connection.
     */
    if ((remote = sock_by_addrpair(server->socks_remote,
                                   &frame->dest,
                                   &frame->src)) != NULL) {
        if (remote->state != PATTY_AX25_SOCK_PENDING_CONNECT) {
            goto reply_dm;
        }

        /*
         * Since we've received an XID packet, we can assume that the remote
         * station is capable of speaking AX.25 v2.2.  Therefore, we should
         * upgrade the socket defaults accordingly, and negotiate downwards
         * as necessary.
         */
        if (patty_ax25_sock_params_negotiate(remote, &params) < 0) {
            int client;

            if ((client = client_by_sock(server, remote)) < 0) {
                goto error_client_by_sock;
            }

            patty_ax25_sock_init(remote);
            patty_ax25_sock_reset(remote);

            return respond_connect(client, -1, errno);
        }

        /*
         * Since this XID frame is for a socket that is awaiting outbound
         * connection, we can send an SABM or SABME packet, as necessary.
         */
        if (params.hdlc & PATTY_AX25_PARAM_HDLC_MODULO_128) {
            remote->mode = PATTY_AX25_SOCK_SABME;
        }

        ret = patty_ax25_sock_send_sabm(remote, PATTY_AX25_FRAME_POLL);

        patty_timer_start(&remote->timer_t1);

        remote->retries = remote->n_retry;

        return ret;
    }

    /*
     * Second, check if this XID packet is for a listening socket.
     */
    if ((local = sock_by_addr(server->socks_local,
                              &frame->dest)) != NULL) {
        int ret,
            client;

        if (local->state != PATTY_AX25_SOCK_LISTENING) {
            goto reply_dm;
        }

        if ((client = client_by_sock(server, local)) < 0) {
            goto error_client_by_sock;
        }

        if ((remote = patty_ax25_sock_new(local->proto, local->type)) == NULL) {
            goto error_sock_new;
        }

        remote->state = PATTY_AX25_SOCK_PENDING_ACCEPT;

        if (params.hdlc & PATTY_AX25_PARAM_HDLC_MODULO_128) {
            remote->mode = PATTY_AX25_SOCK_SABME;
        }

        patty_ax25_sock_bind_if(remote, iface);

        patty_ax25_sock_params_max(remote);

        if (patty_ax25_sock_params_negotiate(remote, &params) < 0) {
            goto error_sock_params_negotiate;
        }

        save_reply_addr(remote, frame);

        if (sock_save(server, client, remote) < 0) {
            goto error_sock_save;
        }

        ret = patty_ax25_sock_send_xid(remote, PATTY_AX25_FRAME_RESPONSE);

        patty_timer_start(&remote->timer_t1);

        remote->retries = remote->n_retry;

        return ret;
    }

reply_dm:
    return reply_dm(iface, frame, PATTY_AX25_FRAME_FINAL);

error_sock_save:
error_sock_params_negotiate:
    patty_ax25_sock_destroy(remote);

error_sock_new:
error_client_by_sock:
error_io:
    return -1;
}

static int handle_frame(patty_ax25_server *server,
                        patty_ax25_if *iface,
                        void *buf,
                        size_t len) {
    patty_ax25_frame frame;
    enum patty_ax25_frame_format format = PATTY_AX25_FRAME_NORMAL;

    ssize_t decoded,
            offset = 0;

    patty_ax25_sock *sock;

    if ((decoded = patty_ax25_frame_decode_address(&frame, buf, len)) < 0) {
        goto error_decode;
    } else {
        offset += decoded;
    }

    if ((sock = sock_by_addrpair(server->socks_remote,
                                 &frame.dest,
                                 &frame.src)) != NULL) {
        if (sock->mode == PATTY_AX25_SOCK_SABME) {
            format = PATTY_AX25_FRAME_EXTENDED;
        }
    }

    if ((decoded = patty_ax25_frame_decode_control(&frame, format, buf, offset, len)) < 0) {
        goto error_decode;
    } else {
        offset += decoded;
    }

    switch (frame.type) {
        case PATTY_AX25_FRAME_I:     return handle_i(server, iface, sock, &frame);
        case PATTY_AX25_FRAME_UI:    return handle_ui(server, iface, sock, &frame);
        case PATTY_AX25_FRAME_RR:    return handle_rr(server, sock, &frame);
        case PATTY_AX25_FRAME_RNR:   return handle_rnr(server, sock, &frame);
        case PATTY_AX25_FRAME_REJ:   return handle_rej(server, sock, &frame);
        case PATTY_AX25_FRAME_SREJ:  return handle_srej(server, sock, &frame);
        case PATTY_AX25_FRAME_XID:   return handle_xid(server, iface, &frame, buf, offset, len);
        case PATTY_AX25_FRAME_SABM:
        case PATTY_AX25_FRAME_SABME: return handle_sabm(server, iface, &frame);
        case PATTY_AX25_FRAME_TEST:  return handle_test(server, iface, &frame);
        case PATTY_AX25_FRAME_UA:    return handle_ua(server, iface, sock, &frame);
        case PATTY_AX25_FRAME_DM:    return handle_dm(server, iface, sock, &frame);
        case PATTY_AX25_FRAME_DISC:  return handle_disc(server, iface, sock, &frame);
        case PATTY_AX25_FRAME_FRMR:  return handle_frmr(server, iface, sock, &frame);
    }

    return 0;

error_decode:
    patty_ax25_if_drop(iface);

    return 0;
}

static int handle_iface(patty_ax25_server *server, struct if_entry *entry) {
    patty_ax25_if *iface = entry->iface;

    ssize_t len;

    if (!patty_ax25_if_ready(entry->iface, &server->fds_r)) {
        goto done;
    }

    if ((len = patty_ax25_if_fill(entry->iface)) < 0) {
        int fd;

        fd_clear(server, entry->fd);

        if ((fd = patty_ax25_if_reset(entry->iface)) < 0) {
            goto error_io;
        }

        fd_watch(server, entry->fd = fd);
    } else if (len == 0) {
        close(entry->fd);

        fd_clear(server, entry->fd);

        goto done;
    }

    while (1) {
        ssize_t len;

        if ((len = patty_ax25_if_drain(iface, iface->rx_buf, iface->mru)) < 0) {
            goto error_io;
        }

        if (!patty_ax25_if_pending(iface)) {
            break;
        }

        if ((len = patty_ax25_if_flush(iface)) < 0) {
            goto error_io;
        }

        if (handle_frame(server, iface, iface->rx_buf, len) < 0) {
            goto error_handle_frame;
        }
    }

done:
    return 0;

error_handle_frame:
error_io:
    return -1;
}

static int handle_ifaces(patty_ax25_server *server) {
    patty_list_item *item = server->ifaces->first;

    while (item) {
        struct if_entry *entry = item->value;

        if (handle_iface(server, entry) < 0) {
            if (errno == EIO) {
                patty_ax25_if_down(entry->iface);

                fd_clear(server, entry->fd);
            } else {
                goto error_io;
            }
        }

        item = item->next;
    }

    return 0;

error_io:
    return -1;
}

static int handle_sock_dgram(patty_ax25_server *server,
                             patty_ax25_sock *sock) {
    ssize_t len;

    if (!FD_ISSET(sock->fd, &server->fds_r)) {
        return 0;
    }

    if ((len = read(sock->fd, sock->io_buf, sock->n_maxlen_tx)) < 0) {
        if (errno == EIO) {
            (void)sock_close(server, sock);
        } else {
            goto error_unknown;
        }
    } else if (len == 0) {
        (void)sock_close(server, sock);
    } else if (len > 0) {
        if (patty_ax25_sock_write(sock, sock->io_buf, len) < 0) {
            (void)sock_close(server, sock);
        }
    }

    return 0;

error_unknown:
    return -1;
}

static int handle_sock_raw(patty_ax25_server *server,
                           patty_ax25_sock *sock) {
    patty_kiss_tnc *raw  = sock->raw;
    patty_ax25_if *iface = sock->iface;

    ssize_t len;

    if (!FD_ISSET(sock->fd, &server->fds_r) || raw == NULL || iface == NULL) {
        return 0;
    }

    if ((len = patty_kiss_tnc_fill(raw)) < 0) {
        goto error_io;
    } else if (len == 0) {
        (void)sock_close(server, sock);

        goto done;
    }

    while (1) {
        ssize_t len;

        if (patty_kiss_tnc_drain(raw, iface->tx_buf, iface->mtu) < 0) {
            goto error_io;
        }

        if (!patty_kiss_tnc_pending(raw)) {
            break;
        }

        if ((len = patty_kiss_tnc_flush(raw)) < 0) {
            goto error_io;
        }

        if (patty_ax25_if_send(iface, iface->tx_buf, len) < 0) {
            goto error_io;
        }
    }

done:
    return 0;

error_io:
    return -1;
}

static int handle_sock(uint32_t key,
                       void *value,
                       void *ctx) {
    patty_ax25_server *server = ctx;
    patty_ax25_sock   *sock   = value;

    ssize_t len;

    switch (sock->type) {
        case PATTY_AX25_SOCK_DGRAM:
            return handle_sock_dgram(server, sock);

        case PATTY_AX25_SOCK_RAW:
            return handle_sock_raw(server, sock);

        case PATTY_AX25_SOCK_STREAM:
            break;
    }

    patty_timer_tick(&sock->timer_t1, &server->elapsed);
    patty_timer_tick(&sock->timer_t2, &server->elapsed);
    patty_timer_tick(&sock->timer_t3, &server->elapsed);

    switch (sock->state) {
        case PATTY_AX25_SOCK_PENDING_ACCEPT:
            if (patty_timer_expired(&sock->timer_t1)) {
                if (sock->retries--) {
                    patty_timer_start(&sock->timer_t1);

                    return patty_ax25_sock_send_xid(sock, PATTY_AX25_FRAME_RESPONSE);
                } else {
                    (void)sock_close(server, sock);
                }
            }

            return 0;

        case PATTY_AX25_SOCK_PENDING_CONNECT:
            if (patty_timer_expired(&sock->timer_t1)) {
                if (sock->retries--) {
                    patty_timer_start(&sock->timer_t1);

                    return patty_ax25_sock_send_sabm(sock, PATTY_AX25_FRAME_POLL);
                } else {
                    int client;

                    if ((client = client_by_sock(server, sock)) < 0) {
                        goto error_client_by_sock;
                    }

                    (void)sock_shutdown(server, sock);
                    (void)sock_close(server, sock);

                    return respond_connect(client, -1, ETIMEDOUT);
                }
            }

            return 0;

        case PATTY_AX25_SOCK_ESTABLISHED:
            if (patty_timer_expired(&sock->timer_t1)) {
                if (sock->retries--) {
                    patty_timer_start(&sock->timer_t1);

                    return patty_ax25_sock_resend_pending(sock)
                        || patty_ax25_sock_send_rr(sock, PATTY_AX25_FRAME_COMMAND, 1);
                } else {
                    (void)sock_shutdown(server, sock);

                    return sock_close(server, sock);
                }
            }

            if (patty_timer_expired(&sock->timer_t2)) {
                patty_timer_stop(&sock->timer_t2);

                if (sock->rx_pending) {
                    sock->rx_pending = 0;

                    return patty_ax25_sock_send_rr(sock, PATTY_AX25_FRAME_RESPONSE, 1);
                }
            }

            if (patty_timer_expired(&sock->timer_t3)) {
                /*
                 * AX.25 v.2.2 Section 6.7.1.3 "Inactive Link Timer T3"
                 */
                sock->retries = sock->n_retry;

                patty_timer_stop(&sock->timer_t3);
                patty_timer_start(&sock->timer_t1);

                return patty_ax25_sock_send_rr(sock, PATTY_AX25_FRAME_COMMAND, 1);
            }

            break;

        case PATTY_AX25_SOCK_PENDING_DISCONNECT:
            if (patty_timer_expired(&sock->timer_t1)) {
                if (sock->retries--) {
                    patty_timer_start(&sock->timer_t1);

                    return patty_ax25_sock_send_disc(sock, PATTY_AX25_FRAME_POLL);
                } else {
                    return sock_close(server, sock);
                }
            }

        default:
            break;
    }

    if (!FD_ISSET(sock->fd, &server->fds_r)) {
        return 0;
    }

    if (sock->flow == PATTY_AX25_SOCK_WAIT) {
        sock->flow = PATTY_AX25_SOCK_READY;
    }

    /*
     * AX.25 v2.2, Section 6.4.1 "Sending I Frames"
     */
    if (patty_ax25_sock_flow_left(sock) == 1) {
        sock_flow_stop(server, sock);
    }

    /*
     * Check to see if any frames are pending resend.  If so, return, allowing
     * service to another socket rather than sending another frame to the
     * current peer.
     */
    if ((len = patty_ax25_sock_resend_pending(sock)) < 0) {
        goto error_sock_resend_pending;
    } else if (len > 0) {
        patty_timer_start(&sock->timer_t1);

        return 0;
    }

    if ((len = read(sock->fd, sock->io_buf, sock->n_maxlen_tx)) < 0) {
        if (errno == EIO) {
            (void)sock_shutdown(server, sock);
        } else {
            goto error_unknown;
        }
    } else if (len == 0) {
        (void)sock_shutdown(server, sock);
    } else if (len > 0) {
        if (patty_ax25_sock_write(sock, sock->io_buf, len) < 0) {
            (void)sock_close(server, sock);
        } else {
            patty_timer_start(&sock->timer_t1);
            patty_timer_stop(&sock->timer_t3);
        }
    }

    return 0;

error_client_by_sock:
error_sock_resend_pending:
error_unknown:
    return -1;
}

static int handle_socks(patty_ax25_server *server) {
    return patty_dict_each(server->socks_by_fd, handle_sock, server);
}

int patty_ax25_server_start(patty_ax25_server *server, const char *path) {
    return listen_unix(server, path);
}

int patty_ax25_server_stop(patty_ax25_server *server) {
    return close(server->fd);
}

int patty_ax25_server_event_handle(patty_ax25_server *server) {
    int nready;

    struct timeval timeout = { 1, 0 };

    struct timespec before,
                    after;

    memcpy(&server->fds_r, &server->fds_watch, sizeof(server->fds_r));

    if (clock_gettime(CLOCK_MONOTONIC, &before) < 0) {
        goto error_clock_gettime;
    }

    if ((nready = select( server->fd_max,
                         &server->fds_r,
                         NULL,
                         NULL,
                         &timeout)) < 0) {
        goto error_io;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &after) < 0) {
        goto error_clock_gettime;
    }

    patty_timer_sub(&after, &before, &server->elapsed);

    if (handle_socks(server) < 0) {
        goto error_io;
    }

    if (nready > 0) {
        if (handle_clients(server) < 0) {
            goto error_io;
        }

        if (handle_ifaces(server) < 0) {
            goto error_io;
        }

        if (accept_client(server) < 0) {
            goto error_io;
        }
    }

    return 0;

error_clock_gettime:
error_io:
    return -1;
}
