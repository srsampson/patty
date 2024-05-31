#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <patty/list.h>

patty_list *patty_list_new() {
    patty_list *list;

    if ((list = malloc(sizeof(*list))) == NULL) {
        goto error_malloc_list;
    }

    list->length = 0;
    list->first  = NULL;
    list->last   = NULL;

    return list;

error_malloc_list:
    return NULL;
}

void patty_list_destroy(patty_list *list) {
    patty_list_item *item = list->first;

    while (item != NULL) {
        patty_list_item *next = (patty_list_item *)item->next;

        free(item);

        item = next;
    }

    free(list);
}

size_t patty_list_length(patty_list *list) {
    return list->length;
}

void *patty_list_set(patty_list *list, off_t index, void *value) {
    patty_list_item *item = list->first;
    size_t i = 0;

    while (item != NULL) {
        if (i++ == index) {
            return item->value = value;
        }

        item = item->next;
    }

    return NULL;
}

void *patty_list_append(patty_list *list, void *value) {
    patty_list_item *new;

    if ((new = malloc(sizeof(*new))) == NULL) {
        goto error_malloc_item;
    }

    new->value = value;
    new->next  = NULL;

    if (list->first == NULL) {
        list->first = new;
        list->last  = new;
    } else {
        list->last->next = new;
        list->last       = new;
    }

    list->length++;

    return value;

error_malloc_item:
    return NULL;
}

void *patty_list_last(patty_list *list) {
    patty_list_item *item = list->first;

    while (item->next != NULL) {
        item = item->next;
    }

    return item->value;
}

void *patty_list_pop(patty_list *list) {
    void *value;
    patty_list_item *item = list->last;

    if (item == NULL) {
        return NULL;
    }

    if (list->last->prev) {
        list->last       = list->last->prev;
        list->last->next = NULL;
    } else {
        list->first = NULL;
        list->last  = NULL;
    }

    list->length--;

    value = item->value;

    free(item);

    return value;
}

void *patty_list_shift(patty_list *list) {
    void *value;
    patty_list_item *item = list->first;

    if (item == NULL) {
        return NULL;
    }

    if (list->first->next) {
        list->first       = list->first->next;
        list->first->prev = NULL;
    } else {
        list->first = NULL;
        list->last  = NULL;
    }

    list->length--;

    value = item->value;

    free(item);

    return value;
}

void *patty_list_splice(patty_list *list, off_t index) {
    patty_list_item *item = list->first;
    size_t i = 0;

    while (index < 0) {
        index += list->length;
    }

    while (item) {
        if (i == index) {
            if (item->prev && item->next) {
                item->prev->next = item->next;
            } else if (item->prev) {
                list->last       = item->prev;
                item->prev->next = NULL;
            } else if (item->next) {
                list->first      = item->next;
                item->next->prev = NULL;
            }

            return item->value;
        }

        item = item->next;
        i++;
    }

    return NULL;
}

void *patty_list_index(patty_list *list, off_t index) {
    patty_list_item *item = list->first;
    size_t i = 0;

    if (index < 0) {
        index += list->length;

    }

    if (index >= list->length || index < 0) {
        return NULL;
    }

    while (i < index && item->next != NULL) {
        item = item->next;
        i++;
    }

    return item->value;
}

void patty_list_each(patty_list *list, patty_list_callback callback, void *ctx) {
    patty_list_item *item;

    for (item = list->first; item != NULL; item = item->next) {
        callback(item->value, ctx);
    }
}
