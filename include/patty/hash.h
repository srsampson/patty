#ifndef _PATTY_HASH_H
#define _PATTY_HASH_H

#include <stdint.h>
#include <sys/types.h>

void patty_hash_init(uint32_t *hash);

void patty_hash_data(uint32_t *hash, void *data, size_t len);

void patty_hash_end(uint32_t *hash);

uint32_t patty_hash(void *data, size_t len);

#endif /* _PATTY_HASH_H */
