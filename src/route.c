#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <patty/hash.h>

#include <patty/ax25.h>

patty_ax25_route *patty_ax25_route_new(patty_ax25_if *iface,
                                       patty_ax25_addr *dest,
                                       patty_ax25_addr *repeaters,
                                       int hops) {
    patty_ax25_route *route;

    int i;

    if (hops >= PATTY_AX25_MAX_HOPS) {
        errno = EOVERFLOW;

        goto error_max_hops;
    }

    if ((route = malloc(sizeof(*route))) == NULL) {
        goto error_malloc_route;
    }

    memset(route, '\0', sizeof(*route));

    route->iface = iface;

    if (dest) {
        patty_ax25_addr_copy(&route->dest, dest, 0);
    }

    for (i=0; i<hops; i++) {
        patty_ax25_addr_copy(&route->repeaters, &repeaters[i], 0);
    }

    return route;

error_malloc_route:
error_max_hops:
    return NULL;
}

patty_ax25_route *patty_ax25_route_new_default(patty_ax25_if *iface) {
    return patty_ax25_route_new(iface, NULL, NULL, 0);
}
    
patty_ax25_route_table *patty_ax25_route_table_new() {
    return patty_dict_new();
}

void patty_ax25_route_table_destroy(patty_ax25_route_table *table) {
    patty_dict_destroy(table);
}

patty_ax25_route *patty_ax25_route_table_find(patty_ax25_route_table *table,
                                              patty_ax25_addr *dest) {
    patty_ax25_route *route;
    uint32_t hash;

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, dest);
    patty_hash_end(&hash);

    route = patty_dict_get(table, hash);

    if (route) {
        return route;
    }

    return patty_ax25_route_table_default(table);
}

patty_ax25_route *patty_ax25_route_table_default(patty_ax25_route_table *table) {
    patty_ax25_addr empty;

    uint32_t hash;

    memset(&empty, '\0', sizeof(empty));

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, &empty);
    patty_hash_end(&hash);

    return patty_dict_get(table, hash);
}

struct dict_ctx_wrapper {
    int (*callback)(patty_ax25_route *route, void *);
    void *ctx;
};

static int each_dict_callback(uint32_t key, void *value, void *ctx) {
    struct dict_ctx_wrapper *wrapper = ctx;

    return wrapper->callback(value, wrapper->ctx);
}

int patty_ax25_route_table_each(patty_ax25_route_table *table,
                                int (*callback)(patty_ax25_route *, void *),
                                void *ctx) {
    struct dict_ctx_wrapper wrapper = {
        .callback = callback,
        .ctx      = ctx
    };

    return patty_dict_each(table, each_dict_callback, &wrapper);
}

int patty_ax25_route_table_add(patty_ax25_route_table *table,
                               patty_ax25_route *route) {
    uint32_t hash;

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, &route->dest);
    patty_hash_end(&hash);

    if (patty_ax25_route_table_find(table, &route->dest) != NULL) {
        errno = EEXIST;

        goto error_exists;
    }

    if (patty_dict_set(table, hash, route) == NULL) {
        goto error_dict_set;
    }

    return 0;

error_dict_set:
error_exists:
    return -1;
}

int patty_ax25_route_table_delete(patty_ax25_route_table *route,
                                  patty_ax25_addr *dest) {
    uint32_t hash;

    patty_hash_init(&hash);
    patty_ax25_addr_hash(&hash, dest);
    patty_hash_end(&hash);

    return patty_dict_delete(route, hash);
}
