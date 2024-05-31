#include <patty/util.h>

ssize_t patty_strlcpy(char *dest, const char *src, size_t n) {
    size_t i;

    for (i=0; i<n-1; i++) {
        dest[i] = src[i];

        if (src[i] == '\0') {
            break;
        }
    }

    dest[i] = '\0';

    return i;
}
