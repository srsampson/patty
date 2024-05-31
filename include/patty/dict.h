#ifndef _PATTY_DICT_H
#define _PATTY_DICT_H

#include <stdint.h>
#include <sys/types.h>

#define PATTY_DICT_BUCKET_SIZE 16

typedef struct _patty_dict_slot {
    uint32_t key;
    void *value;
    int set;

    struct _patty_dict_slot *next;
} patty_dict_slot;

typedef struct _patty_dict_bucket {
    patty_dict_slot slots[16];
} patty_dict_bucket;

typedef struct _patty_dict {
    patty_dict_bucket bucket;
} patty_dict;

patty_dict *patty_dict_new();

patty_dict_slot *patty_dict_slot_find(patty_dict *dict, uint32_t key);

typedef int (*patty_dict_callback)(uint32_t key,
                                   void *value,
                                   void *ctx);

int patty_dict_each(patty_dict *dict,
                    patty_dict_callback callback,
                    void *ctx); 

void *patty_dict_get(patty_dict *dict, uint32_t key);

void *patty_dict_set(patty_dict *dict, uint32_t key, void *value);

int patty_dict_delete(patty_dict *dict, uint32_t key);

void patty_dict_destroy(patty_dict *dict);

#endif /* _PATTY_DICT_H */
