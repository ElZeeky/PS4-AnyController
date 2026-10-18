/*
 * usb/src/usb_common.c - helpers that every backend and every build needs.
 *
 * This lives outside usb_mock.c on purpose: the mock is test-only and is not
 * linked into the payload, but the find helper is used by the console code too.
 */
#include "xcp/usb_backend.h"

/* How many devices we are willing to look at when searching. */
#define XCP_FIND_MAX 16

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

    for (i = 0; i < count; i++) {
        if (found[i].type == XCP_CTRL_NONE) continue;
        if (wanted != XCP_CTRL_NONE && found[i].type != wanted) continue;
        *out = found[i];
        return 1;
    }
    return 0;
}
