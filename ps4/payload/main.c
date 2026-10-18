/*
 * ps4/payload/main.c - the GoldHEN payload entry point.
 *
 * What this does, and why it is a payload rather than a game-process plugin
 * -------------------------------------------------------------------------
 * GoldHEN only loads .prx plugins into a GAME process, so a plugin can never
 * reach the home screen, the settings menus or the sign-in screen: those belong
 * to system processes that never load plugins. GoldHEN *does* run an ELF payload
 * inside an existing system process, and from there Sony's virtual-device API is
 * available, which is the API Remote Play itself goes through. The system then
 * sees a real DualShock 4 - in every menu and in games - instead of a game
 * process having its pad reads rewritten.
 *
 * The input source is unchanged: the same core/ decoder and the same sceUsbd
 * backend the plugin uses. The Xbox pad's receiver is enumerated on the console's
 * own USB bus, its reports are decoded to xcp_pad_state, and the result is
 * inserted into the virtual pad. No PC, no browser, no cable to anything.
 *
 * Requirements and honest limits
 * ------------------------------
 *  * GoldHEN v2.4b18.10 or later: older ones do not run ELF payloads reliably.
 *  * The kernel log is the only place the new device's handle is announced, so
 *    we read it around AddDevice and let it go immediately afterwards.
 *  * Everything about the real console is unverified until it is run there. The
 *    first thing this prints is the USB device list, so one run says what the
 *    receiver actually identifies as.
 */
#include "xcp_klog_watch.h"
#include "xcp_log.h"
#include "xcp_pad_patch.h"
#include "xcp_vda.h"

#include "orbis_glue.h"
#include "xcp/pad_state.h"
#include "xcp/translate.h"
#include "xcp/usb_backend.h"

#include <orbis/libkernel.h>

#include <stdio.h>
#include <string.h>

/* How many USB devices we are willing to list in the diagnostic dump. */
#define XCP_USB_MAX_DEVICES 32

/* Report once every 4 ms while there is input, i.e. a real pad's own rate. */
#define XCP_POLL_INTERVAL_US 4000

/* ...and every 16 ms when there is none, just to keep the pad alive. */
#define XCP_IDLE_INTERVAL_US 16000

/* Short on purpose: the poll runs in the system process, in a plain loop.
 * 16 ms, not 2: the pad streams a report every few milliseconds, and a timeout
 * shorter than the pad's own period makes ordinary gaps look like failures. */
#define XCP_READ_TIMEOUT_MS 16

/* A single failed transfer is NOT a removal. The source is only declared gone
 * once nothing has decoded for this long. */
#define XCP_INPUT_DEAD_US 1000000

/* How long a touchpad click outlives the last L3/R3 edge. The Manba's back
 * buttons are macros, so an "L3+R3" mapping arrives as a short burst rather
 * than a held chord; latching this long turns the burst into one usable click. */
#define XCP_TOUCHPAD_HOLD_US 250000

/* Look for the receiver on the USB bus this often while it is absent. */
#define XCP_RESCAN_INTERVAL_US 1000000

/* How long to wait for the kernel log to name the device we just created. */
#define XCP_KLOG_WAIT_MS 700

/* How long to wait before retrying virtual-pad creation after a failure. */
#define XCP_PAD_RETRY_INTERVAL_US 3000000

/*
 * Dump the first few raw reports in the exact format `xcp_inspector --play`
 * reads, so the pad's own bytes can be checked against the same decoder the
 * console runs. 0 disables it.
 */
#define XCP_CAPTURE_REPORTS 400

/* --------------------------------------------------------------------------- */

static void xcp_notify(const char *message)
{
    OrbisNotificationRequest req;

    memset(&req, 0, sizeof(req));
    req.type     = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

#if XCP_CAPTURE_REPORTS > 0
static int     g_captured;
static uint8_t g_prev[XCP_USB_MAX_REPORT];
static size_t  g_prev_len;
static int     g_have_prev;

static void xcp_capture_report(const uint8_t *buf, size_t len)
{
    char   line[3 * XCP_USB_MAX_REPORT + 32];
    int    used;
    size_t i;

    if (g_captured >= XCP_CAPTURE_REPORTS) return;

    /*
     * Only record a change. An idle pad streams the same report hundreds of
     * times a second, and logging those buries the one line a button press
     * produces - which is the whole point of the capture: press M1, press M2,
     * ... and read off which bit moved.
     */
    if (g_have_prev && len == g_prev_len && memcmp(buf, g_prev, len) == 0) return;
    memcpy(g_prev, buf, len);
    g_prev_len  = len;
    g_have_prev = 1;

    used = snprintf(line, sizeof(line), XCP_TAG "xcpcapture %04d", g_captured);
    for (i = 0; i < len && used > 0 && (size_t)used + 3 < sizeof(line); i++) {
        used += snprintf(line + used, sizeof(line) - (size_t)used, " %02X",
                         (unsigned)buf[i]);
    }
    if (used > 0) printf("%s\n", line);

    g_captured++;
    if (g_captured == XCP_CAPTURE_REPORTS) {
        printf(XCP_TAG "capture complete. Paste every line containing xcpcapture "
                       "into a file and run: xcp_inspector --play capture.txt\n");
    }
}
#else
static void xcp_capture_report(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
}
#endif

/*
 * The single most useful diagnostic this payload has: what the console actually
 * sees on its USB bus. If the receiver is not the 045E:028E the PC reported, this
 * is where that shows up.
 */
static void xcp_dump_usb(xcp_usb_backend *usb)
{
    xcp_usb_device_info devs[XCP_USB_MAX_DEVICES];
    int                 count;
    int                 i;

    if (!usb || !usb->enumerate) {
        printf(XCP_TAG "no USB backend; the pad cannot be read\n");
        return;
    }

    count = usb->enumerate(NULL, devs, XCP_USB_MAX_DEVICES);
    printf(XCP_TAG "usb: %d device(s) visible\n", count);

    for (i = 0; i < count; i++) {
        printf(XCP_TAG "  usb[%d] %04X:%04X %s (%s)\n", i,
               (unsigned)devs[i].vid, (unsigned)devs[i].pid, devs[i].name,
               xcp_controller_type_name(devs[i].type));
    }

    if (count <= 0) {
        printf(XCP_TAG "usb: nothing at all. If the receiver is plugged in, "
                       "sceUsbd is not seeing it, and that is the problem.\n");
    }
}

/* --------------------------------------------------------------------------- */

/*
 * One-time process bring-up: load libSceMbus, scePadInit, and patch
 * scePadVirtualDeviceAddDevice. None of this creates a device, so none of it can
 * put anything on screen - safe to do before any input source exists.
 */
static int xcp_pad_setup_once(void)
{
    static int done = 0;

    if (done) return 0;

    if (xcp_vda_mbus_load() != 0) {
        xcp_notify("XCP: libSceMbus failed to load");
        return -1;
    }

    if (xcp_vda_pad_init() != 0) {
        xcp_notify("XCP: scePadInit failed");
        return -1;
    }

    /*
     * The one change that makes the rest possible: without it AddDevice always
     * answers 0x803B0006 ("assignment screen pending") and this process owns no
     * pad. Must be in place before the first AddDevice.
     */
    if (xcp_pad_patch_vda() != 0) {
        xcp_notify("XCP: libScePad patch failed");
        return -1;
    }

    done = 1;
    return 0;
}

/*
 * Create the virtual pad and learn its handle.
 *
 * The order here is not cosmetic: bring-up first (see xcp_pad_setup_once),
 * then the kernel log, then AddDevice. Creating the device is what raises the
 * console's assignment prompt, so this is only ever called while a real
 * controller is present to feed - and thereby confirm - it.
 */
static int xcp_create_virtual_pad(xcp_vda_device *out)
{
    xcp_klog_watch watch;
    uint64_t       device_id;

    if (xcp_pad_setup_once() != 0) return -1;

    if (xcp_klog_watch_open(&watch) != 0) {
        /* The usual reason: a klog client (GoldHEN's log server) holds it. */
        xcp_notify("XCP: /dev/klog busy - no klog client, retry");
        printf(XCP_TAG "without /dev/klog the handle cannot be learned, and a "
                       "device we cannot address is worse than none. Not "
                       "creating one.\n");
        return -1;
    }

    /* Anything already in the log is older than the device we are about to
     * create and must never be mistaken for it. */
    xcp_klog_watch_drain(&watch);

    /* The marker goes in BEFORE the call, not after: with the libScePad patch
     * applied, AddDevice adds the device synchronously, so the kernel logs the
     * event while the call is still running. The reader below only trusts an
     * event that follows the marker, so a marker written afterwards would make
     * us discard our own device. */
    xcp_klog_watch_mark(XCP_TAG "marker add");

    xcp_vda_add_device(XCP_VDA_USER_SELECT);

    device_id = xcp_klog_watch_wait_for_add(&watch, NULL, XCP_KLOG_WAIT_MS);

    /* Let GoldHEN's log server have the device back the moment we are done. */
    xcp_klog_watch_close(&watch);

    if (!device_id) {
        printf(XCP_TAG "the kernel log never named our device; not creating one "
                       "this time.\n");
        return -1;
    }

    xcp_vda_adopt(out, device_id);

    /*
     * AddDevice leaves the device ownerless, which is what puts the console's
     * "Who's using this controller?" prompt on screen. The login manager's own
     * way of answering that is
     *
     *     sceMbusBindDeviceWithUserId(virtualDeviceId, userId)
     *
     * but on this console a call to it issued right after AddDevice has not been
     * observed to return - the log stops at "device adopted" and the loop below
     * never runs. Until we know whether it blocks or faults (see the note in
     * PROGRESS.md), it stays off, and the prompt is answered by pressing a
     * button on the controller we are feeding: the system sees a button press on
     * the new device and assigns it, exactly as it would for a real pad.
     */
#define XCP_VDA_TRY_BIND 0
#if XCP_VDA_TRY_BIND
    printf(XCP_TAG "binding device 0x%llX to user 0x%08X...\n",
           (unsigned long long)out->device_id, (unsigned)XCP_VDA_USER_PRIMARY);
    if (xcp_vda_bind_device(out, XCP_VDA_USER_PRIMARY) != 0) {
        printf(XCP_TAG "mbus bind failed; the assignment prompt may stay up\n");
    }
#else
    printf(XCP_TAG "bind skipped: press a button on the controller to accept the "
                   "assignment prompt\n");
#endif

    return 0;
}

/* --------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    xcp_vda_device      dev;
    xcp_usb_backend    *usb;
    void               *handle = NULL;
    xcp_controller_type type    = XCP_CTRL_NONE;
    uint64_t            last_scan = 0;
    uint64_t            last_try  = 0;
    uint64_t            last_input_us = 0;
    int                 have_state = 0;
    uint32_t            touchpad_prev = 0;
    int                 touchpad_latched = 0;
    uint64_t            touchpad_last_us = 0;
    int                 logged_insert = 0;
    xcp_pad_state       state;

    (void)argc;
    (void)argv;

    /*
     * The very first thing: a visible sign, on screen, that our code ran at all.
     * If this never appears, the ELF is not executing our main - a loader or
     * GoldHEN-version problem, not this payload's logic.
     */
    xcp_notify("XCP: payload running");

    /*
     * Open our own log first, before anything that can fail, so the file exists
     * even if the run stops early. Its absence afterwards is itself the finding:
     * it would mean this code never ran.
     */
    xcp_log_open();

    printf("\n");
    printf(XCP_TAG "=============================================\n");
    printf(XCP_TAG "virtual-pad payload starting\n");
    printf(XCP_TAG "sizeof(OrbisPadData) = %u\n", (unsigned)sizeof(OrbisPadData));
    printf(XCP_TAG "log file: %s\n", XCP_PAYLOAD_LOG_PATH);
    printf(XCP_TAG "=============================================\n");

    /* Bring USB up first: its listing is the diagnostic we want even if the
     * rest of the bring-up fails. */
    usb = xcp_usb_sceusbd_backend();
    if (usb && usb->init && usb->init(NULL) != XCP_USB_OK) {
        printf(XCP_TAG "sceUsbd init failed; the pad cannot be read\n");
        usb = NULL;
    }
    xcp_dump_usb(usb);

    /*
     * No virtual pad yet, and none until a real controller is open. An ownerless
     * virtual device is exactly what raises the assignment prompt, and a prompt
     * with nothing attached to answer it is a hung console. So the rule is: the
     * virtual pad lives exactly as long as the input source it stands in for.
     */
    memset(&dev, 0, sizeof(dev));
    dev.handle = -1;

    memset(&state, 0, sizeof(state));
    xcp_pad_state_centre_sticks(&state);

    for (;;) {
        uint64_t now = sceKernelGetProcessTime();

        /* --- find the input source ------------------------------------- */
        if (!handle && usb && now - last_scan >= XCP_RESCAN_INTERVAL_US) {
            xcp_usb_device_info info;

            last_scan = now;
            if (xcp_usb_find(usb, NULL, XCP_CTRL_NONE, &info)) {
                handle = usb->open(NULL, &info);
                if (!handle) {
                    printf(XCP_TAG "found %s but could not open it\n", info.name);
                } else {
                    type = info.type;
                    if (usb->start) usb->start(NULL, handle, type);
                    last_input_us = now;
                    touchpad_latched = 0;
                    touchpad_prev = 0;
                    printf(XCP_TAG "input source ready: %s (%s)\n",
                           info.name, xcp_controller_type_name(type));
                    xcp_notify("XCP: controller detected");
                }
            }
        }

        /* --- keep the virtual pad in step with the input source -------- */
        if (handle && !dev.owned && now - last_try >= XCP_PAD_RETRY_INTERVAL_US) {
            last_try = now;
            if (xcp_create_virtual_pad(&dev) == 0) {
                xcp_notify("XCP: virtual pad ready - press a button");
            }
        }
        if (!handle && dev.owned) {
            printf(XCP_TAG "input source gone; removing the virtual pad so the "
                           "assignment prompt clears\n");
            xcp_vda_remove(&dev);
            have_state = 0;
            xcp_notify("XCP: controller lost, virtual pad removed");
        }

        /* --- read input ------------------------------------------------- */
        if (handle) {
            uint8_t report[XCP_USB_MAX_REPORT];
            size_t  len = 0;
            int     ret = usb->read(NULL, handle, report, sizeof(report), &len,
                                    XCP_READ_TIMEOUT_MS);

            if (ret == XCP_USB_OK) {
                last_input_us = now;
                xcp_capture_report(report, len);
                if (xcp_decode(type, report, len, NULL, &state)) {
                    /*
                     * The Manba's M-buttons are firmware macros: an "L3+R3"
                     * mapping arrives as a burst (L3, then L3+R3, then R3, then
                     * neither) instead of a held chord, so the raw chord is
                     * true for about one report and a game never sees the
                     * click. Latch it on the first chord and hold it while
                     * either stick click is still coming; the timeout is
                     * applied below, every iteration, so it releases on time
                     * even when the pad sends nothing at all.
                     */
                    if (state.buttons & XCP_BTN_TOUCHPAD) {
                        touchpad_latched = 1;
                        touchpad_last_us = now;
                    } else if (state.buttons & (XCP_BTN_L3 | XCP_BTN_R3)) {
                        touchpad_last_us = now;
                    }
                    have_state = 1;
                }
                /* A report that does not decode is ignored on purpose: some pads
                 * interleave vendor messages with input reports. */
            } else if (ret == XCP_USB_DISCONNECT) {
                /*
                 * One failed transfer is not a removal. This pad - and the
                 * system's own driver, which also wants it - hiccups, and
                 * tearing the virtual pad down and back up on every hiccup is
                 * what made the console thrash through device ids. Only believe
                 * the source is gone once nothing has decoded for a full second.
                 */
                if (now - last_input_us > XCP_INPUT_DEAD_US) {
                    printf(XCP_TAG "input source really gone (no data for %u ms)\n",
                           (unsigned)((now - last_input_us) / 1000));
                    usb->close(NULL, handle);
                    handle     = NULL;
                    have_state = 0;
                    touchpad_latched = 0;
                    touchpad_prev = 0;
                }
            }
        }

        /*
         * Touchpad-click release, evaluated every iteration rather than only
         * when a report arrives, so it cannot stick if the pad goes quiet right
         * after a click. While latched, L3/R3 are masked out of the sample so a
         * macro that emits them alongside the chord does not also fire them.
         */
        if (touchpad_latched &&
            now - touchpad_last_us > XCP_TOUCHPAD_HOLD_US) {
            touchpad_latched = 0;
        }
        if (have_state) {
            if (touchpad_latched) {
                state.buttons &= ~(XCP_BTN_L3 | XCP_BTN_R3);
                state.buttons |= XCP_BTN_TOUCHPAD;
            } else {
                state.buttons &= ~XCP_BTN_TOUCHPAD;
            }
            if (touchpad_latched != (int)touchpad_prev) {
                touchpad_prev = (uint32_t)touchpad_latched;
                printf(XCP_TAG "touchpad click %s\n",
                       touchpad_latched ? "down" : "up");
            }
        }

        if (dev.owned) {
            int32_t ins = xcp_vda_insert_state(&dev, have_state ? &state : NULL);

            /* Report the first fed sample once, so the log proves whether the
             * virtual pad accepts data at all - the whole point of this run. */
            if (!logged_insert && have_state) {
                printf(XCP_TAG "first InsertData(handle=0x%08X) = 0x%08X\n",
                       (unsigned)dev.handle, (unsigned)ins);
                logged_insert = 1;
            }
        }

        sceKernelUsleep(have_state ? XCP_POLL_INTERVAL_US : XCP_IDLE_INTERVAL_US);
    }
}
