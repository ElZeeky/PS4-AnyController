/*
 * ps4/include/xcp_klog_watch.h - reading /dev/klog while a device is created.
 *
 * The handle AddDevice produced is only announced in the kernel log, so we have
 * to read it. Two things make that delicate:
 *
 *  * /dev/klog has a SINGLE reader. GoldHEN's log server keeps it only while a
 *    client is connected and can go a long time without serving the next one, so
 *    while we hold the device open the user cannot read klog over the network -
 *    which is how they would send us the log. We therefore open it only around
 *    AddDevice and close it again immediately.
 *
 *  * The log carries everything the console does. We accept only the login
 *    manager's event for a pad (subType 2) - see xcp/klog_line.h - and we prove
 *    to ourselves that the reader really delivers by writing a marker line and
 *    waiting for it to come back before trusting anything else we read.
 */
#ifndef XCP_KLOG_WATCH_H
#define XCP_KLOG_WATCH_H

#include <stdint.h>

/* Longest line we keep. Longer lines are truncated and then ignored. */
#define XCP_KLOG_LINE_MAX 512

typedef struct xcp_klog_watch {
    int32_t fd;
    char    line[XCP_KLOG_LINE_MAX];
    unsigned used;
    int      saw_marker;
} xcp_klog_watch;

/* Opens /dev/klog non-blocking. Returns 0 on success, -1 otherwise. */
int xcp_klog_watch_open(xcp_klog_watch *w);

void xcp_klog_watch_close(xcp_klog_watch *w);

/* Read and discard whatever is already buffered, so a stale event cannot be
 * mistaken for the one our AddDevice call has just produced. */
void xcp_klog_watch_drain(xcp_klog_watch *w);

/*
 * Write the self-test marker (a line containing "xcp: marker") into the kernel
 * log. It has to be emitted BEFORE AddDevice: the libScePad patch makes
 * AddDevice add the device synchronously, so its event lands in the log during
 * the call, and the reader would otherwise see it before the marker and refuse
 * it as pre-existing.
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
