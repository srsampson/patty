#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <patty/ax25.h>
#include <patty/daemon.h>

struct _patty_daemon {
    char *sock_path,
         *pidfile;

    patty_ax25_server *server;
};

patty_daemon *patty_daemon_new() {
    patty_daemon *daemon;

    if ((daemon = malloc(sizeof(*daemon))) == NULL) {
        goto error_malloc;
    }

    memset(daemon, '\0', sizeof(*daemon));

    if (patty_daemon_set_sock_path(daemon, PATTY_DAEMON_DEFAULT_SOCK) < 0) {
        goto error_set_sock_path;
    }

    if (patty_daemon_set_pidfile(daemon, PATTY_DAEMON_DEFAULT_PIDFILE) < 0) {
        goto error_set_pidfile;
    }

    if ((daemon->server = patty_ax25_server_new()) == NULL) {
        goto error_server_new;
    }

    return daemon;

error_server_new:
    free(daemon->pidfile);

error_set_pidfile:
    free(daemon->sock_path);

error_set_sock_path:
    free(daemon);

error_malloc:
    return NULL;
}

void patty_daemon_destroy(patty_daemon *daemon) {
    if (daemon->server) {
        patty_ax25_server_destroy(daemon->server);
    }

    if (daemon->sock_path) {
        free(daemon->sock_path);
    }

    if (daemon->pidfile) {
        free(daemon->pidfile);
    }

    free(daemon);
}

int patty_daemon_run(patty_daemon *daemon) {
    if (patty_ax25_server_start(daemon->server, daemon->sock_path) < 0) {
        goto error_server_start;
    }

    while (1) {
        if (patty_ax25_server_event_handle(daemon->server) < 0) {
            goto error_server_event_handle;
        }
    }

    patty_ax25_server_stop(daemon->server);

    return 0;

error_server_event_handle:
    patty_ax25_server_stop(daemon->server);

error_server_start:
    return -1;
}

int patty_daemon_set_sock_path(patty_daemon *daemon, const char *path) {
    if (daemon->sock_path) {
        free(daemon->sock_path);
    }

    if ((daemon->sock_path = strdup(path)) == NULL) {
        goto error_strdup;
    }

    return 0;

error_strdup:
    return -1;
}

int patty_daemon_set_pidfile(patty_daemon *daemon, const char *path) {
    if (daemon->pidfile) {
        free(daemon->pidfile);
    }

    if ((daemon->pidfile = strdup(path)) == NULL) {
        goto error_strdup;
    }

    return 0;

error_strdup:
    return -1;
}

int patty_daemon_if_add(patty_daemon *daemon,
                        patty_ax25_if *iface,
                        const char *ifname) {
    return patty_ax25_server_if_add(daemon->server, iface, ifname);
}

int patty_daemon_route_add(patty_daemon *daemon,
                           const char *ifname,
                           const char *dest,
                           const char **repeaters,
                           int hops) {
    patty_ax25_route *route;
    patty_ax25_if *iface;

    int i;

    if ((iface = patty_ax25_server_if_get(daemon->server, ifname)) == NULL) {
        goto error_server_if_get;
    }

    if ((route = malloc(sizeof(*route))) == NULL) {
        goto error_malloc_route;
    }

    memset(route, '\0', sizeof(*route));

    route->iface = iface;

    if (dest) {
        if (patty_ax25_pton(dest, &route->dest) < 0) {
            goto error_pton;
        }
    }

    for (i=0; i<hops; i++) {
        if (patty_ax25_pton(repeaters[i], &route->repeaters[i]) < 0) {
            goto error_pton;
        }
    }

    return patty_ax25_server_route_add(daemon->server, route);

error_pton:
    free(route);

error_malloc_route:
error_server_if_get:
    return -1;
}

int patty_daemon_route_add_default(patty_daemon *daemon,
                                   const char *ifname) {
    return patty_daemon_route_add(daemon, ifname, NULL, NULL, 0);
}
