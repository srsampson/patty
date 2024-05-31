#include <stdlib.h>
#include <string.h>

#include <patty/dict.h>

patty_dict *patty_dict_new() {
    patty_dict *dict;

    if ((dict = malloc(sizeof(*dict))) == NULL) {
        goto error_malloc_dict;
    }

    memset(dict, 0x00, sizeof(*dict));

    return dict;

error_malloc_dict:
    return NULL;
}

patty_dict_slot *patty_dict_slot_find(patty_dict *dict, uint32_t key) {
    patty_dict_bucket *bucket = &dict->bucket;

    int collisions;

    for (collisions = 0; collisions < 7; collisions++) {
        uint32_t mask  = 0x0f << (4 * collisions);
        uint8_t  index = (key & mask) >> (4 * collisions);

        patty_dict_slot *slot = &bucket->slots[index];

        if (!slot->set) {
            /*
             * In this case, we have determined that there is no value in the
             * dict for the given key.
             */
            return NULL;
        } else if (slot->key == key) {
            /*
             * We have found the desired slot, so return that.
             */
            return slot;
        }

        /*
         * Otherwise, look for the next bucket, if present.
         */
        bucket = (patty_dict_bucket *)slot->next;

        /*
         * If there is no next bucket available, then return null.
         */
        if (bucket == NULL) {
            return NULL;
        }
    }

    return NULL;
}

static int bucket_each_slot(int level,
                            patty_dict_bucket *bucket,
                            patty_dict_callback callback,
                            void *ctx) {
    int i;

    if (level > 7) {
        return 0;
    }

    for (i=0; i<PATTY_DICT_BUCKET_SIZE; i++) {
        patty_dict_slot *slot = &bucket->slots[i];

        if (slot->set) {
            if (callback(slot->key, slot->value, ctx) < 0) {
                goto error_callback;
            }
        }

        if (slot->next) {
            if (bucket_each_slot(level+1,
                                 (patty_dict_bucket *)slot->next,
                                 callback,
                                 ctx) < 0) {
                goto error_next_slot;
            }
        }
    }

    return 0;

error_callback:
error_next_slot:
    return -1;
}

int patty_dict_each(patty_dict *dict, patty_dict_callback callback, void *ctx) {
    return bucket_each_slot(0, &dict->bucket, callback, ctx);
}

void *patty_dict_get(patty_dict *dict, uint32_t key) {
    patty_dict_slot *slot;

    if ((slot = patty_dict_slot_find(dict, key)) == NULL) {
        return NULL;
    }

    return slot->value;
}

void *patty_dict_set(patty_dict *dict,
                     uint32_t key,
                     void *value) {
    patty_dict_bucket *bucket = &dict->bucket;

    int collisions;

    for (collisions = 0; collisions < 7; collisions++) {
        uint32_t mask  = 0x0f << (4 * collisions);
        uint8_t  index = (key & mask) >> (4 * collisions);

        patty_dict_slot *slot = &bucket->slots[index];

        if (!slot->set) {
            /*
             * We have found an empty slot, so let's store the value.
             */
            slot->key   = key;
            slot->value = value;
            slot->set   = 1;

            return value;
        } else if (slot->key == key) {
            /*
             * Otherwise, we've found an existing slot, so let's update that
             * and bail.
             */
            return slot->value = value;
        }

        /*
         * Take a look to see if there is a next bucket in the chain.
         */
        bucket = (patty_dict_bucket *)slot->next;

        /*
         * If there is no next bucket available, then create one.
         */
        if (bucket == NULL) {
            if ((bucket = malloc(sizeof(*bucket))) == NULL) {
                goto error_malloc_bucket;
            }

            memset(bucket, 0x00, sizeof(*bucket));

            slot->next = (patty_dict_slot *)bucket;
        }
    }

    return NULL;

error_malloc_bucket:
    return NULL;
}

int patty_dict_delete(patty_dict *dict, uint32_t key) {
    patty_dict_slot *slot;

    if ((slot = patty_dict_slot_find(dict, key)) == NULL) {
        goto error_dict_slot_find;
    }

    memset(slot, '\0', sizeof(*slot));

    return 0;

error_dict_slot_find:
    return -1;
}

void patty_dict_destroy(patty_dict *dict) {
    free(dict);
}
