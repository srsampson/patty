#ifndef _PATTY_AX25_SERVER_H
#define _PATTY_AX25_SERVER_H

typedef struct _patty_ax25_server patty_ax25_server;

patty_ax25_server *patty_ax25_server_new();

void patty_ax25_server_destroy(patty_ax25_server *server);

int patty_ax25_server_if_add(patty_ax25_server *server,
                             patty_ax25_if *iface,
                             const char *ifname);

int patty_ax25_server_if_delete(patty_ax25_server *server,
                                const char *ifname);

patty_ax25_if *patty_ax25_server_if_get(patty_ax25_server *server,
                                        const char *ifname);

int patty_ax25_server_if_each(patty_ax25_server *server,
                              int (*callback)(char *, patty_ax25_if *, void *),
                              void *ctx);

int patty_ax25_server_route_add(patty_ax25_server *server,
                                patty_ax25_route *route);

int patty_ax25_server_route_delete(patty_ax25_server *server,
                                   patty_ax25_addr *dest);

patty_ax25_route *patty_ax25_server_route_find(patty_ax25_server *server,
                                               patty_ax25_addr *dest);

patty_ax25_route *patty_ax25_server_route_default(patty_ax25_server *server);

int patty_ax25_server_route_each(patty_ax25_server *server,
                                 int (*callback)(patty_ax25_route *, void *),
                                 void *ctx);

int patty_ax25_server_start(patty_ax25_server *server, const char *path);

int patty_ax25_server_stop(patty_ax25_server *server);

int patty_ax25_server_event_handle(patty_ax25_server *server);

#endif /* _PATTY_AX25_SERVER_H */
