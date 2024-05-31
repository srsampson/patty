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

    int local,
        remote;

    if (argc < 2) {
        usage(argc, argv, "No patty socket provided");
    } else if (argc < 3) {
        usage(argc, argv, "No local callsign provided");
    } else if (argc > 3) {
        usage(argc, argv, "Too many arguments provided");
    }

    patty_ax25_pton(argv[2], &addr);

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

    if ((remote = patty_client_accept(client, local, &peer)) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_client_accept()", strerror(errno));

        goto error_client_accept;
    }

    if (write(remote, "hello world\n", 12) < 0) {
        goto error_write;
    }

    sleep(1);

    patty_client_close(client, remote);
    patty_client_close(client, local);

    patty_client_destroy(client);

    return 0;

error_write:
    (void)patty_client_close(client, remote);

error_client_accept:
error_client_listen:
error_client_bind:
    (void)patty_client_close(client, local);

error_client_socket:
    patty_client_destroy(client);

error_client_new:
    return 1;
}
