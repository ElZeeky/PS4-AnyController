/*
 * ps4/include/xcp_log.h - console logging.
 *
 * Two build flavours share this header:
 *
 *  * the payload (XCP_PAYLOAD defined), which has no GoldHEN SDK;
 *  * a plugin-style build, where the SDK's Utilities.h defines `klog` as
 *    printf and GoldHEN routes stdout to its log.
 *
 * The payload can NOT rely on printf. Its printf resolves to the hijacked
 * process's libc and lands on that process's stdout, which only reaches the
 * kernel log if GoldHEN's "TTY redirect" option happens to be on. And the kernel
 * log has a single reader, so a client watching it can stop the payload reading
 * it. Either way the user ends up with "it ran but nothing happened".
 *
 * So in the payload every line takes two paths that need neither TTY redirect
 * nor an exclusive reader:
 *   - to the kernel log, straight through the CRT's klog_printf (syscall);
 *   - to a file under /data (`xcp_log_open()`), which can be pulled over FTP.
 * If the file is absent after a run, our main never ran at all - which is the
 * fault (unresolved imports in the hijacked process) itself.
 */
#ifndef XCP_LOG_H
#define XCP_LOG_H

/* Utilities.h uses size_t in a prototype without including <stddef.h> itself. */
#include <stddef.h>

#include <stdio.h>

#define XCP_TAG "xcp: "

/* Where the payload keeps its own log, reachable over GoldHEN's FTP server. */
#define XCP_PAYLOAD_LOG_PATH "/data/AnyController.log"

#if defined(XCP_PAYLOAD)

/*
 * Open the payload log file (truncating any previous one). Returns 0 on success;
 * on failure logging still works, it just only reaches the kernel log.
 */
int  xcp_log_open(void);

void xcp_log_close(void);

/*
 * The one place a payload line is emitted: truncate-safe, unbuffered (so a
 * crash still leaves what happened on disk), to both the file and the klog.
 */
int  xcp_log_printf(const char *fmt, ...);

#define klog(...)   xcp_log_printf(__VA_ARGS__)
#define printf(...) xcp_log_printf(__VA_ARGS__)

#else

#include <Utilities.h>

#endif

#endif /* XCP_LOG_H */
