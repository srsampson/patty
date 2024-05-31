#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <patty/list.h>
#include <patty/conf.h>

enum state {
    STRING,
    STRING_SINGLE_QUOTE,
    STRING_DOUBLE_QUOTE,
    DELIM,
    COMMENT
};

enum escape {
    ESCAPE_OFF,
    ESCAPE_ON
};

struct _patty_conf_file {
    const char *path;
    size_t size;
    void *buf;
};

static int file_read(patty_conf_file *file, const char *path) {
    int fd;
    struct stat st;

    size_t offset,
           left;

    if ((fd = open(path, O_RDONLY)) < 0) {
        goto error_open;
    }

    if (fstat(fd, &st) < 0) {
        goto error_fstat;
    }

    if ((file->buf = malloc(st.st_size)) == NULL) {
        goto error_malloc_buf;
    }

    offset = 0;
    left   = st.st_size;

    while (1) {
        ssize_t len;

        if ((len = read(fd,
                        (uint8_t *)file->buf + offset,
                        left > st.st_blksize? st.st_blksize: left)) < 0) {
            goto error_read;
        } else if (len < st.st_blksize) {
            break;
        } else {
            offset += len;
            left   -= len;
        }
    }

    (void)close(fd);

    file->size = st.st_size;
    file->path = path;

    return 0;

error_read:
    free(file->buf);

error_malloc_buf:
error_fstat:
    (void)close(fd);

error_open:
    return -1;
}

static void file_free(patty_conf_file *file) {
    free(file->buf);
}

static void token_start(patty_conf_token *token,
                        char *text,
                        size_t lineno,
                        size_t column) {
    token->text   = text;
    token->lineno = lineno;
    token->column = column;
    token->len    = 0;
}

static int token_save(patty_list *list, patty_conf_token *token) {
    patty_conf_token *copy;

    if ((copy = malloc(sizeof(*token))) == NULL) {
        goto error_malloc;
    }

    memcpy(copy, token, sizeof(*copy));

    memset(token, '\0', sizeof(*token));

    if (patty_list_append(list, copy) == NULL) {
        goto error_list_append;
    }

    return 0;

error_list_append:
    free(copy);

error_malloc:
    return -1;
}

static void line_destroy(patty_list *list) {
    patty_list_item *item = list->first;

    while (item) {
        patty_list_item *next = item->next;

        free(item->value);
        free(item);

        item = next;
    }

    free(list);
}

static inline int start_of_string(enum state old, enum state state) {
    switch (state) {
        case STRING:
        case STRING_SINGLE_QUOTE:
        case STRING_DOUBLE_QUOTE:
            if (old == DELIM) {
                return 1;
            }

        default:
            break;
    }

    return 0;
}

static inline int end_of_string(enum state old, enum state state) {
    switch (old) {
        case STRING:
        case STRING_SINGLE_QUOTE:
        case STRING_DOUBLE_QUOTE:
            if (state == DELIM || state == COMMENT) {
                return 1;
            }

        default:
            break;
    }

    return 0;
}

static inline int within_string(enum state old, enum state state) {
    switch (state) {
        case STRING:
            if (old != STRING_SINGLE_QUOTE && old != STRING_DOUBLE_QUOTE) {
                return 1;
            }

            break;

        case STRING_SINGLE_QUOTE:
        case STRING_DOUBLE_QUOTE:
            if (old == state) {
                return 1;
            }

        default:
            break;
    }

    return 0;
}

static inline int char_delim(uint8_t c) {
    return (c == ' ' || c == '\r' || c == '\n' || c == '\t')? 1: 0;
}

int patty_conf_read(const char *path,
                    patty_conf_handler handler,
                    void *ctx) {
    patty_conf_file file;

    patty_list *line;
    patty_conf_token token;

    enum state state   = STRING;
    enum escape escape = ESCAPE_OFF;

    size_t i,
           o,
           lineno = 1,
           column = 0;

    uint8_t last = '\0';

    if (file_read(&file, path) < 0) {
        goto error_file_read;
    }

    if ((line = patty_list_new()) == NULL) {
        goto error_list_new;
    }

    token_start(&token, file.buf, lineno, column);

    for (i=0, o=0; i<file.size; i++) {
        enum state old = state;

        uint8_t c = ((uint8_t *)file.buf)[i];

        if (c == '\n') {
            lineno++;
            column = 1;
        } else {
            column++;
        }

        switch (state) {
            case STRING:
                if (c == '\\' && escape == ESCAPE_OFF) {
                    escape = ESCAPE_ON;
                } else if (char_delim(c)) {
                    state = DELIM;
                } else if (c == '\'') {
                    state = STRING_SINGLE_QUOTE;
                } else if (c == '"') { 
                    state = STRING_DOUBLE_QUOTE;
                } else if (c == '#') {
                    state = COMMENT;
                }

                break;

            case STRING_SINGLE_QUOTE:
                if (c == '\'') {
                    state = STRING;
                }

                break;

            case STRING_DOUBLE_QUOTE:
                if (c == '\\' && escape == ESCAPE_OFF) {
                    escape = ESCAPE_ON;
                } else if (c == '"' && escape == ESCAPE_OFF) {
                    state = STRING;
                }

                break;

            case DELIM:
                if (c == '\\' && escape == ESCAPE_OFF) {
                    escape = ESCAPE_ON;
                } else if (c == '\'') {
                    state = STRING_SINGLE_QUOTE;
                } else if (c == '"') {
                    state = STRING_DOUBLE_QUOTE;
                } else if (c == '#') {
                    state = COMMENT;
                } else if (!char_delim(c)) {
                    state = STRING;
                }

                break;

            case COMMENT:
                if (c == '\n') {
                    state = DELIM;
                }

                break;
        }

        if (start_of_string(old, state)) {
            token_start(&token,
                        (char *)file.buf + o,
                        lineno,
                        column);
        }

        if (within_string(old, state)) {
            if (escape == ESCAPE_OFF) {
                ((char *)file.buf)[o++] = c;

                token.len++;
            }
        } else if (end_of_string(old, state)) {
            ((char *)file.buf)[o++] = '\0';
            
            if (token_save(line, &token) < 0) {
                goto error_token_save;
            }
        }

        if (c == '\n' && escape == ESCAPE_OFF) {
            if (line->length > 0) {
                if (handler(&file, line, ctx) < 0) {
                    goto error_handler;
                }

                line_destroy(line);

                if ((line = patty_list_new()) == NULL) {
                    goto error_list_new;
                }
            }
        }

        if (escape == ESCAPE_ON && last == '\\') {
            escape =  ESCAPE_OFF;
        }

        last = c;
    }

    if (token.len) {
        ((char *)file.buf)[o++] = '\0';
            
        if (token_save(line, &token) < 0) {
            goto error_token_save;
        }
    }

    if (line->length > 0) {
        if (handler(&file, line, ctx) < 0) {
            goto error_handler;
        }
    }

    line_destroy(line);

    file_free(&file);

    return 0;

error_handler:
error_token_save:
    patty_list_destroy(line);

error_list_new:
    file_free(&file);

error_file_read:
    return -1;
}
