#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>

#include <patty/ax25.h>

enum addr_state {
    ADDR_CALLSIGN,
    ADDR_SSID
};

int patty_ax25_pton(const char *callsign,
                    patty_ax25_addr *addr) {
    size_t len = strlen(callsign);

    int i,
        o      = 0,
        digits = 0;

    int ssid = 0;

    enum addr_state state = ADDR_CALLSIGN;

    uint8_t c = '\0';

    if (len == 0) {
        goto error_invalid_callsign;
    } else if (len > PATTY_AX25_ADDRSTRLEN) {
        len = PATTY_AX25_ADDRSTRLEN;
    }

    for (i=0; i<len; i++) {
        c = callsign[i];

        switch (state) {
            case ADDR_CALLSIGN:
                if (o > PATTY_AX25_CALLSTRLEN) {
                    goto error_invalid_callsign;
                }

                if (c == '-') {
                    state = ADDR_SSID;
                } else if (PATTY_AX25_ADDR_CHAR_VALID(c)) {
                    addr->callsign[o++] = (c & 0x7f) << 1;
                } else {
                    goto error_invalid_callsign;
                }

                break;

            case ADDR_SSID:
                if (digits == 2) {
                    goto error_invalid_callsign;
                }

                if (c >= '0' && c <= '9') {
                    ssid *= 10;
                    ssid += c - '0';

                    digits++;
                } else {
                    goto error_invalid_callsign;
                }
        }
    }

    if (ssid > 15) {
        goto error_invalid_callsign;
    }

    if (c == '-') {
        goto error_invalid_callsign;
    }

    while (o < PATTY_AX25_CALLSTRLEN) {
        addr->callsign[o++] = ' ' << 1;
    }

    addr->ssid = (ssid & 0x0f) << 1;

    return 0;

error_invalid_callsign:
    errno = EINVAL;

    return -1;
}

static inline int expt(int base, int e) {
    int ret = base,
        i;

    if (e == 0) {
        return 1;
    }

    for (i=1; i<e; i++) {
        ret *= base;
    }

    return ret;
}

int patty_ax25_ntop(const patty_ax25_addr *addr,
                    char *dest,
                    size_t len) {
    int i,
        o = 0;

    int ssid = (addr->ssid & 0x1e) >> 1;

    for (i=0; i<PATTY_AX25_CALLSTRLEN; i++) {
        uint8_t c;

        if (o == len) {
            goto error;
        }

        c = (addr->callsign[i] & 0xfe) >> 1;

        if (c == ' ') {
            break;
        }

        dest[o++] = c;
    }

    if (ssid) {
        int digits = ssid > 9? 2: 1,
            d;

        if (o + 1 + digits > len) {
            goto error;
        }

        dest[o++] = '-';

        for (d=0; d<digits; d++) {
            int place = expt(10, digits - d - 1),
                num   = ((int)ssid % (place * 10)) / place;

            dest[o++] = num + '0';
        }
    }

    if (o > len) {
        goto error;
    }

    dest[o] = '\0';

    return 0;

error:
    errno = EOVERFLOW;

    return -1;
}

static inline void hash_byte(uint32_t *hash, uint8_t c) {
    *hash += c;
    *hash += (*hash << 10);
    *hash ^= (*hash >>  6);
}

void patty_ax25_addr_hash(uint32_t *hash, const patty_ax25_addr *addr) {
    size_t i;

    for (i=0; i<PATTY_AX25_CALLSTRLEN; i++) {
        hash_byte(hash, addr->callsign[i] >> 1);
    }

    hash_byte(hash, PATTY_AX25_ADDR_SSID_NUMBER(addr->ssid));
}

size_t patty_ax25_addr_copy(void *buf,
                            patty_ax25_addr *addr,
                            uint8_t ssid_flags) {
    size_t encoded = 0;

    memcpy(buf, addr->callsign, sizeof(addr->callsign));

    encoded += sizeof(addr->callsign);

    ((uint8_t *)buf)[encoded++] = ssid_flags | 0x60 | (addr->ssid & 0x1e);

    return encoded;
}
