#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <patty/ax25.h>
#include <patty/ax25/aprs_is.h>
#include <patty/kiss/tnc.h>
#include <patty/bin/if.h>

struct context {
    patty_error *err;

    char *name;
    patty_ax25_addr addr;
};

enum mode {
    MODE_NONE,
    MODE_IFNAME,
    MODE_IFOPTS,
    MODE_IFADDR,
    MODE_KISS,
    MODE_APRS_IS,
};

enum mode_kiss {
    MODE_KISS_DEVICE,
    MODE_KISS_IFOPTS,
    MODE_KISS_BAUD,
    MODE_KISS_FLOW,
};

enum mode_aprs_is {
    MODE_APRS_IS_IFOPTS,
    MODE_APRS_IS_HOST,
    MODE_APRS_IS_PORT,
    MODE_APRS_IS_USER,
    MODE_APRS_IS_PASS,
    MODE_APRS_IS_APPNAME,
    MODE_APRS_IS_VERSION,
    MODE_APRS_IS_FILTER,
};

static patty_ax25_if *create_kiss(struct context *ctx, int argc, char **argv) {
    enum mode_kiss mode = MODE_KISS_DEVICE;

    patty_kiss_tnc_info info;
    patty_ax25_if *iface;

    int i;

    memset(&info, '\0', sizeof(info));

    if (argc == 0) {
        patty_error_fmt(ctx->err, "Too few parameters for 'kiss' interface");

        goto error_invalid;
    }

    for (i=0; i<argc; i++) {
        switch (mode) {
            case MODE_KISS_DEVICE:
                if (access(argv[i], R_OK | W_OK) < 0) {
                    patty_error_fmt(ctx->err, "Unable to access device %s: %s",
                        argv[i], strerror(errno));
                }

                info.flags |= PATTY_KISS_TNC_DEVICE;
                info.device = argv[i];

                mode = MODE_KISS_IFOPTS;

                break;

            case MODE_KISS_IFOPTS:
                if (strcmp(argv[i], "baud") == 0) {
                    mode = MODE_KISS_BAUD;
                } else if (strcmp(argv[i], "flow") == 0) {
                    mode = MODE_KISS_FLOW;
                } else {
                    patty_error_fmt(ctx->err, "Invalid parameter '%s'",
                        argv[i]);
                }

                break;

            case MODE_KISS_BAUD:
                if (!(argv[i][0] >= '0' && argv[i][0] <= '9')) {
                    patty_error_fmt(ctx->err, "Invalid baud rate '%s'",
                        argv[i]);

                    goto error_invalid;
                }

                info.flags |= PATTY_KISS_TNC_BAUD;
                info.baud   = atoi(argv[i]);

                mode = MODE_KISS_IFOPTS;

                break;

            case MODE_KISS_FLOW:
                if (strcmp(argv[i], "crtscts") == 0) {
                    info.flags |= PATTY_KISS_TNC_FLOW;
                    info.flow   = PATTY_KISS_TNC_FLOW_CRTSCTS;
                } else if (strcmp(argv[i], "xonxoff") == 0) {
                    info.flags |= PATTY_KISS_TNC_FLOW;
                    info.flow   = PATTY_KISS_TNC_FLOW_XONXOFF;
                } else {
                    patty_error_fmt(ctx->err, "Invalid flow control '%s'",
                        argv[i]);

                    goto error_invalid;
                }

                mode = MODE_KISS_IFOPTS;

                break;

            default:
                break;
        }
    }

    if ((iface = patty_ax25_if_new(patty_kiss_tnc_driver(),
                                   &info)) == NULL) {
        goto error_if_new;
    }

    if (patty_ax25_if_addr_set(iface, &ctx->addr) < 0) {
        goto error_if_addr_set;
    }

    patty_ax25_if_up(iface);

    return iface;

error_if_addr_set:
    patty_ax25_if_destroy(iface);

error_if_new:
error_invalid:
    return NULL;
}

static patty_ax25_if *create_aprs_is(struct context *ctx,
                                     int argc,
                                     char **argv) {
    patty_ax25_if *iface;

    patty_ax25_aprs_is_info info = {
        .user    = "N0CALL",
        .pass    = "-1",
        .appname = PATTY_AX25_APRS_IS_DEFAULT_APPNAME,
        .version = PATTY_AX25_APRS_IS_DEFAULT_VERSION,
        .filter  = "m/25"
    };

    enum mode_aprs_is mode = MODE_APRS_IS_IFOPTS;

    int i;

    if (argc == 0) {
        patty_error_fmt(ctx->err, "Too few parameters for 'kiss' interface");

        goto error_invalid;
    }

    for (i=0; i<argc; i++) {
        switch (mode) {
            case MODE_APRS_IS_IFOPTS:
                if (strcmp(argv[i], "host") == 0) {
                    mode = MODE_APRS_IS_HOST;
                } else if (strcmp(argv[i], "port") == 0) {
                    mode = MODE_APRS_IS_PORT;
                } else if (strcmp(argv[i], "user") == 0) {
                    mode = MODE_APRS_IS_USER;
                } else if (strcmp(argv[i], "pass") == 0) {
                    mode = MODE_APRS_IS_PASS;
                } else if (strcmp(argv[i], "appname") == 0) {
                    mode = MODE_APRS_IS_APPNAME;
                } else if (strcmp(argv[i], "version") == 0) {
                    mode = MODE_APRS_IS_VERSION;
                } else if (strcmp(argv[i], "filter") == 0) {
                    mode = MODE_APRS_IS_FILTER;
                } else {
                    patty_error_fmt(ctx->err, "Invalid parameter '%s'",
                        argv[i]);
                }

                break;

            case MODE_APRS_IS_HOST:
                info.host = argv[i];
                mode = MODE_APRS_IS_IFOPTS;

                break;

            case MODE_APRS_IS_PORT:
                info.port = argv[i];
                mode = MODE_APRS_IS_IFOPTS;

                break;

            case MODE_APRS_IS_USER:
                info.user = argv[i];
                mode = MODE_APRS_IS_IFOPTS;

                break;

            case MODE_APRS_IS_PASS:
                info.pass = argv[i];
                mode = MODE_APRS_IS_IFOPTS;

                break;

            case MODE_APRS_IS_APPNAME:
                info.appname = argv[i];
                mode = MODE_APRS_IS_IFOPTS;

                break;

            case MODE_APRS_IS_VERSION:
                info.version = argv[i];
                mode = MODE_APRS_IS_IFOPTS;

                break;

            case MODE_APRS_IS_FILTER:
                info.filter = argv[i];
                mode = MODE_APRS_IS_IFOPTS;

                break;
        }
    }

    if ((iface = patty_ax25_if_new(patty_ax25_aprs_is_driver(),
                                   &info)) == NULL) {
        patty_error_fmt(ctx->err, "Unable to connect to APRS-IS service %s:%s: %s",
            info.host, info.port, strerror(errno));

        goto error_if_new;
    }

    if (patty_ax25_if_addr_set(iface, &ctx->addr) < 0) {
        goto error_if_addr_set;
    }

    return iface;

error_if_addr_set:
    patty_ax25_if_destroy(iface);

error_if_new:
error_invalid:
    return NULL;
}

struct if_type {
    const char *name;
    patty_ax25_if *(*func)(struct context *ctx, int argc, char **argv);
};

struct if_type if_types[] = {
    { "kiss",    create_kiss    },
    { "aprs-is", create_aprs_is },
    {  NULL,     NULL           }
};

patty_ax25_if *patty_bin_if_create(int argc,
                                   char **argv,
                                   char **ifname,
                                   patty_error *e) {
    enum mode mode = MODE_NONE;

    struct context ctx = {
        .err  = e,
        .name = NULL
    };

    int i;

    patty_error_clear(e);

    memset(&ctx.addr, '\0', sizeof(ctx.addr));

    for (i=0; i<argc; i++) {
        switch (mode) {
            case MODE_NONE:
                if (strcmp(argv[i], "if") != 0) {
                    patty_error_fmt(e, "Unexpected start of expression '%s'",
                        argv[i]);

                    goto error_invalid;
                } else {
                    mode = MODE_IFNAME;
                }

                break;

            case MODE_IFNAME:
                ctx.name = argv[i];

                mode = MODE_IFOPTS;

                break;

            case MODE_IFOPTS:
                if (strcmp(argv[i], "ax25") == 0) {
                    mode = MODE_IFADDR;
                } else {
                    int t;

                    for (t=0; if_types[t].name; t++) {
                        if (strcmp(if_types[t].name, argv[i]) == 0) {
                            *ifname = ctx.name;

                            return if_types[t].func(&ctx,
                                                    argc - i - 1,
                                                    argv + i + 1);
                        }
                    }

                    patty_error_fmt(e, "Invalid parameter '%s'", argv[i]);

                    goto error_invalid;
                }

                break;

            case MODE_IFADDR:
                if (patty_ax25_pton(argv[i], &ctx.addr) < 0) {
                    patty_error_fmt(e, "Invalid AX.25 address '%s': %s",
                        argv[i], strerror(errno));

                    goto error_invalid;
                }

                mode = MODE_IFOPTS;

                break;

            default:
                break;
        }
    }

    patty_error_fmt(e, "Media type not provided");

error_invalid:
    return NULL;
}
