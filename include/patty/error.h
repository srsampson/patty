#ifndef _PATTY_ERROR_H
#define _PATTY_ERROR_H

#define PATTY_ERROR_STRLEN 256

enum patty_error_state {
    PATTY_ERROR_OK,
    PATTY_ERROR_SET
};

typedef struct _patty_error {
    enum patty_error_state state;
    char err[PATTY_ERROR_STRLEN];
} patty_error;

int patty_error_fmt(patty_error *e, const char *message, ...);

void patty_error_clear(patty_error *e);

int patty_error_set(patty_error *e);

char *patty_error_string(patty_error *e);

#endif /* _PATTY_ERROR_H */
