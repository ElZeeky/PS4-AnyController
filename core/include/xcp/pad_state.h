/*
 * xcp/pad_state.h - canonical, platform-neutral pad state.
 *
 * This is the "core" layer: it must compile with a bare C99 compiler and must
 * never include a PS4/Orbis header. Everything that is specific to Orbis
 * (real handle numbers, timestamps from sceKernelGetProcessTime, the real
 * OrbisPadData struct) lives in the platform layer.
 *
 * The button bit values below are copies of the real Orbis enum, verified in
 * vendor/OpenOrbis-PS4-Toolchain/include/orbis/_types/pad.h. The PS4 layer
 * static_asserts that they still match, so a toolchain change is a build error
 * instead of a silent mis-mapping.
 */
#ifndef XCP_PAD_STATE_H
#define XCP_PAD_STATE_H

#include <stdint.h>

/* --- Digital buttons (values == ORBIS_PAD_BUTTON_*) ---------------------- */
#define XCP_BTN_L3        0x00000002u
#define XCP_BTN_R3        0x00000004u
#define XCP_BTN_OPTIONS   0x00000008u
#define XCP_BTN_UP        0x00000010u
#define XCP_BTN_RIGHT     0x00000020u
#define XCP_BTN_DOWN      0x00000040u
#define XCP_BTN_LEFT      0x00000080u
#define XCP_BTN_L2        0x00000100u
#define XCP_BTN_R2        0x00000200u
#define XCP_BTN_L1        0x00000400u
#define XCP_BTN_R1        0x00000800u
#define XCP_BTN_TRIANGLE  0x00001000u
#define XCP_BTN_CIRCLE    0x00002000u
#define XCP_BTN_CROSS     0x00004000u
#define XCP_BTN_SQUARE    0x00008000u
#define XCP_BTN_TOUCHPAD  0x00100000u

/*
 * NOT part of the Orbis enum. The reference plugin (crucifix86, MIT) invents
 * these two bits to carry Share and the PS/Guide button. Kept because games
 * genuinely read them, but marked unverified - see OPEN-2 in PROGRESS.md.
 */
#define XCP_BTN_SHARE     0x00000001u /* upstream guess, 0x0001 */
#define XCP_BTN_PS        0x00010000u /* upstream guess, 0x010000 */

/* --- Axis conventions ---------------------------------------------------- */
#define XCP_STICK_CENTER  128u
#define XCP_TRIGGER_MAX   255u

/*
 * Decoded state of one virtual pad. This is the only thing the pad layer and
 * the platform layer exchange.
 */
typedef struct xcp_pad_state {
    uint32_t buttons;       /* XCP_BTN_* bit mask */
    uint8_t  left_x;        /* 0..255, centre 128 */
    uint8_t  left_y;
    uint8_t  right_x;
    uint8_t  right_y;
    uint8_t  left_trigger;  /* 0..255 */
    uint8_t  right_trigger;
    uint8_t  connected;     /* 1 if the source device is present */
} xcp_pad_state;

/* Sticks centred, triggers released. connected is left to the caller. */
void xcp_pad_state_centre_sticks(xcp_pad_state *s);

/*
 * Merge `src` into `dst` so that both physical controllers can drive the same
 * player slot:
 *   - buttons  : bitwise OR (either pad can press)
 *   - triggers : max()
 *   - sticks   : the axis furthest from centre wins, compared per axis pair,
 *                so a resting DS4 axis (which jitters around 128) can never
 *                drag an actively deflected Xbox axis back to centre.
 * If `dst` is not connected, `src` wins outright.
 */
void xcp_pad_state_merge(xcp_pad_state *dst, const xcp_pad_state *src);

/* Number of bits set in `buttons` - handy for logging/tests. */
unsigned xcp_pad_state_button_count(uint32_t buttons);

/* Render a button mask as space separated names, e.g. "CROSS L1 UP". */
const char *xcp_button_name(uint32_t single_bit);
void xcp_pad_state_format_buttons(uint32_t buttons, char *out, unsigned out_size);

/* 1 if `a` and `b` are byte-identical (used by tests). */
int xcp_pad_state_equal(const xcp_pad_state *a, const xcp_pad_state *b);

#endif /* XCP_PAD_STATE_H */
