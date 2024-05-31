#include <stdio.h>
#include <inttypes.h>

#include <patty/ax25.h>
#include <patty/print.h>

#define printable(c) \
    (c >= 0x20 && c < 0x7f)

static char *frame_type(enum patty_ax25_frame_type type) {
    switch (type) {
        case PATTY_AX25_FRAME_I:     return "I";
        case PATTY_AX25_FRAME_RR:    return "RR";
        case PATTY_AX25_FRAME_RNR:   return "RNR";
        case PATTY_AX25_FRAME_REJ:   return "REJ";
        case PATTY_AX25_FRAME_SREJ:  return "SREJ";
        case PATTY_AX25_FRAME_SABM:  return "SABM";
        case PATTY_AX25_FRAME_SABME: return "SABME";
        case PATTY_AX25_FRAME_DISC:  return "DISC";
        case PATTY_AX25_FRAME_DM:    return "DM";
        case PATTY_AX25_FRAME_UA:    return "UA";
        case PATTY_AX25_FRAME_FRMR:  return "FRMR";
        case PATTY_AX25_FRAME_UI:    return "UI";
        case PATTY_AX25_FRAME_XID:   return "XID";
        case PATTY_AX25_FRAME_TEST:  return "TEST";

        default:
            break;
    }

    return "unknown";
}

static char *frame_cr(enum patty_ax25_frame_cr cr) {
    switch (cr) {
        case PATTY_AX25_FRAME_COMMAND:  return "cmd";
        case PATTY_AX25_FRAME_RESPONSE: return "res";

        default:
            break;
    }

    return "?";
}

static int print_addr(FILE *fh, const patty_ax25_addr *addr) {
    char buf[PATTY_AX25_ADDRSTRLEN+1];

    if (patty_ax25_ntop(addr, buf, sizeof(buf)) < 0) {
        goto error_ntop;
    }

    fprintf(fh, "%s", buf);

    return 0;

error_ntop:
    return -1;
}

static int print_frame_addrs(FILE *fh, const patty_ax25_frame *frame) {
    int i;

    print_addr(fh, &frame->src);

    fprintf(fh, ">");

    print_addr(fh, &frame->dest);

    for (i=0; i<frame->hops; i++) {
        fprintf(fh, ",");
        print_addr(fh, &frame->repeaters[i]);
    }

    return 0;
}

int patty_print_frame_header(FILE *fh,
                             const patty_ax25_frame *frame) {
    if (print_frame_addrs(fh, frame) < 0) {
        goto error_io;
    }

    if (PATTY_AX25_FRAME_CONTROL_I(frame->control)) {
        if (fprintf(fh, " (%s %s N(R)=%d N(S)=%d P/F=%d info=%zu)",
                frame_type(frame->type),
                frame_cr(frame->cr),
                (int)frame->nr,
                (int)frame->ns,
                (int)frame->pf,
                frame->infolen) < 0) {
            goto error_io;
        }
    } else if (PATTY_AX25_FRAME_CONTROL_U(frame->control)) {
        if (fprintf(fh, " (%s %s P/F=%d",
                frame_type(frame->type),
                frame_cr(frame->cr),
                (int)frame->pf) < 0) {
            goto error_io;
        }

        if (PATTY_AX25_FRAME_CONTROL_UI(frame->control)) {
            if (fprintf(fh, " info=%zu", frame->infolen) < 0) {
                goto error_io;
            }
        }

        if (fprintf(fh, ")") < 0) {
            goto error_io;
        }
    } else if (PATTY_AX25_FRAME_CONTROL_S(frame->control)) {
        if (fprintf(fh, " (%s %s N(R)=%d P/F=%d)",
                frame_type(frame->type),
                frame_cr(frame->cr),
                (int)frame->nr,
                (int)frame->pf) < 0) {
            goto error_io;
        }
    }

    return fprintf(fh, "\n");

error_io:
    return -1;
}

int patty_print_params(FILE *fh,
                       const patty_ax25_params *params) {
    static const struct {
        enum patty_ax25_param_classes flag;
        char *name;
    } classes_flags[] = {
        { PATTY_AX25_PARAM_CLASSES_ABM,         "ABM" },
        { PATTY_AX25_PARAM_CLASSES_HALF_DUPLEX, "half-duplex" },
        { PATTY_AX25_PARAM_CLASSES_FULL_DUPLEX, "full-duplex" },
        { 0, NULL }
    };

    static const struct {
        enum patty_ax25_param_hdlc flag;
        char *name;
    } hdlc_flags[] = {
        { PATTY_AX25_PARAM_HDLC_REJ,        "REJ" },
        { PATTY_AX25_PARAM_HDLC_SREJ,       "SREJ" },
        { PATTY_AX25_PARAM_HDLC_XADDR,      "extended addresses" },
        { PATTY_AX25_PARAM_HDLC_MODULO_8,   "modulo 8" },
        { PATTY_AX25_PARAM_HDLC_MODULO_128, "modulo 128" },
        { PATTY_AX25_PARAM_HDLC_TEST,       "TEST" },
        { PATTY_AX25_PARAM_HDLC_FCS_16,     "16-bit FCS" },
        { PATTY_AX25_PARAM_HDLC_SYNC_TX,    "synchronous TX" },
        { PATTY_AX25_PARAM_HDLC_SREJ_MULTI, "multiple SREJ" },
        { 0, NULL }
    };

    struct {
        enum patty_ax25_param_type flag;
        size_t value;
        char *name;
    } fields[] = {
        { PATTY_AX25_PARAM_INFO_TX,   params->info_tx >> 3, "I Field Length TX" },
        { PATTY_AX25_PARAM_INFO_RX,   params->info_rx >> 3, "I Field Length RX" },
        { PATTY_AX25_PARAM_WINDOW_TX, params->window_tx,    "Window Size TX" },
        { PATTY_AX25_PARAM_WINDOW_RX, params->window_rx,    "Window Size RX" },
        { PATTY_AX25_PARAM_ACK,       params->ack,          "Ack timer" },
        { PATTY_AX25_PARAM_RETRY,     params->retry,        "Retry" },
        { 0, 0, NULL }
    };

    int i;

    if (fprintf(fh, "    XID parameters:\n") < 0) {
        goto error_fprintf;
    }

    if (fprintf(fh, "        Classes of procedures:\n") < 0) {
        goto error_fprintf;
    }

    for (i=0; classes_flags[i].name; i++) {
        if (params->classes & classes_flags[i].flag) {
            if (fprintf(fh, "            > %s\n", classes_flags[i].name) < 0) {
                goto error_fprintf;
            }
        }
    }

    fprintf(fh, "        HDLC optional functions:\n");

    for (i=0; hdlc_flags[i].name; i++) {
        if (params->hdlc & hdlc_flags[i].flag) {
            if (fprintf(fh, "            > %s\n", hdlc_flags[i].name) < 0) {
                goto error_fprintf;
            }
        }
    }

    for (i=0; fields[i].name; i++) {
        if (params->flags & (1 << fields[i].flag)) {
            if (fprintf(fh, "        %s: %zu\n", fields[i].name, fields[i].value) < 0) {
                goto error_fprintf;
            }
        }
    }

    return 0;

error_fprintf:
    return -1;
}

int patty_print_hexdump(FILE *fh, void *data, size_t len) {
    size_t i;

    for (i=0; i<len; i+=16) {
        size_t x;

        if (fprintf(fh, "%08zx:", i) < 0) {
            goto error_io;
        }

        for (x=0; x<16; x++) {
            if (!(x & 1)) {
                if (fprintf(fh, " ") < 0) {
                    goto error_io;
                }
            }

            if (i+x < len) {
                if (fprintf(fh, "%02x", ((uint8_t *)data)[i+x]) < 0) {
                    goto error_io;
                }
            } else {
                if (fprintf(fh, "  ") < 0) {
                    goto error_io;
                }
            }
        }

        if (fprintf(fh, "  ") < 0) {
            goto error_io;
        }

        for (x=0; x<16 && i+x<len; x++) {
            uint8_t c = ((uint8_t *)data)[i+x];

            if (fputc(printable(c)? c: '.', fh) < 0) {
                goto error_io;
            }
        }

        if (fprintf(fh, "\n") < 0) {
            goto error_io;
        }
    }

    return 0;

error_io:
    return -1;
}
