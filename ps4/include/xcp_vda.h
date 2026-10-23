/*
 * ps4/include/xcp_vda.h - the virtual DualShock 4 (the "VDA" API), payload only.
 *
 * Why this exists at all
 * ---------------------
 * A GoldHEN .prx plugin only ever runs inside a game process, so it can never
 * touch the home screen, the settings menus or the "Who's using this
 * controller?" screen: those belong to system processes that never load
 * plugins. The one API that a payload CAN use to reach all of them is Sony's own
 * virtual-device API, the one Remote Play goes through. A device created with it
 * is a real MBus device, and the whole system sees it exactly like a DualShock 4
 * plugged in over USB - menus, sign-in and games alike.
 *
 * These prototypes are ours, because OpenOrbis declares the four functions with
 * an empty parameter list (orbis/Pad.h) and no other public header declares them
 * at all. The signatures below are the ones that are actually called; the stub
 * libScePad.so in the toolchain exports all four names, so the link resolves.
 *
 * Do NOT include <orbis/Pad.h> in a payload file: its `void f();` declarations
 * of the same symbols are incompatible with these.
 */
#ifndef XCP_VDA_H
#define XCP_VDA_H

#include <stdint.h>

#include <orbis/_types/pad.h>

#include "xcp/pad_state.h"

/* --- Sony's API, as we call it ------------------------------------------- */

int32_t scePadInit(void);
int32_t scePadSetProcessPrivilege(int32_t privilege);

/*
 * Returns a status, NOT a handle. On every console it has been tried on it
 * returns 0x803b0006 even when the device WAS created, so a non-zero return is
 * not by itself a failure: the caller has to wait for the kernel-log event that
 * names the device (see xcp_klog_watch.h).
 *
 * `param` is a pointer to xcp_vda_add_param (32 bytes); device_type is 1 on
 * the direct path, 3 on the patched fallback.
 */
int32_t scePadVirtualDeviceAddDevice(void *param, int32_t device_type);
int32_t scePadVirtualDeviceDeleteDevice(int32_t handle);
int32_t scePadVirtualDeviceInsertData(int32_t handle, const void *pad_data);
int32_t scePadVirtualDeviceGetRemoteSetting(int32_t handle, void *setting);

/* --- Tunables ------------------------------------------------------------ */

/* Device types: 1 is what Remote Play uses on 13.52; 3 is the type confirmed
 * on 11.00. The direct path uses 1, the patched fallback uses 3. */
#define XCP_VDA_DEVICE_TYPE_DIRECT 1
#define XCP_VDA_DEVICE_TYPE_LEGACY 3
#define XCP_VDA_DEVICE_TYPE XCP_VDA_DEVICE_TYPE_LEGACY

/*
 * Creating the device for user 1 makes it arrive UNASSIGNED, so the PS4 opens
 * its own native user picker ("Who's using this controller?"). That picker is
 * then closed by binding the device to a user (xcp_vda_bind_device below), the
 * same way the login manager does when a person accepts a controller.
 */
#define XCP_VDA_USER_SELECT 1

/*
 * The first local user's account id. PS4 user ids run 0x10000000, 0x20000000,
 * ... in the order the accounts were created; the primary user is 0x10000000.
 */
#define XCP_VDA_USER_PRIMARY 0x10000000u

/* libScePad imports from libSceMbus, so this has to be loaded first. */
#define XCP_VDA_MBUS_PATH "/system/common/lib/libSceMbus.sprx"

/* --- Our wrapper -------------------------------------------------------- */

typedef struct xcp_vda_add_param {
    int32_t size;    /* must be the size of this struct */
    int32_t user_id; /* XCP_VDA_USER_SELECT */
    int32_t pad[6];  /* unknown; zeroed, and logged so a change would be seen */
} xcp_vda_add_param;

typedef struct xcp_vda_device {
    int32_t  handle;    /* what InsertData takes: low 32 bits of the DeviceId */
    uint64_t device_id; /* the MBus DeviceId, read out of the kernel log */
    int      owned;     /* we created it, so we delete it */
} xcp_vda_device;

/*
 * Load libSceMbus. MUST run before any scePad* call: libScePad's imports from it
 * are unresolved in a payload, and calling into libScePad first can kill the
 * hijacked process. Returns 0 on success.
 */
int xcp_vda_mbus_load(void);

/*
 * scePadInit(), then scePadSetProcessPrivilege(1) - in that order, because the
 * privilege call fails with "not initialised" the other way round. The privilege
 * is what lets an unsigned process talk to the pad service at all: without it
 * the AddDevice calls are refused. Returns 0 if scePadInit succeeded.
 */
int xcp_vda_pad_init(void);

/*
 * The AddDevice call itself. Returns its raw status, which is non-zero even on
 * success - see the note on scePadVirtualDeviceAddDevice above. Never fatal:
 * the caller waits for the kernel-log event either way.
 */
int32_t xcp_vda_add_device(int32_t user_id, int32_t device_type, int sentinel);

int xcp_vda_creds_raise(void);
int xcp_vda_creds_restore(void);
int xcp_vda_creds_active(void);

/*
 * Fill in `out` from the DeviceId the kernel log announced. No PS4 calls.
 */
void xcp_vda_adopt(xcp_vda_device *out, uint64_t device_id);

/* DeleteDevice, if we created the device. */
void xcp_vda_remove(xcp_vda_device *dev);

/*
 * Hand the virtual device an owner, exactly as the login manager does when a
 * person accepts a controller:
 *
 *     sceMbusBindDeviceWithUserId(virtualDeviceId, userId)
 *
 * This is what dismisses the "Who's using this controller?" prompt and makes the
 * pad that user's controller. AddDevice alone leaves the device ownerless and
 * the prompt up, so this call is not optional. Returns the raw status (0 is
 * success).
 */
int32_t xcp_vda_bind_device(const xcp_vda_device *dev, uint32_t user_id);

/*
 * sceMbusDisconnectDevice(deviceId): detach a device from whoever owns it. Used
 * on the physical pad so the user slot it holds is free before the virtual one
 * is bound to that user.
 */
int32_t xcp_vda_disconnect_device(uint64_t device_id);

/*
 * A neutral sample: sticks centred, identity rotation, connected. Everything the
 * system may read is defined, so a pad that has no input source yet still shows
 * up as a live, unmoving controller.
 */
void xcp_vda_neutral(OrbisPadData *data);

/*
 * One report to the console. `state` may be NULL, or disconnected, which yields
 * the neutral sample: the virtual pad must never look unplugged just because our
 * input source is not attached - it is the console's own pad.
 *
 * The sample carries a rising timestamp and counter, as a real pad's do; a
 * constant one is what anything that de-duplicates reports reads as "no new
 * data". Returns InsertData's raw return value.
 */
int32_t xcp_vda_insert_state(xcp_vda_device *dev, const xcp_pad_state *state);

#endif /* XCP_VDA_H */
