#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <sysexits.h>

#include <patty/ax25.h>
#include <patty/print.h>
#include <patty/util.h>

#include <patty/bin/kiss.h>

#define AX25DUMP_BUFSZ 4096

static void usage(int argc, char **argv, const char *message, ...) {
    if (message != NULL) {
        va_list args;

        va_start(args, message);
        vfprintf(stderr, message, args);
         fprintf(stderr, "\n");
        va_end(args);
    }

    fprintf(stderr, "usage: %s [-s patty.sock] -i ifname\n"
                    "       %s /dev/ttyXYZ [tioarg ...]\n"
                    "       %s file.cap\n", argv[0], argv[0], argv[0]);

    exit(EX_USAGE);
}

int main(int argc, char **argv) {
    patty_client *client = NULL;

    struct option opts[] = {
        { "sock", required_argument, NULL, 's' },
        { "if",   required_argument, NULL, 'i' },
        { NULL,   0,                 NULL,  0  }
    };

    void *buf;
    ssize_t readlen;

    char *sock   = NULL,
         *ifname = NULL;

    patty_kiss_tnc_info info;
    patty_kiss_tnc *raw;

    int index,
        ch;

    while ((ch = getopt_long(argc, argv, "s:i:", opts, &index)) >= 0) {
        switch (ch) {
            case 's': sock   = optarg; break;
            case 'i': ifname = optarg; break;

            default:
                usage(argc, argv, NULL);
        }
    }

    memset(&info, '\0', sizeof(info));

    if (ifname) {
        patty_client_setsockopt_if ifreq;

        if (optind < argc) {
            usage(argc, argv, "Too many arguments provided");
        }

        info.flags = PATTY_KISS_TNC_FD;

        if ((client = patty_client_new(sock)) == NULL) {
            fprintf(stderr, "%s: %s: %s: %s\n",
                argv[0], "patty_client_new()", sock, strerror(errno));

            goto error_client_new;
        }

        if ((info.fd = patty_client_socket(client, PATTY_AX25_PROTO_NONE, PATTY_AX25_SOCK_RAW)) < 0) {
            fprintf(stderr, "%s: %s: %s\n",
                argv[0], "patty_client_socket()", strerror(errno));

            goto error_client_socket;
        }

        patty_strlcpy(ifreq.name, ifname, sizeof(ifreq.name));

        ifreq.state = PATTY_AX25_SOCK_PROMISC;

        if (patty_client_setsockopt(client, info.fd, PATTY_AX25_SOCK_IF, &ifreq, sizeof(ifreq)) < 0) {
            fprintf(stderr, "%s: %s: %s: %s\n",
                argv[0], "patty_client_setsockopt()", ifname, strerror(errno));

            goto error_client_setsockopt;
        }
    } else {
        patty_error e;

        if (argc < 2) {
            usage(argc, argv, "Not enough arguments provided");
        }

        if (patty_bin_kiss_config(argc - 1, argv + 1, &info, &e) < 0) {
            fprintf(stderr, "%s: %s: %s: %s\n",
                argv[0], "patty_bin_kiss_config()", sock, patty_error_string(&e));

            goto error_kiss_config;
        }
    }

    if ((raw = patty_kiss_tnc_new(&info)) == NULL) {
        fprintf(stderr, "%s: fd %d: %s: %s\n",
            argv[0], info.fd, "patty_kiss_tnc_new()", strerror(errno));

        goto error_kiss_tnc_new;
    }

    if ((buf = malloc(AX25DUMP_BUFSZ)) == NULL) {
        goto error_malloc_buf;
    }

    while ((readlen = patty_kiss_tnc_recv(raw, buf, AX25DUMP_BUFSZ)) > 0) {
        ssize_t decoded,
                offset = 0;

        patty_ax25_frame frame;

        if ((decoded = patty_ax25_frame_decode_address(&frame, buf, readlen)) < 0) {
            printf("Invalid frame address\n");

            goto error_ax25_frame_decode_address;
        } else {
            offset += decoded;
        }

        if ((decoded = patty_ax25_frame_decode_control(&frame, PATTY_AX25_FRAME_NORMAL, buf, decoded, readlen)) < 0) {
            printf("Invalid frame control\n");

            goto error_ax25_frame_decode_control;
        } else {
            offset += decoded;
        }

        if (patty_print_frame_header(stdout, &frame) < 0) {
            fprintf(stderr, "%s: %s: %s\n",
                argv[0], "patty_print_frame_header()", strerror(errno));

            goto error_io;
        }

        if (frame.type == PATTY_AX25_FRAME_XID) {
            patty_ax25_params params;

            if (patty_ax25_frame_decode_xid(&params,
                                            buf,
                                            offset,
                                            readlen) < 0) {
                printf("Invalid XID parameters\n");

                goto error_ax25_frame_decode_xid;
            } else {
                if (patty_print_params(stdout, &params) < 0) {
                    goto error_io;
                }
            }
        }

error_ax25_frame_decode_xid:
error_ax25_frame_decode_control:
error_ax25_frame_decode_address:
        if (patty_print_hexdump(stdout, buf, readlen) < 0) {
            goto error_io;
        }

        if (fflush(stdout) < 0) {
            goto error_io;
        }
    }

    free(buf);

    patty_kiss_tnc_destroy(raw);

    if (client) {
        patty_client_close(client, info.fd);

        patty_client_destroy(client);
    }

    return 0;

error_io:
    free(buf);

error_malloc_buf:
    patty_kiss_tnc_destroy(raw);

error_kiss_tnc_new:
error_kiss_config:
error_client_setsockopt:
error_client_socket:
    if (client) patty_client_close(client, info.fd);

error_client_new:
    if (client) patty_client_destroy(client);

    return 1;
}
