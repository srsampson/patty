#ifndef _PATTY_PRINT_H
#define _PATTY_PRINT_H

int patty_print_frame_header(FILE *fh,
                             const patty_ax25_frame *frame);

int patty_print_params(FILE *fh,
                       const patty_ax25_params *params);

int patty_print_hexdump(FILE *fh, void *data, size_t len);

#endif /* _PATTY_PRINT_H */
