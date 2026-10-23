/*
 * xcp/klog_line.h - reading meaning out of a kernel-log line.
 *
 * `scePadVirtualDeviceAddDevice` returns a status, not a handle: on the consoles
 * it has been tried on it returns 0x803b0006 even when the device WAS created.
 * The only place a payload can learn the handle it now has to address is the
 * kernel log, where the login manager announces the new device:
 *
 *   #LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x7030d][type:1][subType:2]
 *
 * The handle `scePadVirtualDeviceInsertData` takes is the low 32 bits of that
 * DeviceId.
 *
 * This is pure string work with no platform dependency, which is the point: the
 * host tests exercise exactly the code the console runs.
 */
#ifndef XCP_KLOG_LINE_H
#define XCP_KLOG_LINE_H

#include <stdint.h>

/*
 * Is `line` the login manager's "a device was added" event for a pad created
 * through the virtual-device API?
 *
 * The match is deliberately strict. type 1 is a controller and subType 2 is the
 * Remote Play pad, i.e. the kind of device we create. The DeviceId must also
 * have a non-zero low byte: pads on the console's own ports re-register as
 * 0x30300 / 0x90300, while virtual ids look like 0x5030D / 0x7030D. Nothing
 * looser is accepted, because a real pad or another MBus device appearing in the
 * same window would otherwise hand us a DeviceId that is not ours - and every
 * button we then reported would go to a device we do not own.
 */
int xcp_klog_is_virtual_pad_added(const char *line);

/* The DeviceId named in `line`, or 0 when it names none. */
uint64_t xcp_klog_device_id(const char *line);

#endif /* XCP_KLOG_LINE_H */
