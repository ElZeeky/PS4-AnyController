/*
 * ps4/payload/xcp_klog_watch.c - see xcp_klog_watch.h.
 *
 * File I/O goes through libkernel (sceKernelOpen/Read/Close) rather than the C
 * library's open()/read(): those symbols are not in the sysroot's stub libs,
 * whereas the kernel ones are both declared and exported. GoldHEN's TCP log
 * server on 127.0.0.1:3232 is tried first so we do not have to take /dev/klog
 * away from it.
 */
#include "xcp_klog_watch.h"
#include "xcp_log.h"

#include "xcp/klog_line.h"

#include <fcntl.h>

#include <orbis/libkernel.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>

#define XCP_KLOG_DEVICE "/dev/klog"
#define XCP_KLOG_TCP_PORT 3232

/* How often the wait loop polls the device while it waits. */
#define XCP_KLOG_POLL_US 2000

static int xcp_klog_tcp_open(void)
{
    struct sockaddr_in addr;
    int                fd;
    int                flags;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(XCP_KLOG_TCP_PORT);
    addr.sin_addr.s_addr = htonl(0x7F000001u);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int xcp_klog_watch_open(xcp_klog_watch *w)
{
    if (!w) return -1;

    memset(w, 0, sizeof(*w));
    w->fd = xcp_klog_tcp_open();
    if (w->fd >= 0) {
        w->is_socket = 1;
        printf(XCP_TAG "klog: using GoldHEN TCP server 127.0.0.1:%d\n",
               XCP_KLOG_TCP_PORT);
        return 0;
    }

    w->fd = sceKernelOpen(XCP_KLOG_DEVICE, O_RDONLY | O_NONBLOCK, 0);
    if (w->fd < 0) {
        printf(XCP_TAG "open(" XCP_KLOG_DEVICE ") failed (%d), and GoldHEN's "
                       "TCP log server is not reachable; cannot learn the "
                       "device handle.\n",
               (int)w->fd);
        return -1;
    }
    printf(XCP_TAG "klog: using " XCP_KLOG_DEVICE "\n");
    return 0;
}

void xcp_klog_watch_close(xcp_klog_watch *w)
{
    if (!w) return;
    if (w->fd >= 0) {
        if (w->is_socket) close(w->fd);
        else              sceKernelClose(w->fd);
        w->fd = -1;
    }
    w->is_socket = 0;
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
static int64_t xcp_klog_read(xcp_klog_watch *w, char *buf, size_t size)
{
    if (w->is_socket) return (int64_t)recv(w->fd, buf, size, 0);
    return (int64_t)sceKernelRead(w->fd, buf, size);
}

static uint64_t xcp_klog_pump(xcp_klog_watch *w)
{
    char    buf[512];
    int64_t got;
    size_t  i;
    uint64_t found = 0;

    got = xcp_klog_read(w, buf, sizeof(buf));
    if (got <= 0 || (uint64_t)got > sizeof(buf)) return 0;

    for (i = 0; i < (size_t)got; i++) {
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
        char    buf[512];
        int64_t got = xcp_klog_read(w, buf, sizeof(buf));
        if (got <= 0 || (uint64_t)got > sizeof(buf)) break;
    }
    w->used       = 0;
    w->saw_marker = 1;
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
