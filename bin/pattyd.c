#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <sysexits.h>
#include <errno.h>

#include <patty/ax25.h>
#include <patty/daemon.h>
#include <patty/conf.h>

#include <patty/bin/if.h>
#include <patty/bin/kiss.h>

#define DEFAULT_CONFIG_FILE "/etc/patty/pattyd.conf"
#define DEFAULT_IFNAME      "kiss0"

static int usage(int argc, char **argv, const char *message, ...) {
    if (message != NULL) {
        va_list args;

        va_start(args, message);
        vfprintf(stderr, message, args);
         fprintf(stderr, "\n");
        va_end(args);
    }

    fprintf(stderr, "usage: %s [-f] [-c pattyd.conf]\n"
                    "       %s [-f] -s patty.sock MYCALL /dev/ttyXYZ [tioarg ...]\n",
        argv[0], argv[0]);

    return EX_USAGE;
}

struct context {
    char *config_file;

    patty_daemon *daemon;
    patty_error e;
};

static int handle_sock(struct context *ctx,
                       int lineno,
                       int argc,
                       char **argv) {
    if (argc != 2) {
        patty_error_fmt(&ctx->e, "line %d: Invalid arguments for 'sock'",
            lineno);

        goto error_invalid_args;
    }

    if (patty_daemon_set_sock_path(ctx->daemon, argv[1]) < 0) {
        patty_error_fmt(&ctx->e, "line %d: Unable to set socket path to %s: %s",
            lineno, argv[1], strerror(errno));

        goto error_set_sock_path;
    }

    return 0;

error_set_sock_path:
error_invalid_args:
    return -1;
}

static int handle_pid(struct context *ctx,
                      int lineno,
                      int argc,
                      char **argv) {
    if (argc != 2) {
        patty_error_fmt(&ctx->e, "line %d: Invalid arguments for 'sock'",
            lineno);

        goto error_invalid_args;
    }

    if (patty_daemon_set_pidfile(ctx->daemon, argv[1]) < 0) {
        patty_error_fmt(&ctx->e, "line %d: Unable to set pidfile to %s: %s",
            lineno, argv[1], strerror(errno));

        goto error_set_pidfile;
    }

    return 0;

error_set_pidfile:
error_invalid_args:
    return -1;
}

static int handle_if(struct context *ctx,
                     int lineno,
                     int argc,
                     char **argv) {
    patty_ax25_if *iface;
    patty_error e;
    char *ifname = NULL;

    if (argc < 2) {
        patty_error_fmt(&ctx->e, "line %d: No interface name provided",
            lineno);

        goto error_invalid;
    } else if (argc < 3) {
        patty_error_fmt(&ctx->e, "line %d: No interface options provided",
            lineno);

        goto error_invalid;
    }

    if ((iface = patty_bin_if_create(argc, argv, &ifname, &e)) == NULL) {
        patty_error_fmt(&ctx->e, "line %d: %s",
            lineno,
            patty_error_set(&e)? patty_error_string(&e): strerror(errno));

        goto error_invalid;
    } else {
        int fd = patty_ax25_if_fd(iface);
        char *pty;

        if (isatty(fd) && (pty = ptsname(fd)) != NULL) {
            printf("if %s pty %s\n", ifname, pty);

            fflush(stdout);
        }

        patty_ax25_if_up(iface);
    }

    if (patty_daemon_if_add(ctx->daemon, iface, ifname) < 0) {
        patty_error_fmt(&ctx->e, "line %d: Unable to create interface %s: %s",
            lineno,
            ifname,
            strerror(errno));

        goto error_daemon_if_add;
    }

    return 0;

error_daemon_if_add:
    patty_ax25_if_destroy(iface);

error_invalid:
    return -1;
}

static int handle_route(struct context *ctx,
                        int lineno,
                        int argc,
                        char **argv) {
    if (argc < 2) {
        patty_error_fmt(&ctx->e, "line %d: Invalid route declaration",
            lineno);

        goto error_invalid_route;
    }

    if (strcmp(argv[1], "default") == 0) {
        if (argc != 4 || strcmp(argv[2], "if") != 0) {
            patty_error_fmt(&ctx->e, "line %d: Invalid default route declaration",
                lineno);

            goto error_invalid_route;
        }

        if (patty_daemon_route_add_default(ctx->daemon, argv[3]) < 0) {
            patty_error_fmt(&ctx->e, "line %d: Unable to add default route for interface %s: %s",
                lineno, argv[3], strerror(errno));

            goto error_daemon_route_add;
        }
    } else if (strcmp(argv[1], "station") == 0) {
        int hopc = 0;
        char **hops = NULL;

        if (argc < 5) {
            if (strcmp(argv[3], "if") != 0) {
                patty_error_fmt(&ctx->e, "line %d: Invalid station route declaration: Unexpected keyword '%s'",
                                lineno, argv[3]);

                goto error_invalid_route;
            }
        } else if (argc > 6) {
            if (strcmp(argv[5], "path") != 0) {
                patty_error_fmt(&ctx->e, "line %d: Invalid station route declaration: Unexpected keyword '%s'",
                                lineno, argv[5]);

                goto error_invalid_route;
            }

            if (argc < 7) {
                patty_error_fmt(&ctx->e, "line %d: Invalid station route declaration: No route path provided",
                                lineno);

                goto error_invalid_route;
            }

            hopc = argc - 6;
            hops = &argv[6];
        }

        if (patty_daemon_route_add(ctx->daemon,
                                   argv[4],
                                   argv[2],
                                   (const char **)hops,
                                   hopc) < 0) {
            patty_error_fmt(&ctx->e, "line %d: Unable to add route for interface %s: %s",
                lineno, argv[4], strerror(errno));

            goto error_daemon_route_add;
        }
    } else {
        patty_error_fmt(&ctx->e, "line %d: Invalid route type '%s'",
            lineno, argv[1]);

        goto error_invalid_route;
    }

    return 0;

error_daemon_route_add:
error_invalid_route:
    return -1;
}

struct config_handler {
    const char *name;
    int (*func)(struct context *, int, int, char **);
};

struct config_handler handlers[] = {
    { "sock",  handle_sock   },
    { "pid",   handle_pid    },
    { "if",    handle_if     },
    { "route", handle_route  },
    { NULL,   NULL           }
};

static int handle_config_line(patty_conf_file *file,
                              patty_list *line,
                              void *c) {
    struct context *ctx = c;
    patty_list_item *item  = line->first;

    int argc   = (int)line->length,
        i      = 0,
        ret    = 0,
        lineno = 0;

    char **argv;

    if (!item) {
        return 0;
    }

    if ((argv = malloc((argc + 1) * sizeof(char *))) == NULL) {
        goto error_malloc_argv;
    }

    while (item) {
        patty_conf_token *token = item->value;

        if (lineno == 0) {
            lineno = token->lineno;
        }

        argv[i++] = token->text;

        item = item->next;
    }

    argv[argc] = NULL;

    for (i=0; handlers[i].name; i++) {
        if (strcmp(argv[0], handlers[i].name) == 0) {
            ret = handlers[i].func(ctx, lineno, argc, argv);

            goto done;
        }
    }

    patty_error_fmt(&ctx->e, "line %d: Unknown configuration value '%s'",
        lineno, argv[0]);

done:
    free(argv);

    return ret;

error_malloc_argv:
    return -1;
}

static int handle_standalone(patty_daemon *daemon,
                             int argc,
                             char *argv0,
                             char **argv) {
    patty_ax25_if *iface;
    patty_ax25_addr addr;

    patty_kiss_tnc_info info;
    patty_error e;

    if (patty_daemon_set_sock_path(daemon, argv[0]) < 0) {
        fprintf(stderr, "%s: Unable to set socket path to %s: %s\n",
            argv0, argv[0], strerror(errno));

        goto error_set_sock_path;
    }

    if (patty_ax25_pton(argv[1], &addr) < 0) {
        fprintf(stderr, "%s: Invalid callsign '%s'\n",
            argv0, argv[1]);

        goto error_invalid_callsign;
    }

    if (patty_bin_kiss_config(argc - 2, argv + 2, &info, &e) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv0, argv[2], patty_error_string(&e));

        goto error_kiss_config;
    }

    if ((iface = patty_ax25_if_new(patty_kiss_tnc_driver(),
                                   &info)) == NULL) {
        fprintf(stderr, "%s: Unable to create network interface %s: %s\n",
            argv0, DEFAULT_IFNAME, strerror(errno));

        goto error_if_new;
    } else {
        int fd = patty_ax25_if_fd(iface);
        char *pty;

        if (patty_ax25_if_addr_set(iface, &addr) < 0) {
            fprintf(stderr, "%s: Unable to set address for interface %s: %s\n",
                argv[0], DEFAULT_IFNAME, strerror(errno));

            goto error_if_addr_set;
        }

        if (isatty(fd) && (pty = ptsname(fd)) != NULL) {
            printf("if %s pty %s\n", DEFAULT_IFNAME, pty);
        }

        patty_ax25_if_up(iface);
    }

    if (patty_daemon_if_add(daemon, iface, DEFAULT_IFNAME) < 0) {
        fprintf(stderr, "%s: Unable to add interface %s: %s\n",
            argv0, DEFAULT_IFNAME, strerror(errno));

        goto error_daemon_if_add;
    }

    if (patty_daemon_route_add_default(daemon, DEFAULT_IFNAME) < 0) {
        fprintf(stderr, "%s: Unable to add default route to %s: %s\n",
            argv0, DEFAULT_IFNAME, strerror(errno));

        goto error_daemon_route_add_default;
    }

    return 0;

error_daemon_route_add_default:
error_daemon_if_add:
error_if_addr_set:
    patty_ax25_if_destroy(iface);

error_if_new:
error_kiss_config:
error_invalid_callsign:
error_set_sock_path:
    return -1;
}

enum flags {
    FLAG_FG,
    FLAG_STANDALONE,
    FLAG_COUNT
};

int main(int argc, char **argv) {
    int ret = 0,
        index,
        ch,
        flags[FLAG_COUNT];

    struct option opts[] = {
        { "fg",         no_argument,       &flags[FLAG_FG],          1  },
        { "config",     required_argument, NULL,                    'c' },
        { "standalone", no_argument,       &flags[FLAG_STANDALONE],  1  },
        { NULL,         no_argument,       NULL,                     0  }
    };

    struct context ctx = {
        .config_file = DEFAULT_CONFIG_FILE
    };

    memset(&ctx.e, '\0', sizeof(ctx.e));
    memset(flags, '\0', sizeof(flags));

    if ((ctx.daemon = patty_daemon_new()) == NULL) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_daemon_new()", strerror(errno));

        goto error_daemon_new;
    }

    while ((ch = getopt_long(argc, argv, "fc:s", opts, &index)) >= 0) {
        switch (ch) {
            case 'c':
                ctx.config_file = optarg;
                break;

            case 'f': flags[FLAG_FG]         = 1; break;
            case 's': flags[FLAG_STANDALONE] = 1; break;

            default:
                ret = usage(argc, argv, NULL);
                goto error_invalid_args;
        }
    }

    if (optind == argc) {
        ret = usage(argc, argv, "Too many arguments provided");

        goto error_invalid_args;
    }

    if (flags[FLAG_STANDALONE]) {
        if (argc - optind < 1) {
            ret = usage(argc, argv, "No socket path provided");

            goto error_config;
        } else if (argc - optind < 2) {
            ret = usage(argc, argv, "No callsign provided");

            goto error_config;
        } else if (argc - optind < 3) {
            ret = usage(argc, argv, "No device path provided");

            goto error_config;
        }

        if (handle_standalone(ctx.daemon,
                              argc - optind,
                              argv[0],
                              argv + optind) < 0) {
            goto error_config;
        }
    } else if (patty_conf_read(ctx.config_file, handle_config_line, &ctx) < 0) {
        fprintf(stderr, "%s: %s: %s: %s\n",
            argv[0],
            "patty_conf_read()",
            ctx.config_file,
            patty_error_set(&ctx.e)? patty_error_string(&ctx.e):
                                     strerror(errno));

        goto error_config;
    }

    if (!flags[FLAG_FG]) {
        if (daemon(1, 0) < 0) {
            fprintf(stderr, "%s: %s: %s\n",
                argv[0], "daemon()", strerror(errno));

            goto error_daemon;
        }
    }

    if (patty_daemon_run(ctx.daemon) < 0) {
        fprintf(stderr, "%s: %s: %s\n",
            argv[0], "patty_daemon_run()", strerror(errno));

        goto error_daemon_run;
    }

    patty_daemon_destroy(ctx.daemon);

    return 0;

error_daemon:
error_daemon_run:
error_config:
error_invalid_args:
    patty_daemon_destroy(ctx.daemon);

error_daemon_new:
    return ret;
}
