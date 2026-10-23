/*
 * xcp/usb_backend.h - the "usb backend" layer.
 *
 * One interface, three implementations:
 *   - mock    : scripted reports, always built, used by the PC tests
 *   - hidapi  : real hardware on the PC (Windows HID stack, no driver swap)
 *   - sceusbd : real hardware on the PS4 (libSceUsbd)
 *
 * Nothing above this layer knows which one is in use.
 */
#ifndef XCP_USB_BACKEND_H
#define XCP_USB_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "xcp/report.h"
#include "xcp/hid_generic.h"

/* Upper bound on a report we are willing to receive. */
#define XCP_USB_MAX_REPORT 64

/* Longest device path/identifier a backend may hand back. */
#define XCP_USB_PATH_MAX 256
#define XCP_USB_NAME_MAX 128

typedef struct xcp_usb_device_info {
    uint16_t            vid;
    uint16_t            pid;
    xcp_controller_type type;             /* XCP_CTRL_NONE if we can't drive it */
    uint8_t             iface_class;      /* chosen controller interface        */
    uint8_t             iface_subclass;
    uint8_t             iface_protocol;
    uint8_t             iface_number;
    uint8_t             has_interrupt_in; /* 1 when the interface can be read   */
    char                path[XCP_USB_PATH_MAX]; /* opaque, backend specific      */
    char                name[XCP_USB_NAME_MAX]; /* human readable, may be empty  */
} xcp_usb_device_info;

/* Return values shared by every backend call. */
#define XCP_USB_OK          0
#define XCP_USB_NO_DATA     1   /* nothing pending right now, try again later */
#define XCP_USB_DISCONNECT  (-2)
#define XCP_USB_ERROR       (-1)

/*
 * Status values that mean "no report arrived inside the timeout", not
 * "the device went away". The PS4 backend sees Orbis codes; the PC ones see
 * libusb's -7. Keep the classification in one place so a backend cannot
 * accidentally turn an idle pad into a reconnect loop.
 */
int xcp_usb_status_is_timeout(int32_t status);

/*
 * A backend. Every entry point is optional except open/read/close; a missing
 * entry point is simply not called. `ctx` is per-backend state owned by the
 * implementation (e.g. the list returned by enumerate).
 */
typedef struct xcp_usb_backend {
    const char *name;

    /* One-time process/plugin wide setup. Returns XCP_USB_OK on success. */
    int  (*init)(void *ctx);

    /* Tear down. Safe to call without a matching successful init(). */
    void (*shutdown)(void *ctx);

    /*
     * Fill up to `max` entries in `out`, return the count (>=0) or a negative
     * error. Backends report everything they can see, including devices we
     * cannot drive (those get XCP_CTRL_NONE) so the CLI can show the user what
     * is actually attached.
     */
    int  (*enumerate)(void *ctx, xcp_usb_device_info *out, int max);

    /*
     * Open a device previously returned by enumerate(). Returns an opaque
     * handle, or NULL on failure.
     */
    void *(*open)(void *ctx, const xcp_usb_device_info *info);

    /*
     * Protocol handshake that must happen before the device streams input
     * (the Xbox One/Series GIP "power on" message, mostly). Optional.
     */
    int  (*start)(void *ctx, void *handle, xcp_controller_type type);

    /*
     * Read one report.
     *   XCP_USB_OK          -> *out_len set, buffer filled
     *   XCP_USB_NO_DATA     -> nothing within timeout_ms
     *   XCP_USB_DISCONNECT  -> device went away, handle is now dead
     */
    int  (*read)(void *ctx, void *handle, uint8_t *buf, size_t buf_len,
                 size_t *out_len, int timeout_ms);

    /* Close a handle from open(). */
    void (*close)(void *ctx, void *handle);

    /*
     * Optional. For XCP_CTRL_HID_GENERIC, copy the report-descriptor layout
     * the backend parsed when it opened the device. Returns 1 on success.
     */
    int  (*get_hid_map)(void *ctx, void *handle, xcp_hid_map *out);
} xcp_usb_backend;

/* --- Provided implementations -------------------------------------------- */

/*
 * Scripted backend for tests. Push reports with xcp_usb_mock_push(); the
 * backend hands them out one at a time and reports DISCONNECT once the queue is
 * empty and xcp_usb_mock_set_connected(0) was called.
 */
xcp_usb_backend *xcp_usb_mock_backend(void);
void xcp_usb_mock_reset(void);
void xcp_usb_mock_set_device(uint16_t vid, uint16_t pid);
void xcp_usb_mock_set_connected(int connected);
void xcp_usb_mock_push(const uint8_t *buf, size_t len);
/* Number of reports still queued (for assertions). */
int  xcp_usb_mock_pending(void);
/* Total read() calls made since reset - lets tests prove we poll at all. */
int  xcp_usb_mock_read_count(void);

/*
 * PC backend. Uses hidapi when built with -DXCP_HAVE_HIDAPI, otherwise every
 * call returns an "unsupported" error and enumeration yields nothing.
 */
xcp_usb_backend *xcp_usb_hidapi_backend(void);

/* PS4 backend, sceUsbd. Compiled into the payload build. */
xcp_usb_backend *xcp_usb_sceusbd_backend(void);

/* --- Shared helpers ------------------------------------------------------ */

/*
 * First enumerated device whose type is not XCP_CTRL_NONE. A named profile is
 * preferred over XCP_CTRL_HID_GENERIC; `wanted` overrides that preference.
 * Returns 1 and fills `out`.
 */
int xcp_usb_find(xcp_usb_backend *backend, void *ctx,
                 xcp_controller_type wanted, xcp_usb_device_info *out);

#endif /* XCP_USB_BACKEND_H */
