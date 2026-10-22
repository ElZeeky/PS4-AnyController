/*
 * host/hid_capture.h - raw HID capture for the PC tool.
 *
 * Finds a controller the OS exposes as HID (SetupAPI on Windows, /dev/hidraw
 * on Linux), reads its input reports for a few seconds, and writes them next
 * to the executable in the format the payload's xcpcapture log uses.
 */
#ifndef XCP_HID_CAPTURE_H
#define XCP_HID_CAPTURE_H

#include <stddef.h>

/* 1 when raw HID capture is available in this build. */
int xcp_hid_capture_available(void);

/*
 * Scan for a controller, let the user pick when there are several, capture its
 * reports for `seconds` (0 means the default) and write the dump next to the
 * executable. `out_path` receives the file that was written. Returns 0 on
 * success.
 */
int xcp_hid_capture_auto(int seconds, char *out_path, size_t out_path_size);

#endif /* XCP_HID_CAPTURE_H */
