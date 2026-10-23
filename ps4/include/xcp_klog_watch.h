/*
 * ps4/include/xcp_klog_watch.h - reading the kernel log while a device is
 * created.
 *
 * The handle AddDevice produced is only announced in the kernel log, so we have
 * to read it. GoldHEN's TCP log server on 127.0.0.1:3232 is tried first;
 * /dev/klog is the fallback and has a single reader, so it is opened only
 * around AddDevice and closed immediately afterwards.
 */
#ifndef XCP_KLOG_WATCH_H
#define XCP_KLOG_WATCH_H

#include <stdint.h>

/* Longest line we keep. Longer lines are truncated and then ignored. */
#define XCP_KLOG_LINE_MAX 512

typedef struct xcp_klog_watch {
    int32_t fd;
    int     is_socket;
    char    line[XCP_KLOG_LINE_MAX];
    unsigned used;
    int      saw_marker;
} xcp_klog_watch;

/* Opens GoldHEN's klog TCP server, falling back to /dev/klog. Returns 0 on
 * success, -1 otherwise. */
int xcp_klog_watch_open(xcp_klog_watch *w);

void xcp_klog_watch_close(xcp_klog_watch *w);

/* Read and discard whatever is already buffered, so a stale event cannot be
 * mistaken for the one our AddDevice call has just produced. */
void xcp_klog_watch_drain(xcp_klog_watch *w);

/*
 * Write a marker line into the kernel log. It is emitted before AddDevice only
 * for troubleshooting; drain() already marks the read position as current.
 */
void xcp_klog_watch_mark(const char *marker);

/*
 * Read until the reader has proved itself (the marker line came back) and a
 * virtual-pad event arrives, or `timeout_ms` elapses. Returns the DeviceId, or 0
 * on timeout. `marker` is written into the kernel log first; pass NULL to skip
 * that step.
 */
uint64_t xcp_klog_watch_wait_for_add(xcp_klog_watch *w, const char *marker,
                                     int timeout_ms);

#endif /* XCP_KLOG_WATCH_H */
