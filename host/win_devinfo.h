/*
 * host/win_devinfo.h - list the USB/HID devices Windows is willing to talk about.
 *
 * Deliberately dependency free: no hidapi, no libusb, no Zadig. Its whole job is
 * to answer "what is actually plugged in and how does it identify itself", which
 * is the first thing we need when a new controller shows up.
 *
 * On non-Windows platforms it compiles to a stub that reports "unavailable", so
 * the CLI still builds everywhere.
 */
#ifndef XCP_WIN_DEVINFO_H
#define XCP_WIN_DEVINFO_H

#include <stdint.h>

#define XCP_DEV_INSTANCE_MAX 256
#define XCP_DEV_DESC_MAX     160
#define XCP_DEV_GROUP_MAX    24

typedef struct xcp_dev_entry {
    /* 0 when the instance id had no VID_/PID_ tag (e.g. a root hub). */
    uint16_t vid;
    uint16_t pid;
    int      has_ids;
    char     group[XCP_DEV_GROUP_MAX];  /* which enumeration pass found it */
    char     instance[XCP_DEV_INSTANCE_MAX];
    char     desc[XCP_DEV_DESC_MAX];
} xcp_dev_entry;

/*
 * Fill up to `max` entries. Returns the count, or a negative value when
 * enumeration is unavailable on this platform.
 *
 * Both the HID class and raw USB devices are enumerated on Windows, because an
 * Xbox-format pad in GIP mode is bound to the Xbox driver and appears as a USB
 * device rather than a HID one.
 */
int xcp_enumerate_devices(xcp_dev_entry *out, int max);

/* 1 when this build can enumerate. */
int xcp_enumerate_available(void);

#endif /* XCP_WIN_DEVINFO_H */
