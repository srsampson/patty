#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

#include <patty/ax25.h>

static void usage(int argc, char **argv, const char *message, ...) {
    if (message != NULL) {
        va_list args;

        va_start(args, message);
        vfprintf(stderr, message, args);
         fprintf(stderr, "\n");
        va_end(args);
    }

    fprintf(stderr, "usage: %s /var/run/patty/patty.sock remotecall\n", argv[0]);

    exit(1);
}

int main(int argc, char **argv) {
    patty_client *client;
    patty_ax25_addr peer;

    int fd;

    uint8_t buf[4096];
    ssize_t readlen;

    if (argc < 2) {
        usage(argc, argv, "No patty socket provided");
    } else if (argc < 3) {
        usage(argc, argv, "No remote callsign provided");
    } else if (argc > 3) {
        usage(argc, argv, "Too many arguments provided");
    }

    patty_ax25_pton(argv[2], &peer);

    if ((client = patty_client_new(argv[1])) == NULL) {
        fprintf(stderr, "%s: %s: %s: %s\n",
            argv[0], "patty_client_new()", argv[1], strerror(errno));

        goto error_client_new;
    }

    if ((fd = patty_client_socket(client, PATTY_AX25_PROTO_NONE, PATTY_AX25_SOCK_STREAM)) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_client_socket()", strerror(errno));

        goto error_client_socket;
    }

    if (patty_client_connect(client, fd, &peer) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_client_connect()", strerror(errno));

        goto error_client_connect;
    }

    while ((readlen = read(fd, buf, sizeof(buf))) > 0) {
        if (write(1, buf, readlen) < 0) {
            goto error_write;
        }
    }

    patty_client_close(client, fd);

    patty_client_destroy(client);

    return 0;

error_write:
error_client_connect:
    patty_client_close(client, fd);

error_client_socket:
    patty_client_destroy(client);

error_client_new:
    return 1;
}
