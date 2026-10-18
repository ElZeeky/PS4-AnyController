/*
 * ps4/payload/xcp_klog_watch.c - see xcp_klog_watch.h.
 *
 * File I/O goes through libkernel (sceKernelOpen/Read/Close) rather than the C
 * library's open()/read(): those symbols are not in the sysroot's stub libs,
 * whereas the kernel ones are both declared and exported.
 */
#include "xcp_klog_watch.h"
#include "xcp_log.h"

#include "xcp/klog_line.h"

#include <fcntl.h>

#include <orbis/libkernel.h>

#include <stdio.h>
#include <string.h>

#define XCP_KLOG_DEVICE "/dev/klog"

/* How often the wait loop polls the device while it waits. */
#define XCP_KLOG_POLL_US 2000

int xcp_klog_watch_open(xcp_klog_watch *w)
{
    if (!w) return -1;

    memset(w, 0, sizeof(*w));
    w->fd = sceKernelOpen(XCP_KLOG_DEVICE, O_RDONLY | O_NONBLOCK, 0);
    if (w->fd < 0) {
        printf(XCP_TAG "open(" XCP_KLOG_DEVICE ") failed (%d). Another reader has "
                       "it - GoldHEN's log server does, while a client is "
                       "connected - and without it we cannot learn the handle.\n",
               (int)w->fd);
        return -1;
    }
    return 0;
}

void xcp_klog_watch_close(xcp_klog_watch *w)
{
    if (!w) return;
    if (w->fd >= 0) {
        sceKernelClose(w->fd);
        w->fd = -1;
    }
    w->used = 0;
}

/*
 * Feed one complete line to the caller's matcher. Returns the DeviceId when it
 * is the virtual-pad event and the reader has already proved itself, else 0.
 */
static uint64_t xcp_klog_line(xcp_klog_watch *w, const char *line)
{
    if (strstr(line, "xcp: marker")) {
        w->saw_marker = 1;
        return 0;
    }

    if (xcp_klog_is_virtual_pad_added(line)) {
        uint64_t id = xcp_klog_device_id(line);
        printf(XCP_TAG "klog pad-added: %s\n", line);
        printf(XCP_TAG "  parsed DeviceId = 0x%llX\n", (unsigned long long)id);
        if (!w->saw_marker) {
            /* The event we just matched happened BEFORE we could read, so it
             * cannot be ours: refuse it rather than trust a timeout. */
            printf(XCP_TAG "ignoring a device event that predates our reader\n");
            return 0;
        }
        return id;
    }
    return 0;
}

/* Pull one chunk and hand back every complete line in it. */
static uint64_t xcp_klog_pump(xcp_klog_watch *w)
{
    char   buf[512];
    size_t got;
    size_t i;
    uint64_t found = 0;

    got = sceKernelRead(w->fd, buf, sizeof(buf));
    if (got == 0 || got > sizeof(buf)) return 0; /* nothing, or an error code */

    for (i = 0; i < got; i++) {
        char c = buf[i];

        if (c == '\n' || c == '\r') {
            if (w->used) {
                w->line[w->used] = '\0';
                if (!found) found = xcp_klog_line(w, w->line);
                w->used = 0;
            }
            continue;
        }
        if (w->used + 1 < sizeof(w->line)) {
            w->line[w->used++] = c;
        }
        /* A line longer than the buffer is truncated and its remainder will be
         * treated as a new line, which simply will not match anything. */
    }
    return found;
}

void xcp_klog_watch_drain(xcp_klog_watch *w)
{
    if (!w || w->fd < 0) return;

    for (;;) {
        char   buf[512];
        size_t got = sceKernelRead(w->fd, buf, sizeof(buf));
        if (got == 0 || got > sizeof(buf)) break;
    }
    w->used = 0;
}

void xcp_klog_watch_mark(const char *marker)
{
    if (marker) {
        printf("%s\n", marker);
    }
}

uint64_t xcp_klog_watch_wait_for_add(xcp_klog_watch *w, const char *marker,
                                     int timeout_ms)
{
    int waited = 0;

    if (!w || w->fd < 0) return 0;

    xcp_klog_watch_mark(marker);

    while (waited <= timeout_ms) {
        uint64_t id = xcp_klog_pump(w);
        if (id) return id;

        sceKernelUsleep(XCP_KLOG_POLL_US);
        waited += XCP_KLOG_POLL_US / 1000;
    }

    printf(XCP_TAG "no device event within %d ms\n", timeout_ms);
    return 0;
}
