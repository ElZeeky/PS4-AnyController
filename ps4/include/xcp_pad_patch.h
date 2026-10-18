/*
 * ps4/include/xcp_pad_patch.h - the in-place libScePad patch that makes
 * scePadVirtualDeviceAddDevice usable from a payload.
 *
 * Why this exists
 * ---------------
 * On a retail console, scePadVirtualDeviceAddDevice() runs its IPC exchange
 * with the pad service, and the service replies with what it decided the
 * caller is allowed to do. From a payload (a process that never went through
 * the sign-in/assignment flow) that reply is always
 *
 *     0x803B0006  SCE_PAD_ERROR_ ... "the controller-assignment screen is
 *                 still pending"
 *
 * The device itself is created - the pub/sub container is populated and the
 * kernel log announces a new DualShock 4 - but the error sentinel makes
 * libScePad hand the caller no handle, so nothing can be fed to it.
 *
 * The fix (the same one Ghostpad uses) is to drop the sentinel: let the IPC
 * exchange happen exactly as before, then force the function's return value to
 * 0. That is a two-byte-value change of control flow inside one function, and
 * it is done without touching the caller:
 *
 *   * the call instruction at    vda + 0x00C0  is retargeted,
 *   * to a 10-byte stub placed in the function's own 15-byte NOP tail padding
 *     at                         libScePad + 0x2F21  (dead bytes, right after
 *     the preceding function's `ret`),
 *   * and the stub re-issues the original call, then zeroes eax and returns.
 *
 * Everything is expressed relative to the address of
 * scePadVirtualDeviceAddDevice itself, which the linker hands us directly, so
 * no module base, no ELF header and no memory scan is involved.
 *
 * See ps4/payload/xcp_pad_patch.c for the exact bytes and PROGRESS.md for the
 * offsets, which are specific to firmware 11.00.
 */

#ifndef XCP_PAD_PATCH_H
#define XCP_PAD_PATCH_H

/*
 * Patch scePadVirtualDeviceAddDevice in the calling process.
 *
 * Returns 0 when the patch is in place (or was already), and a negative value
 * otherwise; the callers only have to decide whether to continue. It never
 * throws: any mismatch - a different firmware, a module already patched by
 * something else - is reported and left untouched rather than written blindly.
 */
int xcp_pad_patch_vda(void);

#endif /* XCP_PAD_PATCH_H */
