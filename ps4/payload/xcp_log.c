/*
 * ps4/payload/xcp_log.c - payload logging that survives a missing klog client.
 *
 * The problem this solves: in a payload, printf goes to the hijacked process's
 * stdout, which only reaches the kernel log if GoldHEN's TTY redirect happens to
 * be enabled - and the kernel log has a single reader, so a client watching it
 * can stop us reading it. The result on the console is "payload ran, nothing
 * happened" with nothing to look at.
 *
 * Every line therefore goes two ways:
 *   - to the kernel log directly, through the CRT's klog_printf (a syscall), so
 *     it shows up in GoldHEN's klog regardless of TTY redirect;
 *   - to /data/AnyController.log, which is pulled off the console over FTP.
 *
 * If that file is not there after a run, this code never executed at all: the
 * ELF failed to load or resolve imports in the hijacked process, which is itself
 * the answer.
 */
#include "xcp_log.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <orbis/libkernel.h>

/* Defined by the SDK CRT that is linked in (crt/klog.c). */
int klog_printf(const char *fmt, ...);

static int32_t g_log_fd = -1;

int xcp_log_open(void)
{
    /* Truncate each run so the file is never a mixture of two attempts. */
    g_log_fd = sceKernelOpen(XCP_PAYLOAD_LOG_PATH,
                             O_WRONLY | O_CREAT | O_TRUNC,
                             (OrbisKernelMode)0666);
    return g_log_fd >= 0 ? 0 : -1;
}

void xcp_log_close(void)
{
    if (g_log_fd >= 0) {
        sceKernelClose(g_log_fd);
        g_log_fd = -1;
    }
}

int xcp_log_printf(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) return n;

    if (g_log_fd >= 0) {
        /* Unbuffered on purpose: a crash mid-run still leaves what reached here. */
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        (void)sceKernelWrite(g_log_fd, buf, len);
    }

    if (n >= (int)sizeof(buf)) {
        /* Mark truncation so a cut line is never read as a complete one. */
        klog_printf("%s\n", buf);
        klog_printf("[...]\n");
    } else {
        klog_printf("%s", buf);
    }

    return n;
}
