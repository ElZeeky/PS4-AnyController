/*
 * ps4/dumper/dump.c - dump the bytes of the libScePad functions the VDA
 * patch touches, plus the MBus bind pair, to /data.
 *
 * Build with `./build.sh payload dump`.
 *
 * This is the second cut and it is deliberately dumber than the first: the first
 * tried to find each module's ELF header by scanning backwards from an imported
 * symbol, and the scan walked off the mapped region and killed the payload.
 *
 * It is not needed anyway. The patch works entirely inside
 * scePadVirtualDeviceAddDevice: a call to redirect and a code cave in another
 * function's NOP tail padding (0x90 filler after a `ret`, not int3). Both
 * offsets are relative to the function's own address,
 * and that address comes straight from the linker via dlsym - no memory scan, no
 * ELF header, no module base. So this dumps exactly the function bytes that the
 * offline analysis needs, and nothing else.
 *
 * This payload never calls scePadVirtualDevice*: no device is created, so there
 * is no assignment screen and nothing is left behind.
 */
#include <orbis/libkernel.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Defined by the SDK CRT that is linked in (crt/klog.c). */
int klog_printf(const char *fmt, ...);

/* Imported only to obtain addresses inside the modules. Never called. */
extern int32_t   scePadInit(void);
extern void      scePadVirtualDeviceAddDevice(void);
extern int32_t   sceMbusDisconnectDevice(uint64_t deviceId);
extern int32_t   sceMbusBindDeviceWithUserId(uint64_t deviceId, int32_t userId);

#define XCP_DUMP_LOG "/data/xcp_dump.log"

static int32_t g_log_fd = -1;

static void dl(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) return;

    if (g_log_fd >= 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        (void)sceKernelWrite(g_log_fd, buf, len);
    }
    klog_printf("%s", buf);
}

static void notify(const char *message)
{
    OrbisNotificationRequest req;
    unsigned                 i;

    memset(&req, 0, sizeof(req));
    req.type     = NotificationRequest;
    req.targetId = -1;
    for (i = 0; i + 1 < sizeof(req.message) && message[i]; i++) {
        req.message[i] = message[i];
    }
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void write_all(int32_t fd, const void *buf, uint32_t len)
{
    const unsigned char *p = (const unsigned char *)buf;

    while (len) {
        int64_t n = sceKernelWrite(fd, p, len);
        if (n <= 0) break;
        p   += n;
        len -= (uint32_t)n;
    }
}

static void dump_bytes(const char *tag, const void *addr, uint32_t len)
{
    char    path[128];
    int32_t fd;
    char    msg[96];

    dl("dump: %s addr=%p len=0x%X\n", tag, addr, (unsigned)len);

    snprintf(path, sizeof(path), "/data/%s.bin", tag);

    /* Report the outcome on screen too: the whole point of this build is to
     * learn whether writing to /data works from this process, and that must not
     * depend on the file we could not find. */
    fd = sceKernelOpen(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        snprintf(msg, sizeof(msg), "XCP: %s open %d", tag, (int)fd);
        notify(msg);
        dl("dump: open %s failed (%d)\n", path, (int)fd);
        return;
    }
    write_all(fd, addr, len);
    sceKernelClose(fd);

    snprintf(msg, sizeof(msg), "XCP: %s ok %u", tag, (unsigned)len);
    notify(msg);
    dl("dump: %s wrote %s\n", tag, path);
}

int main(void)
{
    notify("XCP DUMP: running");

    g_log_fd = sceKernelOpen(XCP_DUMP_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    dl("=============================================\n");
    dl("xcp function dump: start\n");
    dl("log file: " XCP_DUMP_LOG "\n");
    dl("=============================================\n");

    /* The one that matters: the function the patch rewrites. */
    dump_bytes("scePadVirtualDeviceAddDevice",
               (const void *)&scePadVirtualDeviceAddDevice, 0x400);
    notify("XCP DUMP: vda done");

    /* Context: neighbours, in case the layout differs from the PS5 write-up. */
    dump_bytes("scePadInit", (const void *)&scePadInit, 0x100);
    dump_bytes("sceMbusDisconnectDevice",
               (const void *)&sceMbusDisconnectDevice, 0x100);
    dump_bytes("sceMbusBindDeviceWithUserId",
               (const void *)&sceMbusBindDeviceWithUserId, 0x100);

    notify("XCP DUMP: done");

    dl("xcp function dump: done\n");

    if (g_log_fd >= 0) sceKernelClose(g_log_fd);
    return 0;
}
