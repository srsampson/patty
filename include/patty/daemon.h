#ifndef _PATTY_DAEMON_H
#define _PATTY_DAEMON_H

#define PATTY_DAEMON_DEFAULT_SOCK    "/var/run/patty/patty.sock"
#define PATTY_DAEMON_DEFAULT_PIDFILE "/var/run/patty/patty.pid"

typedef struct _patty_daemon patty_daemon;

patty_daemon *patty_daemon_new();

void patty_daemon_destroy(patty_daemon *daemon);

int patty_daemon_run(patty_daemon *daemon);

int patty_daemon_set_sock_path(patty_daemon *daemon, const char *path);

int patty_daemon_set_pidfile(patty_daemon *daemon, const char *path);

int patty_daemon_if_add(patty_daemon *daemon,
                        patty_ax25_if *iface,
                        const char *ifname);

int patty_daemon_route_add(patty_daemon *daemon,
                           const char *ifname,
                           const char *dest,
                           const char **repeaters,
                           int hops);

int patty_daemon_route_add_default(patty_daemon *daemon,
                                   const char *ifname);

#endif /* _PATTY_DAEMON_H */
