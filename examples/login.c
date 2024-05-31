#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

#include <patty/ax25.h>

extern char **environ;

static void usage(int argc, char **argv, const char *message, ...) {
    if (message != NULL) {
        va_list args;

        va_start(args, message);
        vfprintf(stderr, message, args);
         fprintf(stderr, "\n");
        va_end(args);
    }

    fprintf(stderr, "usage: %s /var/run/patty/patty.sock localcall\n", argv[0]);

    exit(1);
}

int main(int argc, char **argv) {
    patty_client *client;

    patty_ax25_addr addr,
                    peer;

    int local;

    fd_set fds_watch;

    if (argc < 2) {
        usage(argc, argv, "No patty socket provided");
    } else if (argc < 3) {
        usage(argc, argv, "No local callsign provided");
    } else if (argc > 3) {
        usage(argc, argv, "Too many arguments provided");
    }

    if (patty_ax25_pton(argv[2], &addr) < 0) {
        fprintf(stderr, "%s: %s: %s: %s\n",
            argv[0], "patty_ax25_pton()", argv[2], strerror(errno));

        goto error_pton;
    }

    if ((client = patty_client_new(argv[1])) == NULL) {
        fprintf(stderr, "%s: %s: %s: %s\n",
            argv[0], "patty_client_new()", argv[1], strerror(errno));

        goto error_client_new;
    }

    if ((local = patty_client_socket(client, PATTY_AX25_PROTO_NONE, PATTY_AX25_SOCK_STREAM)) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_client_socket()", strerror(errno));

        goto error_client_socket;
    }

    if (patty_client_bind(client, local, &addr) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_client_bind()", strerror(errno));

        goto error_client_bind;
    }

    if (patty_client_listen(client, local) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_client_listen()", strerror(errno));

        goto error_client_listen;
    }

    FD_ZERO(&fds_watch);
    FD_SET(local, &fds_watch);

    while (1) {
        fd_set fds_ready;

        struct timeval timeout = {
            .tv_sec  = 2,
            .tv_usec = 0
        };

        int pong,
            nready,
            status;

        if ((pong = patty_client_ping(client)) < 0) {
            fprintf(stderr, "%s: %s: %s\n",
                argv[0], "patty_client_ping()", strerror(errno));

            goto error_client_ping;
        } else if (pong == 0) {
            break;
        }

        memcpy(&fds_ready, &fds_watch, sizeof(fds_ready));

        if ((nready = select(local + 1, &fds_ready, NULL, NULL, &timeout)) < 0) {
            fprintf(stderr, "%s: %s: %s\n",
                argv[0], "select()", strerror(errno));

            goto error_select;
        } else if (nready == 0) {
            continue;
        }

        if (waitpid(-1, &status, WNOHANG) < 0) {
            errno = 0;
        }

        if (FD_ISSET(local, &fds_ready)) {
            patty_client_setsockopt_params params = {
                .flags = PATTY_AX25_SOCK_PARAM_MTU
                       | PATTY_AX25_SOCK_PARAM_WINDOW,

                .mtu    = 254,
                .window = 1
            };

            int pid,
                remote;

            if ((remote = patty_client_accept(client, local, &peer)) < 0) {
                exit(127);
            }

            if (patty_client_setsockopt(client,
                                        remote,
                                        PATTY_AX25_SOCK_PARAMS,
                                        &params,
                                        sizeof(params)) < 0) {
                exit(127);
            }

            if ((pid = fork()) < 0) {
                fprintf(stderr, "%s: %s: %s\n",
                    argv[0], "fork()", strerror(errno));

                goto error_fork;
            } else if (pid == 0) {
                char *args[] = {
                    "/bin/login",
                    "-h",
                    NULL,
                    NULL
                };

                char *name;
                char address[10];

                int fd;

                if (patty_ax25_ntop(&peer, address, sizeof(address)) < 0) {
                    fprintf(stderr, "%s: %s: %s\n",
                        argv[0], "patty_ax25_ntop()", strerror(errno));

                    exit(127);
                }

                args[2] = address;

                if ((name = ttyname(remote)) == NULL) {
                    fprintf(stderr, "%s: %s: %s\n",
                        argv[0], "ttyname()", strerror(errno));

                    exit(127);
                }

                if (setsid() < 0) {
                    fprintf(stderr, "%s: %s: %s\n",
                        argv[0], "setsid()", strerror(errno));

                    exit(127);
                }

                if ((fd = open(name, O_RDWR)) < 0) {
                    fprintf(stderr, "%s: %s: %s:  %s\n",
                        argv[0], "open()", name, strerror(errno));
                }

                close(remote);

                close(0);
                close(1);
                close(2);

                dup2(fd, 0);
                dup2(fd, 1);
                dup2(fd, 2);

                if (execve(args[0], args, environ) < 0) {
                    exit(127);
                }
            }

            close(remote);
        }
    }

    patty_client_close(client, local);

    patty_client_destroy(client);

    return 0;

error_fork:
error_select:
error_client_ping:
error_client_listen:
error_client_bind:
    (void)patty_client_close(client, local);

error_client_socket:
    patty_client_destroy(client);

error_client_new:
error_pton:
    return 1;
}
