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

/* How many interfaces we are willing to claim on one device. */
#define XCP_IFACE_MAX 8

typedef struct xcp_sceusbd_slot {
    libusb_device_handle *handle;
    xcp_controller_type   type;
    int                   claimed;
    int32_t               interface;    /* main interface we claimed       */
    int32_t               claimed_ifaces[XCP_IFACE_MAX];
    int                   claimed_count;
    int                   read_errors_logged; /* per open, not per process  */
    int                   strategy;     /* recovery ladder step, see below */
    int                   got_report;   /* at least one report arrived     */
    int                   failure_noted;
    uint8_t               endpoint;     /* interrupt IN endpoint to read   */
    uint8_t               out_endpoint; /* interrupt OUT endpoint, Switch  */
    int                   endpoint_set; /* 0 = fall back to the type's own */
    int                   switch_started;
    int                   switch_hs_step;
    int                   switch_sub_step;
    uint64_t              switch_next_us;
    uint64_t              last_connected_check_us;
    uint8_t               switch_seq;
    xcp_hid_map           hid_map;
    int                   hid_map_ok;
} xcp_sceusbd_slot;

/*
 * The sysroot declares struct libusb_endpoint_descriptor only as a forward
 * reference (orbis/_types/libusb.h uses `struct libusb_endpoint_descriptor *`
 * but never defines it), so its layout is stated here. The array is made of
 * aligned structs, NOT packed 7-byte USB descriptors: using a 7-byte stride
 * reads the second endpoint from inside the first and silently loses the OUT
 * endpoint (Switch Pro init needs it). The tail fields and alignment match the
 * libusb ABI shipped in freebsd-9.3/libusb.h.
 */
struct xcp_libusb_endpoint {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
    uint8_t  bRefresh;
    uint8_t  bSynchAddress;
    unsigned char *extra;
    int      extra_length;
} __attribute__((aligned(sizeof(void *))));

#define XCP_EP_DESC_SIZE   7u
#define XCP_EP_XFER_MASK   0x03u
#define XCP_EP_XFER_INTR   0x03u
#define XCP_EP_DIR_MASK    0x80u

static libusb_device    *g_scan[XCP_SCAN_MAX];
static int               g_scan_count;
/* The array sceUsbdGetDeviceList() handed us. sceUsbdFreeDeviceList() frees
 * the array it returned, so we must remember THAT pointer - never our own
 * g_scan copy - and free it before the next scan and on shutdown. */
static libusb_device   **g_device_list;
static xcp_sceusbd_slot  g_slots[XCP_OPEN_MAX];
static int               g_init_done;

/*
 * The vendored Usbd.h declares these with an empty parameter list (and
 * DetachKernelDriver as returning void), but the implementation takes
 * (handle, interface) and returns an int. Give the symbol a distinct C name
 * with the real prototype so we can use the result without fighting the
 * header. This is the same call AnyPad makes.
 */
extern int xcp_sceusbd_detach_kernel(libusb_device_handle *, int)
    __asm__("sceUsbdDetachKernelDriver");

/* libusb_clear_halt(handle, endpoint); only used by recovery strategy 2. */
extern int xcp_sceusbd_clear_halt(libusb_device_handle *, unsigned char)
    __asm__("sceUsbdClearHalt");

/*
 * Recovery ladder for pads that open but never deliver a report (the DualSense
 * and some HID pads abort every read straight after the claim, and nobody knows
 * why yet). Each failed open tries the next variant; the one that delivers a
 * report is remembered. Every step is logged, so a tester's log says which one
 * worked. Only DualSense and Generic HID take part; the pads that already work
 * are never touched.
 *
 *   0  standard
 *   1  also claim the interfaces the console normally keeps (Sony audio)
 *   2  SET_IDLE(0) and clear-halt on the IN endpoint after the claim
 *   3  claim without detaching the kernel driver first
 */
#define XCP_STRATEGY_COUNT 4
static int g_strategy_next[2];
static int g_strategy_locked[2]; /* 1-based; 0 = nothing has worked yet */

static int sceusbd_ladder_applies(xcp_controller_type type)
{
    return type == XCP_CTRL_DUALSENSE || type == XCP_CTRL_HID_GENERIC;
}

static int sceusbd_ladder_index(xcp_controller_type type)
{
    return type == XCP_CTRL_DUALSENSE ? 0 : 1;
}

static const char *sceusbd_strategy_name(int s)
{
    switch (s) {
    case 1:  return "also claim the interfaces the console keeps";
    case 2:  return "SET_IDLE + clear halt after the claim";
    case 3:  return "claim without detaching the kernel driver";
    default: return "standard";
    }
}

/* The endpoint array is made of aligned structs, not packed descriptors. */
static int iface_endpoints(const struct libusb_interface_descriptor *alt,
                           uint8_t *in, uint8_t *out)
{
    const struct xcp_libusb_endpoint *eps;
    uint8_t found_in = 0, found_out = 0;
    uint8_t j;

    if (!alt) return 0;
    eps = (const struct xcp_libusb_endpoint *)(const void *)alt->endpoint;
    for (j = 0; eps && j < alt->bNumEndpoints; j++) {
        const struct xcp_libusb_endpoint *ep = &eps[j];
        int intr, dir_in;

        if (ep->bLength != XCP_EP_DESC_SIZE) continue;
        intr   = ((ep->bmAttributes & XCP_EP_XFER_MASK) == XCP_EP_XFER_INTR);
        dir_in = ((ep->bEndpointAddress & XCP_EP_DIR_MASK) != 0);
        if (intr && dir_in && !found_in)  found_in = ep->bEndpointAddress;
        if (intr && !dir_in && !found_out) found_out = ep->bEndpointAddress;
    }
    if (in) *in = found_in;
    if (out) *out = found_out;
    return found_in != 0;
}

static int iface_matches_type(const struct libusb_interface_descriptor *alt,
                              xcp_controller_type type)
{
    if (!alt) return 0;
    switch (type) {
    case XCP_CTRL_XBOX360:
        return alt->bInterfaceClass == 0xFF &&
               alt->bInterfaceSubClass == 0x5D &&
               alt->bInterfaceProtocol == 0x01;
    case XCP_CTRL_XBOXONE:
    case XCP_CTRL_XBOX_SERIES:
        return alt->bInterfaceClass == 0xFF &&
               alt->bInterfaceSubClass == 0x47 &&
               alt->bInterfaceProtocol == 0xD0;
    case XCP_CTRL_SWITCH:
    case XCP_CTRL_SWITCH_PRO:
    case XCP_CTRL_DUALSENSE:
        return alt->bInterfaceClass == 0x03;
    case XCP_CTRL_HID_GENERIC:
        return alt->bInterfaceClass == 0x03 &&
               alt->bInterfaceSubClass == 0 &&
               alt->bInterfaceProtocol == 0;
    case XCP_CTRL_NONE:
    default:
        return 0;
    }
}

/*
 * Pick the interface a pad actually streams on. A 2.4 GHz receiver can expose
 * an XInput group, a HID gamepad and a keyboard at once; interface 0 + EP 0x81
 * is not a safe guess. Prefer the interface class that matches the ids, then a
 * known Xbox class, then a HID gamepad candidate.
 */
static int sceusbd_choose_iface(const struct libusb_config_descriptor *cfg,
                                xcp_controller_type type, int preferred,
                                int *iface_out, uint8_t *in_out,
                                uint8_t *out_out, uint8_t *class_out,
                                uint8_t *sub_out, uint8_t *proto_out)
{
    int best = -1, best_score = -1;
    uint8_t best_in = 0, best_out = 0;
    uint8_t best_class = 0, best_sub = 0, best_proto = 0;
    uint8_t i;

    if (!cfg || !cfg->interface) return 0;

    for (i = 0; i < cfg->bNumInterfaces && i < XCP_IFACE_MAX; i++) {
        const struct libusb_interface *iface = &cfg->interface[i];
        const struct libusb_interface_descriptor *alt;
        uint8_t in = 0, out = 0;
        int score = 0;

        if (iface->num_altsetting <= 0) continue;
        alt = &iface->altsetting[0];
        if (!iface_endpoints(alt, &in, &out)) continue;

        if (preferred >= 0 && alt->bInterfaceNumber == (uint8_t)preferred) {
            score = 100;
        } else if (iface_matches_type(alt, type)) {
            score = 40;
        } else if (alt->bInterfaceClass == 0xFF &&
                   alt->bInterfaceSubClass == 0x5D &&
                   alt->bInterfaceProtocol == 0x01) {
            score = 30; /* XInput */
        } else if (alt->bInterfaceClass == 0xFF &&
                   alt->bInterfaceSubClass == 0x47 &&
                   alt->bInterfaceProtocol == 0xD0) {
            score = 28; /* GIP */
        } else if (alt->bInterfaceClass == 0x03 &&
                   alt->bInterfaceSubClass == 0 &&
                   alt->bInterfaceProtocol == 0) {
            score = 20; /* HID gamepad candidate */
        } else if (alt->bInterfaceClass == 0x03) {
            score = 10;
        } else {
            score = 1;
        }

        if (score > best_score) {
            best_score = score;
            best = (int)alt->bInterfaceNumber;
            best_in = in;
            best_out = out;
            best_class = alt->bInterfaceClass;
            best_sub = alt->bInterfaceSubClass;
            best_proto = alt->bInterfaceProtocol;
        }
    }

    if (best < 0) return 0;
    if (iface_out) *iface_out = best;
    if (in_out)    *in_out = best_in;
    if (out_out)   *out_out = best_out;
    if (class_out) *class_out = best_class;
    if (sub_out)   *sub_out = best_sub;
    if (proto_out) *proto_out = best_proto;
    return 1;
}

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
            int j;
            for (j = 0; j < g_slots[i].claimed_count; j++) {
                sceUsbdReleaseInterface(g_slots[i].handle,
                                        g_slots[i].claimed_ifaces[j]);
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
        struct libusb_config_descriptor *cfg = NULL;
        xcp_usb_device_info             info;
        xcp_controller_type             type;
        uint8_t                         iface_class = 0;
        uint8_t                         iface_sub = 0;
        uint8_t                         iface_proto = 0;
        int                             iface = -1;
        int                             has_in = 0;
        int                             scan_index;

        if (g_scan_count >= XCP_SCAN_MAX) break;

        memset(&desc, 0, sizeof(desc));
        if (sceUsbdGetDeviceDescriptor(list[i], &desc) != 0) continue;

        type = xcp_controller_type_for_ids(desc.idVendor, desc.idProduct);

        if (sceUsbdGetConfigDescriptor(list[i], 0, &cfg) == 0 && cfg) {
            has_in = sceusbd_choose_iface(cfg, type, -1, &iface, NULL, NULL,
                                          &iface_class, &iface_sub, &iface_proto);
            if (has_in) {
                type = xcp_controller_type_for_interface(desc.idVendor,
                                                         desc.idProduct,
                                                         iface_class, iface_sub,
                                                         iface_proto, 1);
            }
            sceUsbdFreeConfigDescriptor(cfg);
        }
        if (!has_in) {
            type = xcp_controller_type_for_interface(desc.idVendor,
                                                     desc.idProduct,
                                                     0, 0, 0, 0);
        }

        memset(&info, 0, sizeof(info));
        info.vid              = desc.idVendor;
        info.pid              = desc.idProduct;
        info.type             = type;
        info.iface_class      = iface_class;
        info.iface_subclass   = iface_sub;
        info.iface_protocol   = iface_proto;
        info.iface_number     = iface >= 0 ? (uint8_t)iface : 0;
        info.has_interrupt_in = (uint8_t)(has_in != 0);
        /*
         * Address the device by its position in OUR cache, not by its position
         * in the bus list: a device skipped above (descriptor read failed) must
         * not shift every later index, or open() would grab the wrong device.
         */
        scan_index = g_scan_count;
        snprintf(info.path, XCP_USB_PATH_MAX, "sceusbd:%d", scan_index);
        if (has_in) {
            snprintf(info.name, XCP_USB_NAME_MAX, "%04X:%04X iface %d (%02X/%02X/%02X)",
                     (unsigned)desc.idVendor, (unsigned)desc.idProduct,
                     iface, (unsigned)iface_class, (unsigned)iface_sub,
                     (unsigned)iface_proto);
        } else {
            snprintf(info.name, XCP_USB_NAME_MAX, "%04X:%04X",
                     (unsigned)desc.idVendor, (unsigned)desc.idProduct);
        }

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
static void slot_remember_iface(xcp_sceusbd_slot *slot, int iface)
{
    if (!slot || slot->claimed_count >= XCP_IFACE_MAX) return;
    slot->claimed_ifaces[slot->claimed_count++] = (int32_t)iface;
}

static int slot_has_iface(const xcp_sceusbd_slot *slot, int iface)
{
    int i;

    for (i = 0; i < slot->claimed_count; i++) {
        if (slot->claimed_ifaces[i] == (int32_t)iface) return 1;
    }
    return 0;
}

static int claim_one(libusb_device_handle *handle, int iface, const char *what,
                     xcp_sceusbd_slot *slot)
{
    int32_t ret;

    /*
     * The system driver may hold the interface. Detach it first, as AnyPad
     * does; otherwise a GIP pad can be mistaken for a headset and a HID
     * interface for a keyboard. Calling this when no driver is active is
     * harmless on libusb.
     */
    if (slot && slot->strategy == 3) {
        klog(XCP_TAG "  detach %s iface %d skipped (strategy 3)\n", what, iface);
    } else {
        ret = xcp_sceusbd_detach_kernel(handle, iface);
        klog(XCP_TAG "  detach %s iface %d -> %d\n", what, iface, (int)ret);
    }

    ret = sceUsbdClaimInterface(handle, iface);
    klog(XCP_TAG "  claim %s iface %d -> %d\n", what, iface, (int)ret);
    if (ret != 0) return 0;

    slot_remember_iface(slot, iface);
    return 1;
}

static int sceusbd_claim_input(libusb_device *dev, libusb_device_handle *handle,
                               const xcp_usb_device_info *info,
                               xcp_sceusbd_slot *slot)
{
    struct libusb_config_descriptor *cfg = NULL;
    int32_t ret;
    int     preferred = -1;
    int     claimed = -1;
    uint8_t endpoint = 0;
    uint8_t out_endpoint = 0;
    uint8_t i;

    if (!slot) return -1;
    slot->interface = 0;
    slot->endpoint = 0;
    slot->out_endpoint = 0;
    slot->endpoint_set = 0;

    if (info && info->has_interrupt_in) preferred = (int)info->iface_number;

    ret = sceUsbdGetConfigDescriptor(dev, 0, &cfg);
    if (ret != 0 || !cfg || !cfg->interface) {
        /* No descriptors. Behave exactly as before this change. */
        klog(XCP_TAG "no config descriptor (%d); claiming interface 0\n", (int)ret);
        if (!claim_one(handle, 0, "fallback", slot)) return -1;
        slot->interface = 0;
        slot->claimed = 1;
        return 0;
    }

    klog(XCP_TAG "device: %u interface(s), config value %u\n",
         (unsigned)cfg->bNumInterfaces, (unsigned)cfg->bConfigurationValue);

    for (i = 0; i < cfg->bNumInterfaces && i < XCP_IFACE_MAX; i++) {
        const struct libusb_interface            *iface = &cfg->interface[i];
        const struct libusb_interface_descriptor *alt;
        const struct xcp_libusb_endpoint         *eps;
        uint8_t in = 0, out = 0, j;

        if (iface->num_altsetting <= 0) {
            klog(XCP_TAG "  iface %u: no altsetting\n", (unsigned)i);
            continue;
        }
        alt = &iface->altsetting[0];
        klog(XCP_TAG "  iface %u: class %02X sub %02X proto %02X, %u endpoint(s)\n",
             (unsigned)alt->bInterfaceNumber, (unsigned)alt->bInterfaceClass,
             (unsigned)alt->bInterfaceSubClass, (unsigned)alt->bInterfaceProtocol,
             (unsigned)alt->bNumEndpoints);

        eps = (const struct xcp_libusb_endpoint *)(const void *)alt->endpoint;
        for (j = 0; eps && j < alt->bNumEndpoints; j++) {
            const struct xcp_libusb_endpoint *ep = &eps[j];
            int intr, dir_in;

            if (ep->bLength != XCP_EP_DESC_SIZE) {
                klog(XCP_TAG "    endpoint %u: bLength %u is not a 7 byte "
                             "descriptor, skipped\n",
                     (unsigned)j, (unsigned)ep->bLength);
                continue;
            }
            intr   = ((ep->bmAttributes & XCP_EP_XFER_MASK) == XCP_EP_XFER_INTR);
            dir_in = ((ep->bEndpointAddress & XCP_EP_DIR_MASK) != 0);
            klog(XCP_TAG "    ep 0x%02X %s%s maxpacket %u interval %u\n",
                 (unsigned)ep->bEndpointAddress,
                 intr ? "interrupt" : "other", dir_in ? " IN" : " OUT",
                 (unsigned)ep->wMaxPacketSize, (unsigned)ep->bInterval);
            if (intr && dir_in && !in)  in = ep->bEndpointAddress;
            if (intr && !dir_in && !out) out = ep->bEndpointAddress;
        }
    }

    if (sceusbd_choose_iface(cfg, info ? info->type : XCP_CTRL_NONE,
                             preferred, &claimed, NULL, NULL, NULL, NULL, NULL)) {
        const struct libusb_interface_descriptor *alt = NULL;

        for (i = 0; i < cfg->bNumInterfaces && i < XCP_IFACE_MAX; i++) {
            const struct libusb_interface *iface = &cfg->interface[i];
            if (iface->num_altsetting > 0 &&
                (int)iface->altsetting[0].bInterfaceNumber == claimed) {
                alt = &iface->altsetting[0];
                break;
            }
        }
        if (alt) (void)iface_endpoints(alt, &endpoint, &out_endpoint);
        if (!claim_one(handle, claimed, "main", slot)) {
            claimed = -1;
        }
    }

    if (claimed < 0) {
        /* Nothing looked like an input interface, or the claim was refused:
         * fall back to interface 0, which is what this backend always did. */
        klog(XCP_TAG "no claimable interrupt IN endpoint; trying interface 0\n");
        if (!claim_one(handle, 0, "fallback", slot)) {
            sceUsbdFreeConfigDescriptor(cfg);
            return -1;
        }
        claimed = 0;
        endpoint = 0;
        out_endpoint = 0;
    }

    /*
     * Claim the interfaces the console would otherwise take for itself: audio
     * (so it does not route TV sound to the pad) and, on Xbox/GIP pads, extra
     * HID interfaces (often a keyboard). Failures are logged but not fatal.
     */
    for (i = 0; i < cfg->bNumInterfaces && i < XCP_IFACE_MAX; i++) {
        const struct libusb_interface *iface = &cfg->interface[i];
        const struct libusb_interface_descriptor *alt;
        int extra;

        if (iface->num_altsetting <= 0) continue;
        alt = &iface->altsetting[0];
        if ((int)alt->bInterfaceNumber == claimed) continue;
        if (slot_has_iface(slot, (int)alt->bInterfaceNumber)) continue;
        /* Sony's pads keep their audio and HID side on the console, unless the
         * recovery ladder is trying the opposite (strategy 1). */
        if (info && info->vid == XCP_VID_SONY && slot->strategy != 1) continue;

        extra = alt->bInterfaceClass == 0x01; /* audio control/streaming */
        if (!extra && alt->bInterfaceClass == 0x03 &&
            info && info->type != XCP_CTRL_HID_GENERIC) {
            extra = 1; /* keyboard/mouse side of an Xbox pad */
        }
        if (!extra) continue;

        if (claim_one(handle, (int)alt->bInterfaceNumber,
                      alt->bInterfaceClass == 0x01 ? "audio" : "extra HID", slot)) {
            klog(XCP_TAG "  iface %u kept away from the console\n",
                 (unsigned)alt->bInterfaceNumber);
        }
    }

    sceUsbdFreeConfigDescriptor(cfg);

    slot->interface = claimed;
    slot->endpoint = (uint8_t)endpoint;
    slot->out_endpoint = (uint8_t)out_endpoint;
    slot->endpoint_set = (endpoint != 0);
    slot->claimed = 1;
    klog(XCP_TAG "reading interrupt IN on EP 0x%02X of interface %d\n",
         (unsigned)slot->endpoint, (int)slot->interface);
    return 0;
}

/* Strategy 2: what a host HID driver normally does after the claim. */
static void sceusbd_hid_wake(xcp_sceusbd_slot *slot)
{
    int32_t idle = sceUsbdControlTransfer(slot->handle, 0x21, 0x0A, 0x0000,
                                          (uint16_t)slot->interface, NULL, 0, 500);
    int     halt = slot->endpoint ? xcp_sceusbd_clear_halt(slot->handle,
                                                           slot->endpoint)
                                  : -1;
    klog(XCP_TAG "strategy 2: SET_IDLE -> %d, clear halt EP 0x%02X -> %d\n",
         (int)idle, (unsigned)slot->endpoint, halt);
}

static void sceusbd_note_report(xcp_sceusbd_slot *slot)
{
    int idx;

    if (slot->got_report) return;
    slot->got_report = 1;
    if (!sceusbd_ladder_applies(slot->type)) return;
    idx = sceusbd_ladder_index(slot->type);
    if (!g_strategy_locked[idx]) {
        g_strategy_locked[idx] = slot->strategy + 1;
        klog(XCP_TAG "strategy %d (%s) delivered a report; keeping it\n",
             slot->strategy, sceusbd_strategy_name(slot->strategy));
    }
}

static void sceusbd_note_failure(xcp_sceusbd_slot *slot)
{
    int idx;

    if (slot->got_report || slot->failure_noted ||
        !sceusbd_ladder_applies(slot->type)) {
        return;
    }
    slot->failure_noted = 1;
    idx = sceusbd_ladder_index(slot->type);
    if (!g_strategy_locked[idx]) {
        g_strategy_next[idx] = (slot->strategy + 1) % XCP_STRATEGY_COUNT;
        klog(XCP_TAG "no report with strategy %d; the next open tries %d (%s)\n",
             slot->strategy, g_strategy_next[idx],
             sceusbd_strategy_name(g_strategy_next[idx]));
    }
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

    if (sceusbd_claim_input(dev, slot->handle, info, slot) != 0) {
        sceUsbdClose(slot->handle);
        slot->handle = NULL;
        return NULL;
    }
    slot->type = info->type;

    if (sceusbd_ladder_applies(info->type)) {
        int idx = sceusbd_ladder_index(info->type);

        slot->strategy = g_strategy_locked[idx] ? g_strategy_locked[idx] - 1
                                                : g_strategy_next[idx];
        klog(XCP_TAG "open strategy %d: %s%s\n", slot->strategy,
             sceusbd_strategy_name(slot->strategy),
             g_strategy_locked[idx] ? " (known good)" : "");
    }

    /*
     * A Generic HID device is only accepted after its report descriptor says
     * it is a gamepad. The descriptor also gives the bit position of every
     * button and axis, so there is no per-clone table to maintain.
     */
    if (slot->strategy == 2) sceusbd_hid_wake(slot);

    if (info->type == XCP_CTRL_HID_GENERIC) {
        uint8_t desc[1024];
        int     dlen = sceUsbdControlTransfer(slot->handle, 0x81, 0x06, 0x2200,
                                              (uint16_t)slot->interface,
                                              desc, sizeof(desc), 1000);
        if (dlen <= 0 ||
            !xcp_hid_parse_descriptor(desc, (size_t)dlen,
                                      info->vid, info->pid, &slot->hid_map)) {
            int j;
            klog(XCP_TAG "generic HID: no gamepad descriptor (%d)\n", dlen);
            for (j = 0; j < slot->claimed_count; j++) {
                sceUsbdReleaseInterface(slot->handle, slot->claimed_ifaces[j]);
            }
            sceUsbdClose(slot->handle);
            memset(slot, 0, sizeof(*slot));
            return NULL;
        }
        slot->hid_map_ok = 1;
        klog(XCP_TAG "generic HID: %u buttons, descriptor %d bytes\n",
             (unsigned)slot->hid_map.button_count, dlen);
    }

    klog(XCP_TAG "opened %s as %s\n", info->name,
         xcp_controller_type_name(info->type));

    if (info->type == XCP_CTRL_DUALSENSE) {
        /*
         * Diagnostic only (read-only request): the DualSense has failed on every
         * firmware with the interrupt IN aborting immediately. A control
         * transfer on EP0 tells "the endpoint is the problem" (this works, the
         * reads do not) from "the device is being reset under us" (both fail).
         */
        uint8_t cal[41];
        int     pr = sceUsbdControlTransfer(slot->handle, 0xA1, 0x01, 0x0305,
                                            (uint16_t)slot->interface,
                                            cal, sizeof(cal), 500);
        klog(XCP_TAG "DualSense probe: GET_REPORT(feature 0x05) on iface %d "
                     "-> %d\n", (int)slot->interface, pr);
    }

    return slot;
}

static int sceusbd_is_gip(xcp_controller_type type)
{
    return type == XCP_CTRL_XBOXONE || type == XCP_CTRL_XBOX_SERIES;
}

static void sceusbd_switch_tick(xcp_sceusbd_slot *slot, uint64_t now)
{
    static const uint8_t hs[4][2] = {
        { 0x80, 0x02 }, { 0x80, 0x03 }, { 0x80, 0x02 }, { 0x80, 0x04 },
    };
    static const uint8_t idle[8] = {
        0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40,
    };
    /* Output report 0x01 subcommands: full report mode, IMU on, vibration on,
     * player-1 LED. A stock Pro Controller starts in simple report mode 0x3F,
     * so without these it never sends the 0x30 report this decoder needs. */
    static const uint8_t sub_id[4]  = { 0x03, 0x40, 0x48, 0x30 };
    static const uint8_t sub_arg[4] = { 0x30, 0x01, 0x01, 0x01 };
    static int errors;
    uint8_t    packet[64];
    uint8_t    out;
    int32_t    transferred = 0;
    int32_t    ret;

    if (!slot || !slot->handle || !slot->switch_started) return;
    if (now < slot->switch_next_us) return;

    out = slot->out_endpoint ? slot->out_endpoint : 0x01;
    memset(packet, 0, sizeof(packet));

    if (slot->switch_hs_step < 4) {
        packet[0] = hs[slot->switch_hs_step][0];
        packet[1] = hs[slot->switch_hs_step][1];
        slot->switch_hs_step++;
        slot->switch_next_us = now + 100000u;
    } else if (slot->switch_sub_step < 4) {
        packet[0]  = 0x01;
        packet[1]  = (uint8_t)(slot->switch_seq++ & 0x0Fu);
        memcpy(packet + 2, idle, sizeof(idle));
        packet[10] = sub_id[slot->switch_sub_step];
        packet[11] = sub_arg[slot->switch_sub_step];
        slot->switch_sub_step++;
        slot->switch_next_us = now + 50000u;
    } else {
        packet[0] = 0x10;
        packet[1] = (uint8_t)(slot->switch_seq++ & 0x0Fu);
        memcpy(packet + 2, idle, sizeof(idle));
        slot->switch_next_us = now + 500000u;
    }

    ret = sceUsbdInterruptTransfer(slot->handle, out, packet,
                                   (int32_t)sizeof(packet), &transferred, 100);
    if (ret != 0 && errors < 5) {
        klog(XCP_TAG "Switch output on EP 0x%02X: ret=%d transferred=%d\n",
             (unsigned)out, (int)ret, (int)transferred);
        errors++;
    }
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

    if (!slot || !slot->handle) return XCP_USB_ERROR;

    if (type == XCP_CTRL_SWITCH_PRO) {
        slot->switch_started  = 1;
        slot->switch_hs_step  = 0;
        slot->switch_sub_step = 0;
        slot->switch_next_us  = 0;
        slot->switch_seq      = 0;
        sceusbd_switch_tick(slot, sceKernelGetProcessTime());
        return XCP_USB_OK;
    }

    if (!sceusbd_is_gip(type)) return XCP_USB_OK;

    /* GIP input reports live on the interface's IN endpoint (usually 0x82) and
     * host messages go to its OUT endpoint (usually 0x02). Use the descriptor's
     * OUT endpoint: some third-party pads do not use 0x02. */
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

        ret = sceUsbdInterruptTransfer(slot->handle,
                                       slot->out_endpoint ? slot->out_endpoint
                                                          : 0x02,
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
     * type's own convention: Xbox 360, PDP/Switch Pro and DualSense stream on
     * EP 0x81, Xbox One/Series on EP 0x82. */
    endpoint = slot->endpoint_set ? slot->endpoint
                                  : (uint8_t)(sceusbd_is_gip(slot->type) ? 0x82 : 0x81);

    sceusbd_switch_tick(slot, sceKernelGetProcessTime());

    ret = sceUsbdInterruptTransfer(slot->handle, endpoint, (unsigned char *)buf,
                                   (int32_t)buf_len, &transferred, (uint32_t)timeout_ms);

    if (ret == 0) {
        if (transferred <= 0) return XCP_USB_NO_DATA;
        *out_len = (size_t)transferred;
        sceusbd_note_report(slot);
        return XCP_USB_OK;
    }

    if (xcp_usb_status_is_timeout(ret)) {
        uint64_t now = sceKernelGetProcessTime();

        /*
         * A timeout can also be the only thing a dead handle reports. Do not
         * guess: ask libSceUsbd whether the device is still connected, once a
         * second while the pad is idle. This keeps an idle pad open forever
         * while still letting an unplugged idle pad release its virtual pad.
         */
        if (now - slot->last_connected_check_us >= 1000000u) {
            int32_t connected;

            slot->last_connected_check_us = now;
            connected = sceUsbdCheckConnected(slot->handle);
            /*
             * This is a libusb-style status: 0 means connected, a negative
             * error means the handle/device is gone. Do not test `<= 0`, or
             * every idle pad would be torn down once a second.
             */
            if (connected < 0) {
                static int logged_gone;
                if (logged_gone < 4) {
                    klog(XCP_TAG "read: ep 0x%02X timeout, "
                                 "sceUsbdCheckConnected=%d -> device gone\n",
                         (unsigned)endpoint, (int)connected);
                    logged_gone++;
                }
                return XCP_USB_DISCONNECT;
            }
        }
        return XCP_USB_NO_DATA;
    }

    /*
     * The value here decides whether a transient hiccup tears the whole input
     * source down, so it must be seen rather than guessed: a real timeout that
     * comes back as anything other than -7 is silently treated as a removal, and
     * that turns into a create/delete loop around the virtual pad. Log the first
     * few so the code is a fact.
     */
    {
        /* Per open: a process-wide counter hid every error after the first
         * eight, so a pad that fails again on each reconnect looked silent. */
        if (slot->read_errors_logged < 8) {
            klog(XCP_TAG "read: ep 0x%02X ret=0x%08X transferred=%d -> treating "
                         "as disconnect\n",
                 (unsigned)endpoint, (unsigned)ret, (int)transferred);
            slot->read_errors_logged++;
        }
    }

    /* Anything else means the pad went away or the transfer stalled. Report a
     * disconnect and let the caller close and retry. */
    sceusbd_note_failure(slot);
    return XCP_USB_DISCONNECT;
}

static void sceusbd_close(void *ctx, void *handle)
{
    xcp_sceusbd_slot *slot = (xcp_sceusbd_slot *)handle;

    (void)ctx;
    if (!slot) return;

    if (slot->handle) {
        int i;
        for (i = 0; i < slot->claimed_count; i++) {
            sceUsbdReleaseInterface(slot->handle, slot->claimed_ifaces[i]);
        }
        sceUsbdClose(slot->handle);
    }
    memset(slot, 0, sizeof(*slot));
}

static int sceusbd_get_hid_map(void *ctx, void *handle, xcp_hid_map *out)
{
    xcp_sceusbd_slot *slot = (xcp_sceusbd_slot *)handle;

    (void)ctx;
    if (!slot || !out || !slot->hid_map_ok) return 0;
    *out = slot->hid_map;
    return 1;
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
    sceusbd_get_hid_map,
};

xcp_usb_backend *xcp_usb_sceusbd_backend(void)
{
    return &g_sceusbd;
}
