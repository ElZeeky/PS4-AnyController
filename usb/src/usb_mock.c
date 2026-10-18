/*
 * usb/src/usb_mock.c - scripted USB backend.
 *
 * Deterministic, no I/O. Used by the unit tests and by the PC CLI's replay
 * mode so the whole decode path can be exercised without hardware.
 */
#include "xcp/usb_backend.h"

#include <string.h>

#define XCP_MOCK_QUEUE_DEPTH 256

typedef struct mock_queue_entry {
    uint8_t buf[XCP_USB_MAX_REPORT];
    size_t  len;
} mock_queue_entry;

static mock_queue_entry g_queue[XCP_MOCK_QUEUE_DEPTH];
static int              g_queue_head;
static int              g_queue_count;
static uint16_t         g_vid = 0x045Eu;
static uint16_t         g_pid = 0x02EAu;
static int              g_connected = 1;
static int              g_open;
static int              g_read_count;
static int              g_start_count;

void xcp_usb_mock_reset(void)
{
    memset(g_queue, 0, sizeof(g_queue));
    g_queue_head   = 0;
    g_queue_count  = 0;
    g_vid          = 0x045Eu;
    g_pid          = 0x02EAu;
    g_connected    = 1;
    g_open         = 0;
    g_read_count   = 0;
    g_start_count  = 0;
}

void xcp_usb_mock_set_device(uint16_t vid, uint16_t pid)
{
    g_vid = vid;
    g_pid = pid;
}

void xcp_usb_mock_set_connected(int connected)
{
    g_connected = connected ? 1 : 0;
}

void xcp_usb_mock_push(const uint8_t *buf, size_t len)
{
    int slot;

    if (!buf || len == 0 || len > XCP_USB_MAX_REPORT) return;
    if (g_queue_count >= XCP_MOCK_QUEUE_DEPTH) return;

    slot = (g_queue_head + g_queue_count) % XCP_MOCK_QUEUE_DEPTH;
    memcpy(g_queue[slot].buf, buf, len);
    /* Zero the tail so a short report cannot leak a previous longer one. */
    if (len < XCP_USB_MAX_REPORT) {
        memset(g_queue[slot].buf + len, 0, XCP_USB_MAX_REPORT - len);
    }
    g_queue[slot].len = len;
    g_queue_count++;
}

int xcp_usb_mock_pending(void)
{
    return g_queue_count;
}

int xcp_usb_mock_read_count(void)
{
    return g_read_count;
}

static int mock_init(void *ctx)
{
    (void)ctx;
    g_open = 0;
    return XCP_USB_OK;
}

static void mock_shutdown(void *ctx)
{
    (void)ctx;
    g_open = 0;
}

static int mock_enumerate(void *ctx, xcp_usb_device_info *out, int max)
{
    (void)ctx;

    if (!g_connected || max < 1 || !out) return 0;

    memset(&out[0], 0, sizeof(out[0]));
    out[0].vid  = g_vid;
    out[0].pid  = g_pid;
    out[0].type = xcp_controller_type_for_ids(g_vid, g_pid);
    strncpy(out[0].path, "mock://0", XCP_USB_PATH_MAX - 1);
    strncpy(out[0].name, "mock device", XCP_USB_NAME_MAX - 1);
    return 1;
}

static void *mock_open(void *ctx, const xcp_usb_device_info *info)
{
    (void)ctx;
    (void)info;

    if (!g_connected) return NULL;
    g_open = 1;
    return &g_open; /* non-NULL opaque handle */
}

static int mock_start(void *ctx, void *handle, xcp_controller_type type)
{
    (void)ctx;
    (void)handle;
    (void)type;
    g_start_count++;
    return XCP_USB_OK;
}

static int mock_read(void *ctx, void *handle, uint8_t *buf, size_t buf_len,
                     size_t *out_len, int timeout_ms)
{
    mock_queue_entry *entry;

    (void)ctx;
    (void)timeout_ms;

    if (!handle || !buf || !out_len) return XCP_USB_ERROR;
    if (!g_connected) return XCP_USB_DISCONNECT;

    g_read_count++;

    if (g_queue_count == 0) return XCP_USB_NO_DATA;

    entry = &g_queue[g_queue_head];
    if (entry->len > buf_len) {
        /* Report too big for the caller's buffer: drop it rather than truncate. */
        g_queue_head = (g_queue_head + 1) % XCP_MOCK_QUEUE_DEPTH;
        g_queue_count--;
        return XCP_USB_ERROR;
    }

    memcpy(buf, entry->buf, entry->len);
    *out_len = entry->len;

    g_queue_head = (g_queue_head + 1) % XCP_MOCK_QUEUE_DEPTH;
    g_queue_count--;

    return XCP_USB_OK;
}

static void mock_close(void *ctx, void *handle)
{
    (void)ctx;
    (void)handle;
    g_open = 0;
}

static xcp_usb_backend g_mock = {
    "mock",
    mock_init,
    mock_shutdown,
    mock_enumerate,
    mock_open,
    mock_start,
    mock_read,
    mock_close,
};

xcp_usb_backend *xcp_usb_mock_backend(void)
{
    return &g_mock;
}

/* xcp_usb_find lives in usb_common.c, which the payload also links. */
