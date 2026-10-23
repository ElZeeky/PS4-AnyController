/*
 * xcp/config.h - decoder tuning knobs.
 *
 * One struct, plain data, no floating point (the PS4 build links a minimal
 * libc and floats in a hook path are needless risk).
 */
#ifndef XCP_CONFIG_H
#define XCP_CONFIG_H

#include <stdint.h>

#define XCP_DEFAULT_STICK_DEADZONE      15u  /* out of 127, ~12% */
#define XCP_DEFAULT_TRIGGER_THRESHOLD   30u  /* out of 255 */

typedef struct xcp_config {
    uint8_t stick_deadzone;      /* 0 disables deadzone handling */
    uint8_t trigger_threshold;   /* L2/R2 report pressed at or above this */
    uint8_t invert_left_y;       /* PS4 Y grows downwards, Xbox Y grows up */
    uint8_t invert_right_y;
    uint8_t swap_ab;             /* Japanese/Nintendo style A<->B */
    uint8_t swap_xy;
    /*
     * Manba One quirk: after PC -> Switch -> PC the pad keeps reporting an
     * Xbox 360 packet whose face bits are rotated (A->B, B->Y, Y->A). A power
     * cycle fixes it. The payload can only detect the transition, so it enables
     * this full face permutation for that one connection. Never set this for a
     * clean PC session; it is not a global layout preference.
     */
    uint8_t manba_post_switch_layout;
} xcp_config;

/* Deadzone + Y inversion, matching the reference plugin. */
void xcp_config_default(xcp_config *cfg);

/* Same, but without Y inversion (the Switch PDP pad matches PS4 axes). */
void xcp_config_default_switch(xcp_config *cfg);

/* Switch Pro reports Y with the opposite sign to PS4; invert it. */
void xcp_config_default_switch_pro(xcp_config *cfg);

/* Same, but without Y inversion (DualSense axes already match PS4). */
void xcp_config_default_dualsense(xcp_config *cfg);

#endif /* XCP_CONFIG_H */
