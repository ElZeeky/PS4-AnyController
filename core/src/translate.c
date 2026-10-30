/*
 * core/src/translate.c - Xbox/Switch HID report -> platform-neutral pad state.
 *
 * Derived from crucifix86/xbox_controller_plugin (MIT). The button/axis mapping
 * is intentionally identical to that plugin so behavioural differences we see
 * on console are not caused by the port. Two deliberate differences:
 *
 *   1. upstream writes ds4->quat / ds4->vel / ds4->acell, which do not exist in
 *      the OpenOrbis OrbisPadData struct (see PROGRESS.md OPEN-1). We emit a
 *      neutral orientation/acceleration on the PS4 side instead.
 *   2. upstream emits a monotonically increasing dummy timestamp from a static
 *      counter. Timestamps are a platform concern now, so the platform layer
 *      stamps with sceKernelGetProcessTime.
 *
 * Pure C: only <string.h> and <stdint.h>.
 */
#include "xcp/translate.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Config
 * ------------------------------------------------------------------------ */

void xcp_config_default(xcp_config *cfg)
{
    if (!cfg) return;
    cfg->stick_deadzone    = XCP_DEFAULT_STICK_DEADZONE;
    cfg->trigger_threshold = XCP_DEFAULT_TRIGGER_THRESHOLD;
    cfg->invert_left_y     = 1;  /* reference plugin: PS4 Y is inverted vs Xbox */
    cfg->invert_right_y    = 1;
    cfg->swap_ab           = 0;
    cfg->swap_xy           = 0;
    cfg->manba_post_switch_layout = 0;
}

void xcp_config_default_switch(xcp_config *cfg)
{
    if (!cfg) return;
    xcp_config_default(cfg);
    cfg->invert_left_y  = 0; /* Switch PDP sticks match PS4 axis direction */
    cfg->invert_right_y = 0;
}

void xcp_config_default_switch_pro(xcp_config *cfg)
{
    if (!cfg) return;
    xcp_config_default_switch(cfg);
    cfg->invert_left_y  = 1; /* 0x30 reports Y opposite to PS4, confirmed */
    cfg->invert_right_y = 1;
}

void xcp_config_default_dualsense(xcp_config *cfg)
{
    if (!cfg) return;
    xcp_config_default(cfg);
    cfg->invert_left_y  = 0; /* DualSense sticks already match PS4 axes */
    cfg->invert_right_y = 0;
}

void xcp_config_for_source(xcp_controller_type type,
                           xcp_controller_type previous,
                           xcp_config *cfg)
{
    if (!cfg) return;

    switch (type) {
    case XCP_CTRL_SWITCH:     xcp_config_default_switch(cfg);     break;
    case XCP_CTRL_SWITCH_PRO: xcp_config_default_switch_pro(cfg); break;
    case XCP_CTRL_DUALSENSE:  xcp_config_default_dualsense(cfg);  break;
    default:                  xcp_config_default(cfg);            break;
    }

    /*
     * The Manba One V2 in wired PC mode reports a rotated face layout if it was
     * in Switch mode just before. Power-cycling the pad restores the normal
     * layout, so this is pad state, not a different USB profile. We cannot ask
     * the pad which state it is in; the only safe signal is the transition.
     * A plain PC reconnect (previous == XBOX360) must stay on the normal map.
     */
    if (type == XCP_CTRL_XBOX360 &&
        (previous == XCP_CTRL_SWITCH || previous == XCP_CTRL_SWITCH_PRO)) {
        cfg->manba_post_switch_layout = 1;
    }
}

/* ---------------------------------------------------------------------------
 * Axis helpers
 * ------------------------------------------------------------------------ */

uint8_t xcp_stick_from_signed(int16_t value)
{
    /*
     * 0 .. 65535, then take the top byte: 0 .. 255 with 128 as the centre.
     *
     * Note the deliberate asymmetry inherited from the reference plugin:
     * 0 -> 128, -1 -> 127, +1 -> 128, -32768 -> 0, +32767 -> 255. Resting axes
     * sit at 127 or 128 depending on which way they are nudged; the deadzone
     * snaps both back to 128. Do not "fix" this: it is the mapping we are
     * reproducing, and the tests pin it.
     */
    int32_t shifted = (int32_t)value + 32768;
    return (uint8_t)(shifted >> 8);
}

uint8_t xcp_trigger_from_10bit(uint16_t value)
{
    return (uint8_t)(value >> 2); /* 1023 -> 255 */
}

uint8_t xcp_apply_deadzone(uint8_t value, uint8_t deadzone)
{
    int16_t centred;
    int16_t magnitude;
    int16_t range;
    int16_t scaled;

    if (deadzone == 0) return value;

    centred   = (int16_t)value - (int16_t)XCP_STICK_CENTER;
    magnitude = (int16_t)(centred < 0 ? -centred : centred);

    if (magnitude <= (int16_t)deadzone) return (uint8_t)XCP_STICK_CENTER;

    /*
     * Rescale the remaining travel onto the full half-range, exactly like the
     * reference plugin. Do NOT clamp the result at 127: the extreme low end
     * (raw 0) produces 128 and 128-128 == 0 is the correct full-deflection
     * value. 113*127/112 == 128 is the largest value this can produce, so it
     * cannot overflow.
     */
    range = (int16_t)(127 - deadzone);
    /*
     * A deadzone of 127+ leaves no half-range to rescale onto (magnitude can
     * still reach 128 at the extreme). Guard the division rather than crash: a
     * pad that can barely leave centre simply stays centred.
     */
    if (range <= 0) return (uint8_t)XCP_STICK_CENTER;
    scaled = (int16_t)(((magnitude - (int16_t)deadzone) * 127) / range);

    return (uint8_t)(centred < 0 ? (128 - scaled) : (128 + scaled));
}

/*
 * Deadzone must be applied to both axes of a stick *pair* so that a diagonal
 * deflection is not shaved into a cardinal one. The reference plugin applies it
 * per axis; we keep that behaviour to stay bit-identical.
 */
static void apply_stick(const xcp_config *cfg, uint8_t *x, uint8_t *y,
                        int invert_y)
{
    if (invert_y) {
        *y = (uint8_t)(255u - *y);
    }
    if (cfg->stick_deadzone > 0) {
        *x = xcp_apply_deadzone(*x, cfg->stick_deadzone);
        *y = xcp_apply_deadzone(*y, cfg->stick_deadzone);
    }
}

uint32_t xcp_dpad_mask_from_bits(uint8_t bits)
{
    uint32_t mask = 0;
    if (bits & 0x01u) mask |= XCP_BTN_UP;
    if (bits & 0x02u) mask |= XCP_BTN_DOWN;
    if (bits & 0x04u) mask |= XCP_BTN_LEFT;
    if (bits & 0x08u) mask |= XCP_BTN_RIGHT;
    return mask;
}

uint32_t xcp_switch_hat_mask(uint8_t hat)
{
    switch (hat) {
    case XCP_SWITCH_HAT_UP:         return XCP_BTN_UP;
    case XCP_SWITCH_HAT_UP_RIGHT:   return XCP_BTN_UP    | XCP_BTN_RIGHT;
    case XCP_SWITCH_HAT_RIGHT:      return XCP_BTN_RIGHT;
    case XCP_SWITCH_HAT_DOWN_RIGHT: return XCP_BTN_DOWN  | XCP_BTN_RIGHT;
    case XCP_SWITCH_HAT_DOWN:       return XCP_BTN_DOWN;
    case XCP_SWITCH_HAT_DOWN_LEFT:  return XCP_BTN_DOWN  | XCP_BTN_LEFT;
    case XCP_SWITCH_HAT_LEFT:       return XCP_BTN_LEFT;
    case XCP_SWITCH_HAT_UP_LEFT:    return XCP_BTN_UP    | XCP_BTN_LEFT;
    default:                        return 0; /* centred or out of range */
    }
}

/* ---------------------------------------------------------------------------
 * Validation
 * ------------------------------------------------------------------------ */

int xcp_report_looks_valid(xcp_controller_type type, const uint8_t *buf, size_t len)
{
    if (!buf) return 0;

    switch (type) {
    case XCP_CTRL_XBOX360:
        return len >= XCP_XBOX360_REPORT_SIZE &&
               buf[0] == 0x00 && buf[1] == XCP_XBOX360_REPORT_SIZE;
    case XCP_CTRL_XBOXONE:
    case XCP_CTRL_XBOX_SERIES:
        return len >= XCP_XBOXONE_REPORT_SIZE &&
               buf[0] == XCP_XBOXONE_REPORT_INPUT;
    case XCP_CTRL_SWITCH:
        return len >= XCP_SWITCH_REPORT_SIZE;
    case XCP_CTRL_SWITCH_PRO:
        return len >= XCP_SWITCH_PRO_MIN_SIZE &&
               buf[0] == XCP_SWITCH_PRO_REPORT_ID;
    case XCP_CTRL_DUALSENSE:
        return len >= XCP_DUALSENSE_MIN_SIZE &&
               buf[0] == XCP_DUALSENSE_REPORT_ID;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------------------
 * Xbox 360
 * ------------------------------------------------------------------------ */

static void decode_xbox360(const xcp_xbox360_report *r, const xcp_config *cfg,
                           xcp_pad_state *out)
{
    uint16_t buttons = (uint16_t)(r->buttons_low | ((uint16_t)r->buttons_high << 8));
    uint32_t mask    = 0;

    out->left_x  = xcp_stick_from_signed(r->left_stick_x);
    out->left_y  = xcp_stick_from_signed(r->left_stick_y);
    out->right_x = xcp_stick_from_signed(r->right_stick_x);
    out->right_y = xcp_stick_from_signed(r->right_stick_y);

    apply_stick(cfg, &out->left_x,  &out->left_y,  cfg->invert_left_y);
    apply_stick(cfg, &out->right_x, &out->right_y, cfg->invert_right_y);

    out->left_trigger  = r->left_trigger;
    out->right_trigger = r->right_trigger;

    /* Face buttons (optional A/B and X/Y swap). */
    if (cfg->manba_post_switch_layout) {
        /*
         * Pad still in its post-Switch state: physical A/B/Y arrive on the
         * B/Y/A bits. Map the raw bits back to a standard PS4 diamond.
         */
        if (buttons & (XCP_360_A << 8)) mask |= XCP_BTN_TRIANGLE;
        if (buttons & (XCP_360_B << 8)) mask |= XCP_BTN_CROSS;
        if (buttons & (XCP_360_X << 8)) mask |= XCP_BTN_SQUARE;
        if (buttons & (XCP_360_Y << 8)) mask |= XCP_BTN_CIRCLE;
    } else {
        if (cfg->swap_ab) {
            if (buttons & (XCP_360_A << 8)) mask |= XCP_BTN_CIRCLE;
            if (buttons & (XCP_360_B << 8)) mask |= XCP_BTN_CROSS;
        } else {
            if (buttons & (XCP_360_A << 8)) mask |= XCP_BTN_CROSS;
            if (buttons & (XCP_360_B << 8)) mask |= XCP_BTN_CIRCLE;
        }
        if (cfg->swap_xy) {
            if (buttons & (XCP_360_X << 8)) mask |= XCP_BTN_TRIANGLE;
            if (buttons & (XCP_360_Y << 8)) mask |= XCP_BTN_SQUARE;
        } else {
            if (buttons & (XCP_360_X << 8)) mask |= XCP_BTN_SQUARE;
            if (buttons & (XCP_360_Y << 8)) mask |= XCP_BTN_TRIANGLE;
        }
    }

    /* Shoulders. */
    if (buttons & (XCP_360_LB << 8)) mask |= XCP_BTN_L1;
    if (buttons & (XCP_360_RB << 8)) mask |= XCP_BTN_R1;

    /* Triggers are analog; also raise the digital L2/R2 bit past threshold. */
    if (r->left_trigger  >= cfg->trigger_threshold) mask |= XCP_BTN_L2;
    if (r->right_trigger >= cfg->trigger_threshold) mask |= XCP_BTN_R2;

    /* Stick clicks. */
    if (buttons & XCP_360_L3) mask |= XCP_BTN_L3;
    if (buttons & XCP_360_R3) mask |= XCP_BTN_R3;

    /*
     * Touchpad click. The Xbox pad has no touchpad and - verified on hardware -
     * its extra back buttons are firmware macros that only ever emit the bits
     * above, never a bit of their own. So the touchpad click shares a chord
     * with L3+R3: the Manba's M1..M4 can be programmed to an "L3+R3" macro, and
     * pressing both sticks does the same. L3 and R3 themselves still pass
     * through, so nothing loses its normal function.
     */
    if ((buttons & XCP_360_L3) && (buttons & XCP_360_R3)) {
        mask |= XCP_BTN_TOUCHPAD;
    }

    /* Menu: Start -> Options, Back -> Share. */
    if (buttons & XCP_360_START) mask |= XCP_BTN_OPTIONS;
    if (buttons & XCP_360_BACK)  mask |= XCP_BTN_SHARE;

    /* Guide -> PS. */
    if (buttons & (XCP_360_GUIDE << 8)) mask |= XCP_BTN_PS;

    /* D-pad. */
    mask |= xcp_dpad_mask_from_bits(r->buttons_low & 0x0Fu);

    out->buttons   = mask;
    out->connected = 1;
}

/* ---------------------------------------------------------------------------
 * Xbox One / Series (GIP)
 * ------------------------------------------------------------------------ */

static void decode_xboxone(const xcp_xboxone_report *r, const xcp_config *cfg,
                           int share, xcp_pad_state *out)
{
    uint32_t mask = 0;
    uint8_t  lt   = xcp_trigger_from_10bit(r->left_trigger);
    uint8_t  rt   = xcp_trigger_from_10bit(r->right_trigger);

    out->left_x  = xcp_stick_from_signed(r->left_stick_x);
    out->left_y  = xcp_stick_from_signed(r->left_stick_y);
    out->right_x = xcp_stick_from_signed(r->right_stick_x);
    out->right_y = xcp_stick_from_signed(r->right_stick_y);

    apply_stick(cfg, &out->left_x,  &out->left_y,  cfg->invert_left_y);
    apply_stick(cfg, &out->right_x, &out->right_y, cfg->invert_right_y);

    out->left_trigger  = lt;
    out->right_trigger = rt;

    /* Face buttons live in the low byte of the 16-bit mask. */
    if (cfg->swap_ab) {
        if (r->buttons_low & XCP_ONE_A) mask |= XCP_BTN_CIRCLE;
        if (r->buttons_low & XCP_ONE_B) mask |= XCP_BTN_CROSS;
    } else {
        if (r->buttons_low & XCP_ONE_A) mask |= XCP_BTN_CROSS;
        if (r->buttons_low & XCP_ONE_B) mask |= XCP_BTN_CIRCLE;
    }
    if (cfg->swap_xy) {
        if (r->buttons_low & XCP_ONE_X) mask |= XCP_BTN_TRIANGLE;
        if (r->buttons_low & XCP_ONE_Y) mask |= XCP_BTN_SQUARE;
    } else {
        if (r->buttons_low & XCP_ONE_X) mask |= XCP_BTN_SQUARE;
        if (r->buttons_low & XCP_ONE_Y) mask |= XCP_BTN_TRIANGLE;
    }

    /* Shoulders, stick clicks and d-pad live in the high byte. */
    if (r->buttons_high & XCP_ONE_LB) mask |= XCP_BTN_L1;
    if (r->buttons_high & XCP_ONE_RB) mask |= XCP_BTN_R1;
    if (r->buttons_high & XCP_ONE_L3) mask |= XCP_BTN_L3;
    if (r->buttons_high & XCP_ONE_R3) mask |= XCP_BTN_R3;
    mask |= xcp_dpad_mask_from_bits(r->buttons_high & 0x0Fu);

    /* Touchpad click shares the L3+R3 chord (see decode_xbox360). */
    if ((r->buttons_high & XCP_ONE_L3) && (r->buttons_high & XCP_ONE_R3)) {
        mask |= XCP_BTN_TOUCHPAD;
    }

    /* Menu: Menu -> Options, View -> Share.
     * On a Series pad the dedicated Share button also maps to Share; the DS4
     * has no separate capture button, so both system buttons share one bit. */
    if (r->buttons_low & XCP_ONE_MENU) mask |= XCP_BTN_OPTIONS;
    if (r->buttons_low & XCP_ONE_VIEW) mask |= XCP_BTN_SHARE;
    if (share)                         mask |= XCP_BTN_SHARE;

    /* Analog triggers also set the digital bits. */
    if (lt >= cfg->trigger_threshold) mask |= XCP_BTN_L2;
    if (rt >= cfg->trigger_threshold) mask |= XCP_BTN_R2;

    /*
     * The Guide/Xbox button is NOT in the 0x20 input report; it arrives as a
     * separate 0x07 message. The reference plugin ignores it, so XCP_BTN_PS is
     * never produced on this path. Kept as a known gap, not an oversight.
     */

    out->buttons   = mask;
    out->connected = 1;
}

/* ---------------------------------------------------------------------------
 * Switch input-only pads
 * ------------------------------------------------------------------------ */

static void decode_switch(const xcp_switch_report *r, const xcp_config *cfg,
                          xcp_pad_state *out)
{
    uint32_t mask = 0;

    out->left_x  = r->left_stick_x;
    out->left_y  = r->left_stick_y;
    out->right_x = r->right_stick_x;
    out->right_y = r->right_stick_y;

    apply_stick(cfg, &out->left_x,  &out->left_y,  cfg->invert_left_y);
    apply_stick(cfg, &out->right_x, &out->right_y, cfg->invert_right_y);

    /* ZL/ZR are digital on these pads, so fake the analog value. */
    out->left_trigger  = (r->buttons0 & XCP_SWITCH_ZL) ? 255u : 0u;
    out->right_trigger = (r->buttons0 & XCP_SWITCH_ZR) ? 255u : 0u;

    /* Nintendo layout is positional: A sits East, B sits South. */
    if (cfg->swap_ab) {
        if (r->buttons0 & XCP_SWITCH_A) mask |= XCP_BTN_CROSS;
        if (r->buttons0 & XCP_SWITCH_B) mask |= XCP_BTN_CIRCLE;
    } else {
        if (r->buttons0 & XCP_SWITCH_A) mask |= XCP_BTN_CIRCLE;
        if (r->buttons0 & XCP_SWITCH_B) mask |= XCP_BTN_CROSS;
    }
    if (cfg->swap_xy) {
        if (r->buttons0 & XCP_SWITCH_X) mask |= XCP_BTN_SQUARE;
        if (r->buttons0 & XCP_SWITCH_Y) mask |= XCP_BTN_TRIANGLE;
    } else {
        if (r->buttons0 & XCP_SWITCH_X) mask |= XCP_BTN_TRIANGLE;
        if (r->buttons0 & XCP_SWITCH_Y) mask |= XCP_BTN_SQUARE;
    }

    if (r->buttons0 & XCP_SWITCH_L)  mask |= XCP_BTN_L1;
    if (r->buttons0 & XCP_SWITCH_R)  mask |= XCP_BTN_R1;
    if (r->buttons0 & XCP_SWITCH_ZL) mask |= XCP_BTN_L2;
    if (r->buttons0 & XCP_SWITCH_ZR) mask |= XCP_BTN_R2;

    if (r->buttons1 & XCP_SWITCH_L3) mask |= XCP_BTN_L3;
    if (r->buttons1 & XCP_SWITCH_R3) mask |= XCP_BTN_R3;
    if (r->buttons1 & XCP_SWITCH_PLUS)  mask |= XCP_BTN_OPTIONS;
    if (r->buttons1 & XCP_SWITCH_MINUS) mask |= XCP_BTN_SHARE;
    if (r->buttons1 & XCP_SWITCH_HOME)  mask |= XCP_BTN_PS;

    mask |= xcp_switch_hat_mask(r->hat);

    out->buttons   = mask;
    out->connected = 1;
}

static void decode_switch_pro(const xcp_switch_pro_report *r,
                              const xcp_config *cfg, xcp_pad_state *out)
{
    uint32_t mask = 0;
    uint16_t lx, ly, rx, ry;

    lx = (uint16_t)(r->left_x_lo |
                    ((uint16_t)(r->left_xy_hi & 0x0Fu) << 8));
    ly = (uint16_t)((r->left_xy_hi >> 4) |
                    ((uint16_t)r->left_y_hi << 4));
    rx = (uint16_t)(r->right_x_lo |
                    ((uint16_t)(r->right_xy_hi & 0x0Fu) << 8));
    ry = (uint16_t)((r->right_xy_hi >> 4) |
                    ((uint16_t)r->right_y_hi << 4));

    out->left_x  = (uint8_t)(lx >> 4);
    out->left_y  = (uint8_t)(ly >> 4);
    out->right_x = (uint8_t)(rx >> 4);
    out->right_y = (uint8_t)(ry >> 4);

    apply_stick(cfg, &out->left_x,  &out->left_y,  cfg->invert_left_y);
    apply_stick(cfg, &out->right_x, &out->right_y, cfg->invert_right_y);

    out->left_trigger  = (r->buttons_left & XCP_SWPRO_ZL) ? 255u : 0u;
    out->right_trigger = (r->buttons_right & XCP_SWPRO_ZR) ? 255u : 0u;

    if (cfg->swap_ab) {
        if (r->buttons_right & XCP_SWPRO_A) mask |= XCP_BTN_CROSS;
        if (r->buttons_right & XCP_SWPRO_B) mask |= XCP_BTN_CIRCLE;
    } else {
        if (r->buttons_right & XCP_SWPRO_A) mask |= XCP_BTN_CIRCLE;
        if (r->buttons_right & XCP_SWPRO_B) mask |= XCP_BTN_CROSS;
    }
    if (cfg->swap_xy) {
        if (r->buttons_right & XCP_SWPRO_X) mask |= XCP_BTN_SQUARE;
        if (r->buttons_right & XCP_SWPRO_Y) mask |= XCP_BTN_TRIANGLE;
    } else {
        if (r->buttons_right & XCP_SWPRO_X) mask |= XCP_BTN_TRIANGLE;
        if (r->buttons_right & XCP_SWPRO_Y) mask |= XCP_BTN_SQUARE;
    }

    if (r->buttons_left & XCP_SWPRO_L)  mask |= XCP_BTN_L1;
    if (r->buttons_right & XCP_SWPRO_R) mask |= XCP_BTN_R1;
    if (r->buttons_left & XCP_SWPRO_ZL) mask |= XCP_BTN_L2;
    if (r->buttons_right & XCP_SWPRO_ZR) mask |= XCP_BTN_R2;

    if (r->buttons_shared & XCP_SWPRO_L3)      mask |= XCP_BTN_L3;
    if (r->buttons_shared & XCP_SWPRO_R3)      mask |= XCP_BTN_R3;
    if (r->buttons_shared & XCP_SWPRO_PLUS)    mask |= XCP_BTN_OPTIONS;
    if (r->buttons_shared & XCP_SWPRO_MINUS)   mask |= XCP_BTN_SHARE;
    if (r->buttons_shared & XCP_SWPRO_CAPTURE) mask |= XCP_BTN_SHARE;
    if (r->buttons_shared & XCP_SWPRO_HOME)    mask |= XCP_BTN_PS;

    if (r->buttons_left & XCP_SWPRO_DOWN)  mask |= XCP_BTN_DOWN;
    if (r->buttons_left & XCP_SWPRO_UP)    mask |= XCP_BTN_UP;
    if (r->buttons_left & XCP_SWPRO_RIGHT) mask |= XCP_BTN_RIGHT;
    if (r->buttons_left & XCP_SWPRO_LEFT)  mask |= XCP_BTN_LEFT;

    out->buttons   = mask;
    out->connected = 1;
}

/* ---------------------------------------------------------------------------
 * Sony DualSense / DualSense Edge
 * ------------------------------------------------------------------------ */

static void decode_dualsense(const xcp_dualsense_report *r,
                             const xcp_config *cfg, xcp_pad_state *out)
{
    uint32_t mask = 0;
    uint8_t  lt   = r->left_trigger;
    uint8_t  rt   = r->right_trigger;

    out->left_x  = r->left_stick_x;
    out->left_y  = r->left_stick_y;
    out->right_x = r->right_stick_x;
    out->right_y = r->right_stick_y;

    apply_stick(cfg, &out->left_x,  &out->left_y,  cfg->invert_left_y);
    apply_stick(cfg, &out->right_x, &out->right_y, cfg->invert_right_y);

    /*
     * The report carries both an analog trigger value and a digital L2/R2 bit.
     * Some DualSense firmware sets the bit before the analog value leaves zero,
     * so mirror Linux's hid-playstation: force full travel when the bit is set
     * and the analog value is still zero, otherwise games that only read the
     * analog value would miss the press.
     */
    if ((r->buttons1 & XCP_DS_L2) && lt == 0) lt = 255;
    if ((r->buttons1 & XCP_DS_R2) && rt == 0) rt = 255;

    out->left_trigger  = lt;
    out->right_trigger = rt;

    /* Face buttons are already in PlayStation positions. */
    if (r->buttons0 & XCP_DS_SQUARE)   mask |= XCP_BTN_SQUARE;
    if (r->buttons0 & XCP_DS_CROSS)    mask |= XCP_BTN_CROSS;
    if (r->buttons0 & XCP_DS_CIRCLE)   mask |= XCP_BTN_CIRCLE;
    if (r->buttons0 & XCP_DS_TRIANGLE) mask |= XCP_BTN_TRIANGLE;

    if (r->buttons1 & XCP_DS_L1) mask |= XCP_BTN_L1;
    if (r->buttons1 & XCP_DS_R1) mask |= XCP_BTN_R1;
    if (r->buttons1 & XCP_DS_L2) mask |= XCP_BTN_L2;
    if (r->buttons1 & XCP_DS_R2) mask |= XCP_BTN_R2;
    if (lt >= cfg->trigger_threshold) mask |= XCP_BTN_L2;
    if (rt >= cfg->trigger_threshold) mask |= XCP_BTN_R2;
    if (r->buttons1 & XCP_DS_L3) mask |= XCP_BTN_L3;
    if (r->buttons1 & XCP_DS_R3) mask |= XCP_BTN_R3;

    if (r->buttons1 & XCP_DS_CREATE)  mask |= XCP_BTN_SHARE;
    if (r->buttons1 & XCP_DS_OPTIONS) mask |= XCP_BTN_OPTIONS;

    if (r->buttons2 & XCP_DS_PS)       mask |= XCP_BTN_PS;
    if (r->buttons2 & XCP_DS_TOUCHPAD) mask |= XCP_BTN_TOUCHPAD;

    /*
     * Mute, and the Edge's Fn/paddle bits, have no DS4 equivalent. The Edge
     * paddles can be remapped to normal buttons in the controller's own
     * profile, so leaving them unmapped loses nothing.
     */

    mask |= xcp_switch_hat_mask(r->buttons0 & 0x0Fu);

    out->buttons   = mask;
    out->connected = 1;
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------ */

int xcp_decode(xcp_controller_type type,
               const uint8_t *buf, size_t len,
               const xcp_config *cfg,
               xcp_pad_state *out)
{
    xcp_config local;

    if (!buf || !out || type == XCP_CTRL_NONE) return 0;
    if (!xcp_report_looks_valid(type, buf, len)) return 0;

    if (!cfg) {
        xcp_config_for_source(type, XCP_CTRL_NONE, &local);
        cfg = &local;
    }

    memset(out, 0, sizeof(*out));

    switch (type) {
    case XCP_CTRL_XBOX360: {
        xcp_xbox360_report r;
        memcpy(&r, buf, sizeof(r));
        decode_xbox360(&r, cfg, out);
        return 1;
    }
    case XCP_CTRL_XBOXONE: {
        xcp_xboxone_report r;
        memcpy(&r, buf, sizeof(r));
        decode_xboxone(&r, cfg, 0, out);
        return 1;
    }
    case XCP_CTRL_XBOX_SERIES: {
        xcp_xboxone_report r;
        int share = 0;
        memcpy(&r, buf, sizeof(r));
        if (len >= XCP_XBOX_SERIES_MIN_SIZE) {
            share = (buf[len - XCP_XBOX_SERIES_SHARE_FROM_END] &
                     XCP_XBOX_SERIES_SHARE_BIT) != 0;
        }
        decode_xboxone(&r, cfg, share, out);
        return 1;
    }
    case XCP_CTRL_SWITCH: {
        xcp_switch_report r;
        memcpy(&r, buf, sizeof(r));
        decode_switch(&r, cfg, out);
        return 1;
    }
    case XCP_CTRL_SWITCH_PRO: {
        xcp_switch_pro_report r;
        memcpy(&r, buf, sizeof(r));
        decode_switch_pro(&r, cfg, out);
        return 1;
    }
    case XCP_CTRL_DUALSENSE: {
        xcp_dualsense_report r;
        memcpy(&r, buf, sizeof(r));
        decode_dualsense(&r, cfg, out);
        return 1;
    }
    default:
        return 0;
    }
}

int xcp_gip_guide_state(const uint8_t *buf, size_t len)
{
    if (!buf || len < 5 || buf[0] != XCP_XBOXONE_REPORT_GUIDE) return -1;
    return (buf[4] & 0x01u) ? 1 : 0;
}
