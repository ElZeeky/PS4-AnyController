/*
 * usb/src/usb_common.c - helpers that every backend and every build needs.
 *
 * This lives outside usb_mock.c on purpose: the mock is test-only and is not
 * linked into the payload, but the find helper is used by the console code too.
 */
#include "xcp/usb_backend.h"

/* How many devices we are willing to look at when searching. */
#define XCP_FIND_MAX 16

/* libusb's timeout is -7; Orbis reports 0x80240007 to sceUsbd callers. */
#define XCP_LIBUSB_ERROR_TIMEOUT (-7)
#define XCP_ORBIS_ERROR_TIMEOUT  ((int32_t)0x80240007u)

int xcp_usb_status_is_timeout(int32_t status)
{
    return status == XCP_LIBUSB_ERROR_TIMEOUT ||
           status == XCP_ORBIS_ERROR_TIMEOUT;
}

int xcp_usb_find(xcp_usb_backend *backend, void *ctx,
                 xcp_controller_type wanted, xcp_usb_device_info *out)
{
    xcp_usb_device_info found[XCP_FIND_MAX];
    int                 count;
    int                 i;

    if (!backend || !backend->enumerate || !out) return 0;

    count = backend->enumerate(ctx, found, XCP_FIND_MAX);
    if (count <= 0) return 0;
    if (count > XCP_FIND_MAX) count = XCP_FIND_MAX;

    {
        int generic = -1;

        for (i = 0; i < count; i++) {
            if (found[i].type == XCP_CTRL_NONE) continue;
            if (wanted != XCP_CTRL_NONE) {
                if (found[i].type == wanted) {
                    *out = found[i];
                    return 1;
                }
                continue;
            }
            /*
             * A named profile is always preferable to a descriptor-only HID
             * candidate. If only a candidate exists, use it after looking at
             * every device, so one unknown HID keyboard cannot hide a known
             * Xbox/Switch pad that was enumerated later.
             */
            if (found[i].type == XCP_CTRL_HID_GENERIC) {
                if (generic < 0) generic = i;
                continue;
            }
            *out = found[i];
            return 1;
        }
        if (generic >= 0) {
            *out = found[generic];
            return 1;
        }
    }
    return 0;
}
