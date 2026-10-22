/*
 * ps4/src/usb_sceusbd.c - the PS4 implementation of the USB backend.
 *
 * Every call here is a real function from the OpenOrbis Usbd.h header; nothing
 * is invented. Two things are worth knowing before reading it:
 *
 *  * sceUsbdGetDeviceList hands back an array we have to keep alive, because
 *    sceUsbdOpen needs the libusb_device* rather than a path. We therefore cache
 *    the list and address devices by their index, which is what goes into
 *    xcp_usb_device_info.path.
 *
 *  * libusb error codes are NOT in the vendored headers (only the transfer
 *    status enum is), so the one we test for by value is called out explicitly
 *    below and is flagged in PROGRESS.md as needing confirmation from klog.
 */
#include "xcp/usb_backend.h"
#include "xcp/report.h"
#include "xcp_log.h"

#include <orbis/Usbd.h>
#include <orbis/libkernel.h>

#include <stdio.h>
#include <string.h>

/* How many devices we remember from the last enumerate() call. */
#define XCP_SCAN_MAX 32

/* How many pads we are willing to keep open at once. */
#define XCP_OPEN_MAX 2

/*
 * libusb's LIBUSB_ERROR_TIMEOUT. Not present in the vendored Usbd.h, which only
 * declares the transfer *status* enum, so it is stated here by value and must be
 * confirmed from a real capture on the console: if a read timeout shows up as
 * something else, this is the line to change.
 */
#define XCP_LIBUSB_ERROR_TIMEOUT (-7)

typedef struct xcp_sceusbd_slot {
    libusb_device_handle *handle;
    xcp_controller_type   type;
    int                   claimed;
    int32_t               interface;    /* which one we claimed            */
    uint8_t               endpoint;     /* interrupt IN endpoint to read   */
    int                   endpoint_set; /* 0 = fall back to the type's own */
} xcp_sceusbd_slot;

/*
 * The sysroot declares struct libusb_endpoint_descriptor only as a forward
 * reference (orbis/_types/libusb.h uses `struct libusb_endpoint_descriptor *`
 * but never defines it), so its layout is stated here. These are the standard
 * libusb fields, and the three we read - bEndpointAddress, bmAttributes and
 * wMaxPacketSize - sit at fixed offsets 2/3/4 whatever the tail looks like.
 * Every entry is nevertheless validated by its bLength before use.
 */
struct xcp_libusb_endpoint {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
};

#define XCP_EP_DESC_SIZE   7u
#define XCP_EP_XFER_MASK   0x03u
#define XCP_EP_XFER_INTR   0x03u
#define XCP_EP_DIR_MASK    0x80u

/* How many interfaces we are willing to try before giving up. */
#define XCP_IFACE_MAX 8

static libusb_device    *g_scan[XCP_SCAN_MAX];
static int               g_scan_count;
/* The array sceUsbdGetDeviceList() handed us. sceUsbdFreeDeviceList() frees
 * the array it returned, so we must remember THAT pointer - never our own
 * g_scan copy - and free it before the next scan and on shutdown. */
static libusb_device   **g_device_list;
static xcp_sceusbd_slot  g_slots[XCP_OPEN_MAX];
static int               g_init_done;

/* ---------------------------------------------------------------------------
 * Internals
 * ------------------------------------------------------------------------ */

static int sceusbd_init(void *ctx)
{
    int32_t ret;

    (void)ctx;
    if (g_init_done) return XCP_USB_OK;

    ret = sceUsbdInit();
    if (ret != 0) {
        klog(XCP_TAG "sceUsbdInit failed (%d)\n", (int)ret);
        return XCP_USB_ERROR;
    }

    g_init_done = 1;
    klog(XCP_TAG "sceUsbd init ok\n");
    return XCP_USB_OK;
}

static void free_scan(void)
{
    if (g_device_list) {
        sceUsbdFreeDeviceList(g_device_list);
        g_device_list = NULL;
    }
    g_scan_count = 0;
}

static void sceusbd_shutdown(void *ctx)
{
    int i;

    (void)ctx;
    for (i = 0; i < XCP_OPEN_MAX; i++) {
        if (g_slots[i].handle) {
            if (g_slots[i].claimed) {
                sceUsbdReleaseInterface(g_slots[i].handle, g_slots[i].interface);
            }
            sceUsbdClose(g_slots[i].handle);
            g_slots[i].handle  = NULL;
            g_slots[i].claimed = 0;
        }
    }
    free_scan();

    if (g_init_done) {
        sceUsbdExit();
        g_init_done = 0;
    }
}

static int sceusbd_enumerate(void *ctx, xcp_usb_device_info *out, int max)
{
    libusb_device **list = NULL;
    int32_t         count;
    int             written = 0;
    int             i;

    (void)ctx;
    if (!out || max <= 0) return 0;

    free_scan();

    count = sceUsbdGetDeviceList(&list);
    if (count <= 0 || !list) {
        return 0;
    }

    /* Remember the array; free_scan() will free exactly this on the next scan
     * and on shutdown. The device pointers inside it must not outlive it. */
    g_device_list = list;

    for (i = 0; i < count && written < max; i++) {
        struct libusb_device_descriptor desc;
        xcp_usb_device_info             info;
        xcp_controller_type             type;
        int                             scan_index;

        if (g_scan_count >= XCP_SCAN_MAX) break;

        memset(&desc, 0, sizeof(desc));
        if (sceUsbdGetDeviceDescriptor(list[i], &desc) != 0) continue;

        type = xcp_controller_type_for_ids(desc.idVendor, desc.idProduct);

        memset(&info, 0, sizeof(info));
        info.vid  = desc.idVendor;
        info.pid  = desc.idProduct;
        info.type = type;
        /*
         * Address the device by its position in OUR cache, not by its position
         * in the bus list: a device skipped above (descriptor read failed) must
         * not shift every later index, or open() would grab the wrong device.
         */
        scan_index = g_scan_count;
        snprintf(info.path, XCP_USB_PATH_MAX, "sceusbd:%d", scan_index);
        snprintf(info.name, XCP_USB_NAME_MAX, "%04X:%04X",
                 (unsigned)desc.idVendor, (unsigned)desc.idProduct);

        /* Keep the pointer alive so open() can use it; the list stays alive
         * until the next enumerate()/shutdown frees it. */
        g_scan[g_scan_count++] = list[i];
        out[written++] = info;
    }

    return written;
}

/*
 * Log what the device actually is, then pick the interface to claim and the
 * interrupt IN endpoint to read from.
 *
 * A 2.4 GHz receiver dongle is a composite device: on a PC the Manba One's
 * receiver appears twice, once as an XInput interface group and once as a HID
 * game controller. Claiming interface 0 and reading EP 0x81 blindly can therefore
 * miss the interface the reports actually arrive on - and "found it but could not
 * open it" is the failure that would cost a round trip to the console. So we ask
 * the descriptors, and log everything we saw either way: on the console this log
 * is the only thing we get.
 *
 * Returns 0 with both out parameters filled, or -1 if nothing could be claimed.
 */
static int sceusbd_claim_input(libusb_device *dev, libusb_device_handle *handle,
                               int32_t *iface_out, uint8_t *endpoint_out)
{
    struct libusb_config_descriptor *cfg = NULL;
    int32_t ret;
    int     claimed = -1;
    uint8_t i;

    *iface_out    = 0;
    *endpoint_out = 0;

    ret = sceUsbdGetConfigDescriptor(dev, 0, &cfg);
    if (ret != 0 || !cfg || !cfg->interface) {
        /* No descriptors. Behave exactly as before this change. */
        klog(XCP_TAG "no config descriptor (%d); claiming interface 0\n", (int)ret);
        ret = sceUsbdClaimInterface(handle, 0);
        if (ret != 0) {
            klog(XCP_TAG "sceUsbdClaimInterface(0) failed (%d)\n", (int)ret);
            return -1;
        }
        return 0;
    }

    klog(XCP_TAG "device: %u interface(s), config value %u\n",
         (unsigned)cfg->bNumInterfaces, (unsigned)cfg->bConfigurationValue);

    for (i = 0; i < cfg->bNumInterfaces && i < XCP_IFACE_MAX; i++) {
        const struct libusb_interface             *iface = &cfg->interface[i];
        const struct libusb_interface_descriptor  *alt;
        const unsigned char                       *raw;
        uint8_t                                    found_ep = 0;
        uint8_t                                    j;

        if (iface->num_altsetting <= 0) {
            klog(XCP_TAG "  iface %u: no altsetting\n", (unsigned)i);
            continue;
        }
        alt = &iface->altsetting[0];

        klog(XCP_TAG "  iface %u: class %02X sub %02X proto %02X, %u endpoint(s)\n",
             (unsigned)alt->bInterfaceNumber, (unsigned)alt->bInterfaceClass,
             (unsigned)alt->bInterfaceSubClass, (unsigned)alt->bInterfaceProtocol,
             (unsigned)alt->bNumEndpoints);

        raw = (const unsigned char *)(const void *)alt->endpoint;
        for (j = 0; raw && j < alt->bNumEndpoints; j++) {
            const struct xcp_libusb_endpoint *ep =
                (const struct xcp_libusb_endpoint *)(const void *)
                (raw + (unsigned)j * XCP_EP_DESC_SIZE);
            int intr;
            int in;

            if (ep->bLength != XCP_EP_DESC_SIZE) {
                klog(XCP_TAG "    endpoint %u: bLength %u is not a 7 byte "
                             "descriptor, skipped\n",
                     (unsigned)j, (unsigned)ep->bLength);
                continue;
            }

            intr = ((ep->bmAttributes & XCP_EP_XFER_MASK) == XCP_EP_XFER_INTR);
            in   = ((ep->bEndpointAddress & XCP_EP_DIR_MASK) != 0);

            klog(XCP_TAG "    ep 0x%02X %s%s maxpacket %u interval %u\n",
                 (unsigned)ep->bEndpointAddress,
                 intr ? "interrupt" : "other", in ? " IN" : " OUT",
                 (unsigned)ep->wMaxPacketSize, (unsigned)ep->bInterval);

            if (intr && in && !found_ep) {
                found_ep = ep->bEndpointAddress;
            }
        }

        if (!found_ep) continue; /* not an input interface */

        ret = sceUsbdClaimInterface(handle, (int32_t)alt->bInterfaceNumber);
        klog(XCP_TAG "  claim iface %u -> %d\n",
             (unsigned)alt->bInterfaceNumber, (int)ret);

        if (ret == 0) {
            claimed       = (int)alt->bInterfaceNumber;
            *endpoint_out = found_ep;
            break;
        }
    }

    sceUsbdFreeConfigDescriptor(cfg);

    if (claimed < 0) {
        /* Nothing looked like an input interface, or every claim was refused:
         * fall back to interface 0, which is what this backend always did. */
        klog(XCP_TAG "no claimable interrupt IN endpoint; trying interface 0\n");
        ret = sceUsbdClaimInterface(handle, 0);
        if (ret != 0) {
            klog(XCP_TAG "sceUsbdClaimInterface(0) failed (%d)\n", (int)ret);
            return -1;
        }
        return 0;
    }

    *iface_out = claimed;
    klog(XCP_TAG "reading interrupt IN on EP 0x%02X of interface %d\n",
         (unsigned)*endpoint_out, (int)claimed);
    return 0;
}

static void *sceusbd_open(void *ctx, const xcp_usb_device_info *info)
{
    xcp_sceusbd_slot *slot = NULL;
    libusb_device    *dev  = NULL;
    int32_t           ret;
    int               index;
    int               i;

    (void)ctx;
    if (!info) return NULL;

    /* Find the cached device pointer by index. */
    if (sscanf(info->path, "sceusbd:%d", &index) != 1) return NULL;
    if (index < 0 || index >= g_scan_count) return NULL;
    dev = g_scan[index];
    if (!dev) return NULL;

    for (i = 0; i < XCP_OPEN_MAX; i++) {
        if (!g_slots[i].handle) {
            slot = &g_slots[i];
            break;
        }
    }
    if (!slot) return NULL;

    memset(slot, 0, sizeof(*slot));

    ret = sceUsbdOpen(dev, &slot->handle);
    if (ret != 0 || !slot->handle) {
        klog(XCP_TAG "sceUsbdOpen failed (%d)\n", (int)ret);
        slot->handle = NULL;
        return NULL;
    }

    /*
     * NOTE: sceUsbdDetachKernelDriver is declared with an empty parameter list in
     * the vendored Usbd.h, so it cannot be given a handle. It is deliberately
     * not called; see PROGRESS.md 4.4.
     */

    if (sceusbd_claim_input(dev, slot->handle, &slot->interface, &slot->endpoint) != 0) {
        sceUsbdClose(slot->handle);
        slot->handle = NULL;
        return NULL;
    }
    slot->claimed      = 1;
    slot->endpoint_set = (slot->endpoint != 0);
    slot->type         = info->type;

    klog(XCP_TAG "opened %s as %s\n", info->name,
         xcp_controller_type_name(info->type));

    return slot;
}

static int sceusbd_is_gip(xcp_controller_type type)
{
    return type == XCP_CTRL_XBOXONE || type == XCP_CTRL_XBOX_SERIES;
}

static int sceusbd_start(void *ctx, void *handle, xcp_controller_type type)
{
    /*
     * The Xbox 360 protocol needs no handshake: the pad streams 20 byte reports
     * on EP 0x81 as soon as input 0 is claimed.
     *
     * An Xbox One / Series pad speaking GIP uses the same three startup
     * messages Linux xpad and SDL send: power-on, LED-on and "security passed".
     * Send all three: some Series firmware does not stream after power-on
     * alone. The sequence number in byte 2 advances for every message.
     */
    static const uint8_t gip_init[3][8] = {
        { 0x05, 0x20, 0x00, 0x01, 0x00 },
        { 0x0A, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14 },
        { 0x06, 0x20, 0x00, 0x02, 0x01, 0x00 },
    };
    static const uint8_t gip_init_len[3] = { 5, 7, 6 };
    xcp_sceusbd_slot *slot = (xcp_sceusbd_slot *)handle;
    int32_t           transferred = 0;
    int32_t           ret;
    unsigned          i;

    (void)ctx;

    if (!sceusbd_is_gip(type)) return XCP_USB_OK;
    if (!slot || !slot->handle) return XCP_USB_ERROR;

    /* GIP input reports live on EP 0x82 and host messages go to EP 0x02. */
    ret = sceUsbdSetInterfaceAltSetting(slot->handle, slot->interface, 0);
    if (ret != 0) {
        klog(XCP_TAG "SetInterfaceAltSetting(%d,0) failed (%d)\n",
             (int)slot->interface, (int)ret);
    }

    for (i = 0; i < 3; i++) {
        uint8_t packet[8];
        memcpy(packet, gip_init[i], gip_init_len[i]);
        packet[2] = (uint8_t)i;
        transferred = 0;

        ret = sceUsbdInterruptTransfer(slot->handle, 0x02,
                                       packet, gip_init_len[i],
                                       &transferred, 100);
        klog(XCP_TAG "GIP init %u/%u sent, ret=%d transferred=%d\n",
             i + 1, 3u, (int)ret, (int)transferred);

        /* A failed power-on means the pad will not stream; the other two are
         * courtesy messages, so do not tear the device down if they fail. */
        if (i == 0 && ret != 0) return XCP_USB_ERROR;
    }

    return XCP_USB_OK;
}

static int sceusbd_read(void *ctx, void *handle, uint8_t *buf, size_t buf_len,
                        size_t *out_len, int timeout_ms)
{
    xcp_sceusbd_slot *slot = (xcp_sceusbd_slot *)handle;
    int32_t           transferred = 0;
    int32_t           ret;
    uint8_t           endpoint;

    (void)ctx;

    if (!slot || !slot->handle || !buf || !out_len) return XCP_USB_ERROR;
    if (buf_len == 0 || buf_len > 0x7FFFFFFF) return XCP_USB_ERROR;

    /* Whatever the descriptors said, if they said anything; otherwise the
     * type's own convention: Xbox 360, PDP Switch and DualSense stream on
     * EP 0x81, Xbox One/Series on EP 0x82. */
    endpoint = slot->endpoint_set ? slot->endpoint
                                  : (uint8_t)(sceusbd_is_gip(slot->type) ? 0x82 : 0x81);

    ret = sceUsbdInterruptTransfer(slot->handle, endpoint, (unsigned char *)buf,
                                   (int32_t)buf_len, &transferred, (uint32_t)timeout_ms);

    if (ret == 0) {
        if (transferred <= 0) return XCP_USB_NO_DATA;
        *out_len = (size_t)transferred;
        return XCP_USB_OK;
    }

    if (ret == XCP_LIBUSB_ERROR_TIMEOUT) return XCP_USB_NO_DATA;

    /*
     * The value here decides whether a transient hiccup tears the whole input
     * source down, so it must be seen rather than guessed: a real timeout that
     * comes back as anything other than -7 is silently treated as a removal, and
     * that turns into a create/delete loop around the virtual pad. Log the first
     * few so the code is a fact.
     */
    {
        static int logged;
        if (logged < 8) {
            klog(XCP_TAG "read: ep 0x%02X ret=0x%08X transferred=%d -> treating "
                         "as disconnect\n",
                 (unsigned)endpoint, (unsigned)ret, (int)transferred);
            logged++;
        }
    }

    /* Anything else means the pad went away or the transfer stalled. Report a
     * disconnect and let the caller close and retry. */
    return XCP_USB_DISCONNECT;
}

static void sceusbd_close(void *ctx, void *handle)
{
    xcp_sceusbd_slot *slot = (xcp_sceusbd_slot *)handle;

    (void)ctx;
    if (!slot) return;

    if (slot->handle) {
        if (slot->claimed) sceUsbdReleaseInterface(slot->handle, slot->interface);
        sceUsbdClose(slot->handle);
    }
    memset(slot, 0, sizeof(*slot));
}

static xcp_usb_backend g_sceusbd = {
    "sceusbd",
    sceusbd_init,
    sceusbd_shutdown,
    sceusbd_enumerate,
    sceusbd_open,
    sceusbd_start,
    sceusbd_read,
    sceusbd_close,
};

xcp_usb_backend *xcp_usb_sceusbd_backend(void)
{
    return &g_sceusbd;
}
