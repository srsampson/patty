#ifndef _PATTY_LIST_H
#define _PATTY_LIST_H

#include <stdint.h>
#include <sys/types.h>

typedef struct _patty_list_item {
    struct _patty_list_item *prev,
                            *next;

    void *value;
} patty_list_item;

typedef struct _patty_list {
    size_t length;

    patty_list_item *first,
                    *last;
} patty_list;

typedef void (*patty_list_callback)(void *item, void *ctx);

patty_list *patty_list_new();

void patty_list_destroy(patty_list *list);

size_t patty_list_length(patty_list *list);

void *patty_list_set(patty_list *list, off_t index, void *item);

void *patty_list_append(patty_list *list, void *item);

void *patty_list_prepend(patty_list *list, void *item);

void *patty_list_last(patty_list *list);

void *patty_list_pop(patty_list *list);

void *patty_list_shift(patty_list *list);

void *patty_list_splice(patty_list *list, off_t index);

void *patty_list_insert(patty_list *list, off_t index);

void *patty_list_index(patty_list *list, off_t index);

void patty_list_each(patty_list *list,
                     patty_list_callback callback,
                     void *ctx);

#endif /* _PATTY_LIST_H */
