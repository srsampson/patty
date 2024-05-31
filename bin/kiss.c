#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <patty/ax25.h>
#include <patty/bin/kiss.h>

int patty_bin_kiss_config(int argc,
                          char **argv,
                          patty_kiss_tnc_info *info,
                          patty_error *e) {
    int i;

    memset(info, '\0', sizeof(*info));

    info->flags |= PATTY_KISS_TNC_DEVICE;
    info->device = argv[0];

    for (i=1; i<argc; i++) {
        if (strcmp(argv[i], "crtscts") == 0) {
            info->flags |= PATTY_KISS_TNC_FLOW;
            info->flow   = PATTY_KISS_TNC_FLOW_CRTSCTS;
        } else if (strcmp(argv[i], "xonxoff") == 0) {
            info->flags |= PATTY_KISS_TNC_FLOW;
            info->flow   = PATTY_KISS_TNC_FLOW_XONXOFF;
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            info->flags |= PATTY_KISS_TNC_BAUD;
            info->baud   = atoi(argv[i]);
        } else {
            patty_error_fmt(e, "Invalid KISS TNC device parameter '%s'",
                argv[i]);

            goto error_invalid_device_setting;
        }
    }

    return 0;

error_invalid_device_setting:
    return -1;
}
