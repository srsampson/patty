#ifndef _PATTY_AX25_ROUTE_H
#define _PATTY_AX25_ROUTE_H

typedef struct _patty_ax25_route {
    patty_ax25_if *iface;

    patty_ax25_addr dest,
                    repeaters[PATTY_AX25_MAX_HOPS];

    size_t hops;
} patty_ax25_route;

typedef struct _patty_dict patty_ax25_route_table;

patty_ax25_route *patty_ax25_route_new(patty_ax25_if *iface,
                                       patty_ax25_addr *dest,
                                       patty_ax25_addr *repeaters,
                                       int hops);

patty_ax25_route *patty_ax25_route_new_default(patty_ax25_if *iface);
    
patty_ax25_route_table *patty_ax25_route_table_new();

void patty_ax25_route_table_destroy(patty_ax25_route_table *table);

int patty_ax25_route_table_add(patty_ax25_route_table *table,
                               patty_ax25_route *route);

int patty_ax25_route_table_delete(patty_ax25_route_table *route,
                                  patty_ax25_addr *dest);

patty_ax25_route *patty_ax25_route_table_find(patty_ax25_route_table *table,
                                              patty_ax25_addr *dest);

patty_ax25_route *patty_ax25_route_table_default(patty_ax25_route_table *table);

int patty_ax25_route_table_each(patty_ax25_route_table *table,
                                int (*callback)(patty_ax25_route *, void *),
                                void *ctx);

#endif /* _PATTY_AX25_ROUTE_H */
