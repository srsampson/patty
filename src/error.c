#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <patty/error.h>

int patty_error_fmt(patty_error *e, const char *message, ...) {
    va_list args;

    va_start(args, message);

    if (vsnprintf(e->err, sizeof(e->err)-1, message, args) < 0) {
        goto error_vsnprintf;
    }

    va_end(args);

    e->state = PATTY_ERROR_SET;

    return 0;

error_vsnprintf:
    va_end(args);

    return -1;
}

void patty_error_clear(patty_error *e) {
    memset(e->err, '\0', sizeof(e->err));

    e->state = PATTY_ERROR_OK;
}

int patty_error_set(patty_error *e) {
    return e->state == PATTY_ERROR_SET? 1: 0;
}

char *patty_error_string(patty_error *e) {
    return e->err;
}
