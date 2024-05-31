#include <stdlib.h>
#include <string.h>

#include <patty/hash.h>

void patty_hash_init(uint32_t *hash) {
    *hash = 0xffffffdf;
}

void patty_hash_data(uint32_t *hash, void *data, size_t len) {
    size_t i;

    for (i=0; i<len; i++) {
        uint8_t c = ((char *)data)[i];

        *hash += c;
        *hash += (*hash << 10);
        *hash ^= (*hash >>  6);
    }
}

void patty_hash_end(uint32_t *hash) {
    *hash += (*hash <<  3);
    *hash ^= (*hash >> 11);
    *hash += (*hash << 15);
}

uint32_t patty_hash(void *data, size_t len) {
    uint32_t hash;

    patty_hash_init(&hash);
    patty_hash_data(&hash, data, len);
    patty_hash_end(&hash);

    return hash;
}
