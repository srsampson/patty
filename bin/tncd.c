#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <sysexits.h>

#include <patty/ax25.h>
#include <patty/print.h>
#include <patty/util.h>

#include <patty/bin/kiss.h>

static void usage(int argc, char **argv, const char *message, ...) {
    if (message != NULL) {
        va_list args;

        va_start(args, message);
        vfprintf(stderr, message, args);
         fprintf(stderr, "\n");
        va_end(args);
    }

    fprintf(stderr, "usage: %s [-s patty.sock] -i ifname\n", argv[0]);

    exit(EX_USAGE);
}

static int pty_close(patty_client *client, int fd) {
    enum patty_client_call call = PATTY_CLIENT_CLOSE;

    patty_client_close_request request = {
        .fd = fd
    };

    patty_client_close_response response;

    if (patty_client_write(client, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (patty_client_write(client, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if (patty_client_read(client, &response, sizeof(response)) < 0) {
        goto error_io;
    }

    if (response.ret < 0) {
        errno = response.eno;
    }

    return response.ret;

error_io:
    return -1;
}

static int pty_promisc(patty_client *client,
                       const char *ifname,
                       char *pty,
                       size_t len) {
    enum patty_client_call call;

    patty_client_socket_request socket_request = {
        .proto = PATTY_AX25_PROTO_NONE,
        .type  = PATTY_AX25_SOCK_RAW
    };

    patty_client_socket_response socket_response;
    patty_client_setsockopt_request setsockopt_request;
    patty_client_setsockopt_response setsockopt_response;

    patty_client_setsockopt_if ifreq;

    call = PATTY_CLIENT_SOCKET;

    if (patty_client_write(client, &call, sizeof(call)) < 0) {
        goto error_client_write_socket_request;
    }

    if (patty_client_write(client,
                           &socket_request,
                           sizeof(socket_request)) < 0) {
        goto error_client_write_socket_request;
    }

    if (patty_client_read(client,
                          &socket_response,
                          sizeof(socket_response)) < 0) {
        goto error_client_read_socket_response;
    }

    if (socket_response.fd < 0) {
        errno = socket_response.eno;

        goto error_client_socket;
    }

    setsockopt_request.fd  = socket_response.fd;
    setsockopt_request.opt = PATTY_AX25_SOCK_IF;
    setsockopt_request.len = sizeof(ifreq);

    patty_strlcpy(ifreq.name, ifname, sizeof(ifreq.name));
    ifreq.state = PATTY_AX25_SOCK_PROMISC;

    call = PATTY_CLIENT_SETSOCKOPT;

    if (patty_client_write(client, &call, sizeof(call)) < 0) {
        goto error_client_write_socket_request;
    }

    if (patty_client_write(client,
                           &setsockopt_request,
                           sizeof(setsockopt_request)) < 0) {
        goto error_client_write_setsockopt_request;
    }

    if (patty_client_write(client,
                           &ifreq,
                           sizeof(ifreq)) < 0) {
        goto error_client_write_ifreq;
    }

    if (patty_client_read(client,
                          &setsockopt_response,
                          sizeof(setsockopt_response)) < 0) {
        goto error_client_read_setsockopt_response;
    }

    if (setsockopt_response.ret < 0) {
        errno = setsockopt_response.eno;

        goto error_client_setsockopt;
    }

    patty_strlcpy(pty, socket_response.path, len);

    return socket_response.fd;

error_client_setsockopt:
error_client_read_setsockopt_response:
error_client_write_ifreq:
error_client_write_setsockopt_request:
    (void)pty_close(client, socket_response.fd);

error_client_socket:
error_client_read_socket_response:
error_client_write_socket_request:
    return -1;
}

int main(int argc, char **argv) {
    patty_client *client;

    struct option opts[] = {
        { "sock", required_argument, NULL, 's' },
        { "if",   required_argument, NULL, 'i' },
        { NULL,   0,                 NULL,  0  }
    };

    char *sock   = NULL,
         *ifname = NULL;

    int ch,
        index;

    int fd;
    char pty[256];

    while ((ch = getopt_long(argc, argv, "s:i:", opts, &index)) >= 0) {
        switch (ch) {
            case 's': sock   = optarg; break;
            case 'i': ifname = optarg; break;

            default:
                usage(argc, argv, NULL);
        }
    }

    if (ifname == NULL) {
        usage(argc, argv, "No interface name provided");
    }

    if (optind < argc) {
        usage(argc, argv, "Too many arguments provided");
    }

    if ((client = patty_client_new(sock)) == NULL) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_client_new()", strerror(errno));

        goto error_client_new;
    }

    if ((fd = pty_promisc(client, ifname, pty, sizeof(pty))) < 0) {
        fprintf(stderr, "%s: %s: %s: %s\n",
            argv[0], "pty_promisc()", ifname, strerror(errno));

        goto error_pty_promisc;
    }

    printf("%s\n", pty);

    while (1) {
        int pong;

        if ((pong = patty_client_ping(client)) < 0) {
            fprintf(stderr, "%s: %s: %s\n",
                argv[0], "patty_client_ping()", strerror(errno));

            goto error_client_ping;
        } else if (pong == 0) {
            break;
        }

        sleep(2);
    }

    patty_client_destroy(client);

error_client_ping:
    (void)pty_close(client, fd);

error_pty_promisc:
    patty_client_destroy(client);

error_client_new:
    return 1;
}
