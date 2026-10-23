/*
 * xcp/translate.h - the whole "core" contract: bytes in, pad state out.
 *
 * Mapping is the reference plugin's mapping (crucifix86/xbox_controller_plugin,
 * MIT, kept as credit in NOTICE), rewritten against the *verified* OrbisPadData
 * field names and with the platform-specific parts removed.
 */
#ifndef XCP_TRANSLATE_H
#define XCP_TRANSLATE_H

#include <stddef.h>
#include <stdint.h>

#include "xcp/config.h"
#include "xcp/pad_state.h"
#include "xcp/report.h"

/*
 * Decode one raw report.
 *
 * Returns 1 when `buf` was a valid report of `type` and `out` was filled in,
 * 0 when it was not. On 0 the contents of `out` are unspecified - callers that
 * care should keep their previous state.
 *
 * `type` must not be XCP_CTRL_NONE. `cfg` may be NULL, meaning: defaults for
 * the controller family (xcp_config_default for Xbox, ..._switch for Switch
 * PDP, ..._switch_pro for Switch Pro, ..._dualsense for DualSense).
 */
int xcp_decode(xcp_controller_type type,
               const uint8_t *buf, size_t len,
               const xcp_config *cfg,
               xcp_pad_state *out);

/*
 * Build the config for a freshly opened source. `previous` is the type of the
 * source that was open before it, or XCP_CTRL_NONE on the first connection.
 * This is where device-specific transition quirks live; keep them out of the
 * decoders' defaults.
 */
void xcp_config_for_source(xcp_controller_type type,
                           xcp_controller_type previous,
                           xcp_config *cfg);

/*
 * Is `buf` plausibly a report belonging to `type`? Cheaper than a full decode
 * and used by the polling loop to skip garbage without touching pad state.
 */
int xcp_report_looks_valid(xcp_controller_type type, const uint8_t *buf, size_t len);

/* --- Small pieces, exposed because they are worth testing alone ----------- */

/* Stick value with the deadzone applied, PS4 convention (centre 128). */
uint8_t xcp_apply_deadzone(uint8_t value, uint8_t deadzone);

/* Signed 16-bit Xbox axis -> unsigned 8-bit PS4 axis (centre 128). */
uint8_t xcp_stick_from_signed(int16_t value);

/* 10-bit GIP trigger -> 8-bit PS4 trigger. */
uint8_t xcp_trigger_from_10bit(uint16_t value);

/* D-pad bits (bit0 up, bit1 down, bit2 left, bit3 right) -> XCP_BTN_* mask. */
uint32_t xcp_dpad_mask_from_bits(uint8_t bits);

/* Switch hat (0..7, >=8 centred) -> XCP_BTN_* mask. */
uint32_t xcp_switch_hat_mask(uint8_t hat);

#endif /* XCP_TRANSLATE_H */
