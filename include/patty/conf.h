#ifndef _PATTY_CONF_H
#define _PATTY_CONF_H

#include <patty/list.h>

typedef struct _patty_conf_file patty_conf_file;

typedef struct _patty_conf_token {
    char *text;

    size_t lineno,
           column,
           len;
} patty_conf_token;

typedef int (*patty_conf_handler)(patty_conf_file *, patty_list *, void *);

int patty_conf_read(const char *file,
                    patty_conf_handler handler,
                    void *ctx);

#endif /* _PATTY_CONF_H */
