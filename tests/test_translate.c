/*
 * tests/test_translate.c - the core decoder.
 *
 * Every expectation here is hand computed from the mapping, so a change in the
 * mapping has to be a deliberate change of these numbers.
 */
#include "framework.h"

#include "xcp/translate.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Report builders
 * ------------------------------------------------------------------------ */

static size_t make_360(uint8_t *buf, uint8_t low, uint8_t high,
                       uint8_t lt, uint8_t rt,
                       int16_t lx, int16_t ly, int16_t rx, int16_t ry)
{
    memset(buf, 0, XCP_XBOX360_REPORT_SIZE);
    buf[0] = 0x00;
    buf[1] = 0x14;
    buf[2] = low;
    buf[3] = high;
    buf[4] = lt;
    buf[5] = rt;
    buf[6]  = (uint8_t)(lx & 0xFF);
    buf[7]  = (uint8_t)((lx >> 8) & 0xFF);
    buf[8]  = (uint8_t)(ly & 0xFF);
    buf[9]  = (uint8_t)((ly >> 8) & 0xFF);
    buf[10] = (uint8_t)(rx & 0xFF);
    buf[11] = (uint8_t)((rx >> 8) & 0xFF);
    buf[12] = (uint8_t)(ry & 0xFF);
    buf[13] = (uint8_t)((ry >> 8) & 0xFF);
    return XCP_XBOX360_REPORT_SIZE;
}

static size_t make_one(uint8_t *buf, uint8_t low, uint8_t high,
                       uint16_t lt, uint16_t rt,
                       int16_t lx, int16_t ly, int16_t rx, int16_t ry)
{
    memset(buf, 0, XCP_XBOXONE_REPORT_SIZE);
    buf[0] = 0x20;
    buf[3] = 0x0E;
    buf[4] = low;
    buf[5] = high;
    buf[6]  = (uint8_t)(lt & 0xFF);
    buf[7]  = (uint8_t)((lt >> 8) & 0xFF);
    buf[8]  = (uint8_t)(rt & 0xFF);
    buf[9]  = (uint8_t)((rt >> 8) & 0xFF);
    buf[10] = (uint8_t)(lx & 0xFF);
    buf[11] = (uint8_t)((lx >> 8) & 0xFF);
    buf[12] = (uint8_t)(ly & 0xFF);
    buf[13] = (uint8_t)((ly >> 8) & 0xFF);
    buf[14] = (uint8_t)(rx & 0xFF);
    buf[15] = (uint8_t)((rx >> 8) & 0xFF);
    buf[16] = (uint8_t)(ry & 0xFF);
    buf[17] = (uint8_t)((ry >> 8) & 0xFF);
    return XCP_XBOXONE_REPORT_SIZE;
}

static size_t make_switch(uint8_t *buf, uint8_t b0, uint8_t b1, uint8_t hat,
                          uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry)
{
    buf[0] = b0;
    buf[1] = b1;
    buf[2] = hat;
    buf[3] = lx;
    buf[4] = ly;
    buf[5] = rx;
    buf[6] = ry;
    return XCP_SWITCH_REPORT_SIZE;
}

static size_t make_switch_pro(uint8_t *buf, uint8_t right, uint8_t shared,
                              uint8_t left, uint16_t lx, uint16_t ly,
                              uint16_t rx, uint16_t ry)
{
    memset(buf, 0, 64);
    buf[0]  = XCP_SWITCH_PRO_REPORT_ID;
    buf[3]  = right;
    buf[4]  = shared;
    buf[5]  = left;
    buf[6]  = (uint8_t)(lx & 0xFF);
    buf[7]  = (uint8_t)(((lx >> 8) & 0x0F) | ((ly & 0x0F) << 4));
    buf[8]  = (uint8_t)(ly >> 4);
    buf[9]  = (uint8_t)(rx & 0xFF);
    buf[10] = (uint8_t)(((rx >> 8) & 0x0F) | ((ry & 0x0F) << 4));
    buf[11] = (uint8_t)(ry >> 4);
    return 64;
}

static size_t make_dualsense(uint8_t *buf,
                             uint8_t b0, uint8_t b1, uint8_t b2,
                             uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry,
                             uint8_t lt, uint8_t rt)
{
    memset(buf, 0, XCP_DUALSENSE_REPORT_SIZE);
    buf[0]  = XCP_DUALSENSE_REPORT_ID;
    buf[1]  = lx;
    buf[2]  = ly;
    buf[3]  = rx;
    buf[4]  = ry;
    buf[5]  = lt;
    buf[6]  = rt;
    buf[8]  = b0;
    buf[9]  = b1;
    buf[10] = b2;
    return XCP_DUALSENSE_REPORT_SIZE;
}

/*
 * Series reports carry the Share bit near the end of the whole USB transfer,
 * not in the 18-byte input prefix. `total_len` is 44 (payload 40, firmware
 * 5.1) or 48 (payload 44, firmware 5.5); both include the 4-byte GIP header.
 */
static size_t make_series(uint8_t *buf, size_t total_len,
                          uint8_t low, uint8_t high,
                          uint16_t lt, uint16_t rt,
                          int16_t lx, int16_t ly, int16_t rx, int16_t ry,
                          int share)
{
    memset(buf, 0, total_len);
    buf[0] = 0x20;
    buf[3] = 0x0E;
    buf[4] = low;
    buf[5] = high;
    buf[6]  = (uint8_t)(lt & 0xFF);
    buf[7]  = (uint8_t)((lt >> 8) & 0xFF);
    buf[8]  = (uint8_t)(rt & 0xFF);
    buf[9]  = (uint8_t)((rt >> 8) & 0xFF);
    buf[10] = (uint8_t)(lx & 0xFF);
    buf[11] = (uint8_t)((lx >> 8) & 0xFF);
    buf[12] = (uint8_t)(ly & 0xFF);
    buf[13] = (uint8_t)((ly >> 8) & 0xFF);
    buf[14] = (uint8_t)(rx & 0xFF);
    buf[15] = (uint8_t)((rx >> 8) & 0xFF);
    buf[16] = (uint8_t)(ry & 0xFF);
    buf[17] = (uint8_t)((ry >> 8) & 0xFF);
    if (share) buf[total_len - XCP_XBOX_SERIES_SHARE_FROM_END] |=
        XCP_XBOX_SERIES_SHARE_BIT;
    return total_len;
}

/* ---------------------------------------------------------------------------
 * Axis primitives
 * ------------------------------------------------------------------------ */

XCP_TEST(signed_axis_maps_to_ps4_range)
{
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(0), 128);
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(-32768), 0);
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(32767), 255);

    /*
     * The mapping is asymmetric by one unit around the centre, and that is the
     * reference plugin's behaviour, reproduced on purpose. The deadzone folds
     * both 127 and 128 back to 128, so it never reaches a game with the default
     * config.
     */
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(1), 128);
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(-1), 127);
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(256), 129);   /* 33024 >> 8        */
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(512), 130);   /* 33280 >> 8        */
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(-256), 127);  /* 32512 >> 8        */
    XCP_CHECK_EQ_INT(xcp_stick_from_signed(-512), 126);  /* 32256 >> 8        */
}

XCP_TEST(ten_bit_trigger_maps_to_eight_bit)
{
    XCP_CHECK_EQ_INT(xcp_trigger_from_10bit(0), 0);
    XCP_CHECK_EQ_INT(xcp_trigger_from_10bit(1023), 255);
    XCP_CHECK_EQ_INT(xcp_trigger_from_10bit(512), 128);
}

XCP_TEST(deadzone_snaps_inside_and_rescales_outside)
{
    /* Inside the deadzone -> exactly centre. */
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(128, 15), 128);
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(143, 15), 128);
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(113, 15), 128);

    /* First step outside the deadzone moves by one unit. */
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(144, 15), 129);
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(112, 15), 127);

    /* Full deflection is preserved (the rescale keeps the whole range). */
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(255, 15), 255);
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(0, 15), 0);

    /* Deadzone 0 is a no-op. */
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(200, 0), 200);

    /*
     * A deadzone of 127 leaves no half-range to rescale onto: the extreme value
     * must fall back to centre instead of dividing by zero.
     */
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(0, 127), 128);
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(255, 127), 128);
    XCP_CHECK_EQ_INT(xcp_apply_deadzone(128, 200), 128);
}

XCP_TEST(dpad_bits_to_mask)
{
    XCP_CHECK_EQ_HEX(xcp_dpad_mask_from_bits(0x00), 0);
    XCP_CHECK_EQ_HEX(xcp_dpad_mask_from_bits(0x01), XCP_BTN_UP);
    XCP_CHECK_EQ_HEX(xcp_dpad_mask_from_bits(0x02), XCP_BTN_DOWN);
    XCP_CHECK_EQ_HEX(xcp_dpad_mask_from_bits(0x04), XCP_BTN_LEFT);
    XCP_CHECK_EQ_HEX(xcp_dpad_mask_from_bits(0x08), XCP_BTN_RIGHT);
    XCP_CHECK_EQ_HEX(xcp_dpad_mask_from_bits(0x09), XCP_BTN_UP | XCP_BTN_RIGHT);
}

XCP_TEST(switch_hat_masks)
{
    XCP_CHECK_EQ_HEX(xcp_switch_hat_mask(XCP_SWITCH_HAT_UP), XCP_BTN_UP);
    XCP_CHECK_EQ_HEX(xcp_switch_hat_mask(XCP_SWITCH_HAT_UP_RIGHT),
                     XCP_BTN_UP | XCP_BTN_RIGHT);
    XCP_CHECK_EQ_HEX(xcp_switch_hat_mask(XCP_SWITCH_HAT_CENTERED), 0);
    XCP_CHECK_EQ_HEX(xcp_switch_hat_mask(0xFF), 0);
}

/* ---------------------------------------------------------------------------
 * Xbox 360
 * ------------------------------------------------------------------------ */

XCP_TEST(xbox360_centred_report)
{
    uint8_t      buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;
    size_t        len = make_360(buf, 0, 0, 0, 0, 0, 0, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
    XCP_CHECK_EQ_INT(s.left_x, 128);
    XCP_CHECK_EQ_INT(s.left_y, 128);
    XCP_CHECK_EQ_INT(s.right_x, 128);
    XCP_CHECK_EQ_INT(s.right_y, 128);
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
    XCP_CHECK_EQ_INT(s.right_trigger, 0);
    XCP_CHECK_EQ_INT(s.connected, 1);
}

XCP_TEST(xbox360_report_skips_rubbish)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;
    size_t        len = make_360(buf, 0, 0, 0, 0, 0, 0, 0, 0);

    buf[0] = 0x01;                                   /* wrong message type */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, len, NULL, &s), 0);

    buf[0] = 0x00; buf[1] = 0x13;                    /* wrong length       */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, len, NULL, &s), 0);

    buf[1] = 0x14;                                   /* good again         */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, len, NULL, &s), 1);

    /* A short buffer must never be read past. */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 19, NULL, &s), 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 0, NULL, &s), 0);
}

XCP_TEST(xbox360_face_buttons_and_shoulders)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;

    /* A + LB */
    make_360(buf, 0, XCP_360_A | XCP_360_LB, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CROSS | XCP_BTN_L1);

    /* B + RB */
    make_360(buf, 0, XCP_360_B | XCP_360_RB, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CIRCLE | XCP_BTN_R1);

    /* X + Y */
    make_360(buf, 0, XCP_360_X | XCP_360_Y, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_SQUARE | XCP_BTN_TRIANGLE);
}

XCP_TEST(xbox360_menu_sticks_guide_dpad)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;

    make_360(buf, XCP_360_START | XCP_360_BACK | XCP_360_L3 | XCP_360_R3, 0,
             0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_OPTIONS | XCP_BTN_SHARE | XCP_BTN_L3 | XCP_BTN_R3 |
                     XCP_BTN_TOUCHPAD);

    /* Guide -> PS bit (upstream guess, see OPEN-2). */
    make_360(buf, 0, XCP_360_GUIDE, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_PS);

    /* D-pad down+left. */
    make_360(buf, XCP_360_DPAD_DOWN | XCP_360_DPAD_LEFT, 0, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_DOWN | XCP_BTN_LEFT);
}

XCP_TEST(xbox360_l3r3_chord_is_touchpad_click)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;

    /* L3 alone: normal stick click, no touchpad. */
    make_360(buf, XCP_360_L3, 0, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_L3);

    /* R3 alone: normal stick click, no touchpad. */
    make_360(buf, XCP_360_R3, 0, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_R3);

    /* L3+R3 together: touchpad click appears alongside them. */
    make_360(buf, XCP_360_L3 | XCP_360_R3, 0, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_L3 | XCP_BTN_R3 | XCP_BTN_TOUCHPAD);
}

XCP_TEST(xbox360_trigger_threshold_and_analog)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;

    /* Just below threshold: analog value present, digital bit absent. */
    make_360(buf, 0, 0, 29, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 29);
    XCP_CHECK_EQ_HEX(s.buttons & XCP_BTN_L2, 0);

    /* Exactly at threshold: digital bit appears. */
    make_360(buf, 0, 0, 30, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons & XCP_BTN_L2, XCP_BTN_L2);

    /* Full press on both. */
    make_360(buf, 0, 0, 255, 255, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 255);
    XCP_CHECK_EQ_HEX(s.buttons & (XCP_BTN_L2 | XCP_BTN_R2), XCP_BTN_L2 | XCP_BTN_R2);
}

XCP_TEST(xbox360_stick_extremes_and_y_inversion)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;

    /* Left stick pinned right and up: X -> 255, Y -> 0 after inversion. */
    make_360(buf, 0, 0, 0, 0, 32767, 32767, -32768, -32768);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, NULL, &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 255);
    XCP_CHECK_EQ_INT(s.left_y, 0);
    XCP_CHECK_EQ_INT(s.right_x, 0);
    XCP_CHECK_EQ_INT(s.right_y, 255);
}

XCP_TEST(xbox360_y_inversion_can_be_disabled)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;
    xcp_config    cfg;

    xcp_config_default(&cfg);
    cfg.invert_left_y  = 0;
    cfg.invert_right_y = 0;

    make_360(buf, 0, 0, 0, 0, 0, 32767, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_INT(s.left_y, 255); /* no inversion -> same direction as X */
}

XCP_TEST(xbox360_ab_swap_option)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;
    xcp_config    cfg;

    xcp_config_default(&cfg);
    cfg.swap_ab = 1;
    cfg.swap_xy = 1;

    make_360(buf, 0, XCP_360_A | XCP_360_X, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CIRCLE | XCP_BTN_TRIANGLE);
}

XCP_TEST(xbox360_post_switch_face_layout)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;
    xcp_config    cfg;

    xcp_config_default(&cfg);
    cfg.manba_post_switch_layout = 1;

    /* The pad is still in its post-Switch state: raw A/B/Y arrive rotated. */
    make_360(buf, 0, XCP_360_A, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_TRIANGLE);

    make_360(buf, 0, XCP_360_B, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CROSS);

    make_360(buf, 0, XCP_360_X, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_SQUARE);

    make_360(buf, 0, XCP_360_Y, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CIRCLE);
}

XCP_TEST(config_for_source_only_flags_switch_to_xbox_transition)
{
    xcp_config    cfg;
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;

    xcp_config_for_source(XCP_CTRL_XBOX360, XCP_CTRL_SWITCH_PRO, &cfg);
    XCP_CHECK_EQ_INT(cfg.manba_post_switch_layout, 1);

    xcp_config_for_source(XCP_CTRL_XBOX360, XCP_CTRL_SWITCH, &cfg);
    XCP_CHECK_EQ_INT(cfg.manba_post_switch_layout, 1);

    xcp_config_for_source(XCP_CTRL_XBOX360, XCP_CTRL_XBOX360, &cfg);
    XCP_CHECK_EQ_INT(cfg.manba_post_switch_layout, 0);

    /* A plain PC reconnect keeps the normal face layout. */
    make_360(buf, 0, XCP_360_A, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CROSS);

    xcp_config_for_source(XCP_CTRL_XBOX360, XCP_CTRL_NONE, &cfg);
    XCP_CHECK_EQ_INT(cfg.manba_post_switch_layout, 0);

    /* Only the Switch -> Xbox transition rotates the face bits. */
    xcp_config_for_source(XCP_CTRL_XBOX360, XCP_CTRL_SWITCH_PRO, &cfg);
    make_360(buf, 0, XCP_360_A, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_TRIANGLE);

    xcp_config_for_source(XCP_CTRL_SWITCH_PRO, XCP_CTRL_XBOX360, &cfg);
    XCP_CHECK_EQ_INT(cfg.manba_post_switch_layout, 0);
    XCP_CHECK_EQ_INT(cfg.invert_left_y, 1);
}

XCP_TEST(xbox360_deadzone_zero_keeps_raw_values)
{
    uint8_t       buf[XCP_XBOX360_REPORT_SIZE];
    xcp_pad_state s;
    xcp_config    cfg;

    xcp_config_default(&cfg);
    cfg.stick_deadzone = 0;

    make_360(buf, 0, 0, 0, 0, 256, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX360, buf, 20, &cfg, &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 129);
}

/* ---------------------------------------------------------------------------
 * Xbox One / Series (GIP)
 * ------------------------------------------------------------------------ */

XCP_TEST(xboxone_centred_report)
{
    uint8_t       buf[XCP_XBOXONE_REPORT_SIZE];
    xcp_pad_state s;
    size_t        len = make_one(buf, 0, 0, 0, 0, 0, 0, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOXONE, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
    XCP_CHECK_EQ_INT(s.left_x, 128);
    XCP_CHECK_EQ_INT(s.left_y, 128);
    XCP_CHECK_EQ_INT(s.connected, 1);
}

XCP_TEST(xboxone_report_skips_rubbish)
{
    uint8_t       buf[XCP_XBOXONE_REPORT_SIZE];
    xcp_pad_state s;
    size_t        len = make_one(buf, 0, 0, 0, 0, 0, 0, 0, 0);

    buf[0] = 0x07; /* guide/other report - not an input report */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOXONE, buf, len, NULL, &s), 0);

    buf[0] = 0x20;
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOXONE, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOXONE, buf, 17, NULL, &s), 0);
}

XCP_TEST(xboxone_full_button_and_axis_decode)
{
    uint8_t       buf[XCP_XBOXONE_REPORT_SIZE];
    xcp_pad_state s;

    /* A + Menu in the low byte, d-pad up + RB in the high byte. */
    make_one(buf, XCP_ONE_A | XCP_ONE_MENU,
             XCP_ONE_DPAD_UP | XCP_ONE_RB,
             1023, 512, 0, -32768, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOXONE, buf, 18, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_CROSS | XCP_BTN_OPTIONS | XCP_BTN_UP |
                     XCP_BTN_R1 | XCP_BTN_L2 | XCP_BTN_R2);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 128);
    XCP_CHECK_EQ_INT(s.left_y, 255); /* fully down after inversion */
    XCP_CHECK_EQ_INT(s.left_x, 128);
}

XCP_TEST(xboxone_view_stickclicks_and_dpad_bits)
{
    uint8_t       buf[XCP_XBOXONE_REPORT_SIZE];
    xcp_pad_state s;

    make_one(buf, XCP_ONE_VIEW | XCP_ONE_Y,
             XCP_ONE_L3 | XCP_ONE_R3 | XCP_ONE_DPAD_RIGHT | XCP_ONE_DPAD_DOWN,
             0, 0, 0, 0, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOXONE, buf, 18, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_SHARE | XCP_BTN_TRIANGLE | XCP_BTN_L3 |
                     XCP_BTN_R3 | XCP_BTN_RIGHT | XCP_BTN_DOWN |
                     XCP_BTN_TOUCHPAD);
}

/* ---------------------------------------------------------------------------
 * Xbox Series X|S
 * ------------------------------------------------------------------------ */

XCP_TEST(xbox_series_share_is_at_the_end_of_the_packet)
{
    uint8_t       buf[48];
    xcp_pad_state s;
    size_t        len;

    /* 44-byte transfer (40-byte payload, fw 5.1): Share is byte 18 (44 - 26). */
    len = make_series(buf, 44, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX_SERIES, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);

    len = make_series(buf, 44, 0, 0, 0, 0, 0, 0, 0, 0, 1);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX_SERIES, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_SHARE);

    /* 48-byte transfer (44-byte payload, fw 5.5): Share moved to byte 22. */
    len = make_series(buf, 48, 0, 0, 0, 0, 0, 0, 0, 0, 1);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX_SERIES, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_SHARE);

    /* A stray bit at byte 18 must not count when the transfer is 48 bytes. */
    len = make_series(buf, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    buf[18] = XCP_XBOX_SERIES_SHARE_BIT;
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX_SERIES, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);

    /* Short report: no Share byte to read; still decodes the buttons. */
    len = make_series(buf, 18, XCP_ONE_A, 0, 0, 0, 0, 0, 0, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX_SERIES, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CROSS);

    /* A truncated packet must not read Share out of the header/sequence. */
    len = make_series(buf, 27, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    buf[1] = XCP_XBOX_SERIES_SHARE_BIT;
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX_SERIES, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
}

XCP_TEST(xbox_series_standard_buttons_decode_like_xbox_one)
{
    uint8_t       buf[44];
    xcp_pad_state s;
    size_t        len = make_series(buf, 44,
                                    XCP_ONE_MENU | XCP_ONE_VIEW | XCP_ONE_A,
                                    XCP_ONE_LB | XCP_ONE_R3 | XCP_ONE_DPAD_UP,
                                    1023, 0, 0, -32768, 0, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_XBOX_SERIES, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_OPTIONS | XCP_BTN_SHARE | XCP_BTN_CROSS |
                     XCP_BTN_L1 | XCP_BTN_R3 | XCP_BTN_UP | XCP_BTN_L2);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.left_y, 255);
}

/* ---------------------------------------------------------------------------
 * DualSense / DualSense Edge
 * ------------------------------------------------------------------------ */

XCP_TEST(dualsense_centred_report)
{
    uint8_t       buf[XCP_DUALSENSE_REPORT_SIZE];
    xcp_pad_state s;
    size_t        len = make_dualsense(buf, XCP_SWITCH_HAT_CENTERED, 0, 0,
                                       128, 128, 128, 128, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
    XCP_CHECK_EQ_INT(s.left_x, 128);
    XCP_CHECK_EQ_INT(s.left_y, 128);
    XCP_CHECK_EQ_INT(s.right_x, 128);
    XCP_CHECK_EQ_INT(s.right_y, 128);
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
    XCP_CHECK_EQ_INT(s.right_trigger, 0);
    XCP_CHECK_EQ_INT(s.connected, 1);

    /* Validation only needs the 11-byte prefix, not the full USB report. */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_MIN_SIZE, NULL, &s), 1);
}

XCP_TEST(dualsense_face_buttons_shoulders_and_menu)
{
    uint8_t       buf[XCP_DUALSENSE_REPORT_SIZE];
    xcp_pad_state s;

    make_dualsense(buf,
                   XCP_SWITCH_HAT_CENTERED | XCP_DS_SQUARE | XCP_DS_CROSS |
                       XCP_DS_CIRCLE | XCP_DS_TRIANGLE,
                   XCP_DS_L1 | XCP_DS_R1 | XCP_DS_CREATE | XCP_DS_OPTIONS |
                       XCP_DS_L3 | XCP_DS_R3,
                   XCP_DS_PS | XCP_DS_TOUCHPAD,
                   128, 128, 128, 128, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_SQUARE | XCP_BTN_CROSS | XCP_BTN_CIRCLE |
                     XCP_BTN_TRIANGLE | XCP_BTN_L1 | XCP_BTN_R1 |
                     XCP_BTN_SHARE | XCP_BTN_OPTIONS | XCP_BTN_L3 |
                     XCP_BTN_R3 | XCP_BTN_PS | XCP_BTN_TOUCHPAD);
}

XCP_TEST(dualsense_triggers_hat_and_sticks)
{
    uint8_t       buf[XCP_DUALSENSE_REPORT_SIZE];
    xcp_pad_state s;

    /* D-pad up-right; sticks full low/low and high/high (no Y inversion). */
    make_dualsense(buf, XCP_SWITCH_HAT_UP_RIGHT, 0, 0,
                   0, 0, 255, 255, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_UP | XCP_BTN_RIGHT);
    XCP_CHECK_EQ_INT(s.left_x, 0);
    XCP_CHECK_EQ_INT(s.left_y, 0);
    XCP_CHECK_EQ_INT(s.right_x, 255);
    XCP_CHECK_EQ_INT(s.right_y, 255);

    /* One step below threshold: analog value only. */
    make_dualsense(buf, XCP_SWITCH_HAT_CENTERED, 0, 0,
                   128, 128, 128, 128, 29, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 29);
    XCP_CHECK_EQ_HEX(s.buttons & XCP_BTN_L2, 0);

    /* Exactly at threshold: digital bit appears. */
    make_dualsense(buf, XCP_SWITCH_HAT_CENTERED, 0, 0,
                   128, 128, 128, 128, 30, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons & XCP_BTN_L2, XCP_BTN_L2);

    /* Digital bit with a zero analog value: force full travel (Linux quirk). */
    make_dualsense(buf, XCP_SWITCH_HAT_CENTERED, XCP_DS_L2 | XCP_DS_R2, 0,
                   128, 128, 128, 128, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 255);
    XCP_CHECK_EQ_HEX(s.buttons & (XCP_BTN_L2 | XCP_BTN_R2),
                     XCP_BTN_L2 | XCP_BTN_R2);
}

XCP_TEST(dualsense_all_hat_directions_and_trigger_release)
{
    static const uint8_t hats[8] = {
        XCP_SWITCH_HAT_UP, XCP_SWITCH_HAT_UP_RIGHT, XCP_SWITCH_HAT_RIGHT,
        XCP_SWITCH_HAT_DOWN_RIGHT, XCP_SWITCH_HAT_DOWN,
        XCP_SWITCH_HAT_DOWN_LEFT, XCP_SWITCH_HAT_LEFT,
        XCP_SWITCH_HAT_UP_LEFT,
    };
    static const uint32_t expected[8] = {
        XCP_BTN_UP, XCP_BTN_UP | XCP_BTN_RIGHT, XCP_BTN_RIGHT,
        XCP_BTN_DOWN | XCP_BTN_RIGHT, XCP_BTN_DOWN,
        XCP_BTN_DOWN | XCP_BTN_LEFT, XCP_BTN_LEFT,
        XCP_BTN_UP | XCP_BTN_LEFT,
    };
    uint8_t       buf[XCP_DUALSENSE_REPORT_SIZE];
    xcp_pad_state s;
    int           i;

    for (i = 0; i < 8; i++) {
        make_dualsense(buf, hats[i], 0, 0, 128, 128, 128, 128, 0, 0);
        XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                    XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
        XCP_CHECK_EQ_HEX(s.buttons, expected[i]);
    }

    make_dualsense(buf, XCP_SWITCH_HAT_CENTERED, 0, 0,
                   128, 128, 128, 128, 255, 255);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_L2 | XCP_BTN_R2);

    /* Both analog triggers back at zero: the digital bits must go away too. */
    make_dualsense(buf, XCP_SWITCH_HAT_CENTERED, 0, 0,
                   128, 128, 128, 128, 0, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
    XCP_CHECK_EQ_INT(s.right_trigger, 0);
}

XCP_TEST(dualsense_edge_extras_are_not_mapped)
{
    uint8_t       buf[XCP_DUALSENSE_REPORT_SIZE];
    xcp_pad_state s;

    make_dualsense(buf,
                   XCP_SWITCH_HAT_CENTERED | XCP_DS_CROSS,
                   0,
                   XCP_DS_MUTE | XCP_DS_EDGE_FN_L | XCP_DS_EDGE_FN_R |
                       XCP_DS_EDGE_PADDLE_L | XCP_DS_EDGE_PADDLE_R,
                   128, 128, 128, 128, 0, 0);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 1);
    /* Mute and the Edge's Fn/paddles have no DS4 equivalent and stay off. */
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CROSS);
}

XCP_TEST(dualsense_report_skips_rubbish)
{
    uint8_t       buf[XCP_DUALSENSE_REPORT_SIZE];
    xcp_pad_state s;

    make_dualsense(buf, XCP_SWITCH_HAT_CENTERED, 0, 0,
                   128, 128, 128, 128, 0, 0);

    buf[0] = 0x02; /* not the DualSense input report */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf,
                                XCP_DUALSENSE_REPORT_SIZE, NULL, &s), 0);

    buf[0] = XCP_DUALSENSE_REPORT_ID;
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf, 10, NULL, &s), 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_DUALSENSE, buf, 11, NULL, &s), 1);
}

/* ---------------------------------------------------------------------------
 * Switch input-only
 * ------------------------------------------------------------------------ */

XCP_TEST(switch_positional_face_buttons_and_hat)
{
    uint8_t       buf[7];
    xcp_pad_state s;
    size_t        len = make_switch(buf, XCP_SWITCH_A | XCP_SWITCH_L,
                                    XCP_SWITCH_PLUS | XCP_SWITCH_HOME,
                                    XCP_SWITCH_HAT_RIGHT,
                                    128, 128, 128, 128);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_CIRCLE | XCP_BTN_L1 | XCP_BTN_OPTIONS |
                     XCP_BTN_PS | XCP_BTN_RIGHT);
    XCP_CHECK_EQ_INT(s.left_x, 128);
    XCP_CHECK_EQ_INT(s.left_y, 128); /* Switch path does not invert Y */
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
}

XCP_TEST(switch_zl_zr_are_digital)
{
    uint8_t       buf[7];
    xcp_pad_state s;

    make_switch(buf, XCP_SWITCH_ZL | XCP_SWITCH_ZR, 0, XCP_SWITCH_HAT_CENTERED,
                128, 128, 128, 128);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH, buf, 7, NULL, &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 255);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_L2 | XCP_BTN_R2);
}

XCP_TEST(switch_pro_full_report)
{
    uint8_t       buf[64];
    xcp_pad_state s;
    size_t        len = make_switch_pro(buf,
                                        XCP_SWPRO_A | XCP_SWPRO_R |
                                        XCP_SWPRO_ZR,
                                        XCP_SWPRO_PLUS | XCP_SWPRO_MINUS |
                                        XCP_SWPRO_L3 | XCP_SWPRO_R3 |
                                        XCP_SWPRO_HOME | XCP_SWPRO_CAPTURE,
                                        XCP_SWPRO_UP | XCP_SWPRO_LEFT |
                                        XCP_SWPRO_L | XCP_SWPRO_ZL,
                                        2048, 2048, 2048, 2048);

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, len, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_CIRCLE | XCP_BTN_L1 | XCP_BTN_R1 |
                     XCP_BTN_R2 | XCP_BTN_OPTIONS | XCP_BTN_SHARE |
                     XCP_BTN_L3 | XCP_BTN_R3 | XCP_BTN_PS | XCP_BTN_UP |
                     XCP_BTN_LEFT | XCP_BTN_L2);
    XCP_CHECK_EQ_INT(s.left_x, 128);
    XCP_CHECK_EQ_INT(s.left_y, 128);
    XCP_CHECK_EQ_INT(s.right_x, 128);
    XCP_CHECK_EQ_INT(s.right_y, 128);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 255);
}

/*
 * Pinned to RAW bytes, not to the XCP_SWPRO_* names: a mislabelled constant
 * would otherwise make the test agree with the bug. Byte 3 of a real Switch Pro
 * 0x30 report is Y=0x01 X=0x02 B=0x04 A=0x08 (the PDP/HORI HID order is
 * different). The decoder is positional: Nintendo A (east) -> Circle,
 * B (south) -> Cross, X (north) -> Triangle, Y (west) -> Square.
 */
XCP_TEST(switch_pro_face_bits_are_the_real_report_layout)
{
    uint8_t       buf[64];
    xcp_pad_state s;

    make_switch_pro(buf, 0x04, 0, 0, 2048, 2048, 2048, 2048); /* B */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CROSS);

    make_switch_pro(buf, 0x08, 0, 0, 2048, 2048, 2048, 2048); /* A */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_CIRCLE);

    make_switch_pro(buf, 0x02, 0, 0, 2048, 2048, 2048, 2048); /* X */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_TRIANGLE);

    make_switch_pro(buf, 0x01, 0, 0, 2048, 2048, 2048, 2048); /* Y */
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_SQUARE);
}

XCP_TEST(gip_guide_message_is_decoded)
{
    uint8_t down[6]   = { 0x07, 0x20, 0x00, 0x02, 0x01, 0x5B };
    uint8_t up[6]     = { 0x07, 0x20, 0x01, 0x02, 0x00, 0x5B };
    uint8_t input[18] = { 0x20 };

    XCP_CHECK_EQ_INT(xcp_gip_guide_state(down, sizeof(down)), 1);
    XCP_CHECK_EQ_INT(xcp_gip_guide_state(up, sizeof(up)), 0);
    XCP_CHECK_EQ_INT(xcp_gip_guide_state(input, sizeof(input)), -1);
    XCP_CHECK_EQ_INT(xcp_gip_guide_state(down, 4), -1);
    XCP_CHECK_EQ_INT(xcp_gip_guide_state(NULL, 6), -1);
}

XCP_TEST(wireless_360_receiver_is_not_treated_as_gip)
{
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x0719), XCP_CTRL_NONE);
    /* Even if its interface looks like nothing we know. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x045E, 0x0719,
                                                       0xFF, 0x5D, 0x81, 1),
                     XCP_CTRL_NONE);
}

XCP_TEST(switch_pro_12bit_sticks)
{
    uint8_t       buf[64];
    xcp_pad_state s;
    xcp_config    cfg;

    xcp_config_default_switch_pro(&cfg);
    cfg.stick_deadzone = 0;
    make_switch_pro(buf, 0, 0, 0, 0, 4095, 1024, 3072);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, &cfg, &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 0);
    XCP_CHECK_EQ_INT(s.left_y, 0);
    XCP_CHECK_EQ_INT(s.right_x, 64);
    XCP_CHECK_EQ_INT(s.right_y, 63);
}

XCP_TEST(switch_pro_default_config_inverts_y)
{
    uint8_t       buf[64];
    xcp_pad_state s;
    xcp_config    cfg;

    xcp_config_default_switch_pro(&cfg);
    cfg.stick_deadzone = 0;
    make_switch_pro(buf, 0, 0, 0, 1024, 0, 3072, 4095);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, &cfg, &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 64);
    XCP_CHECK_EQ_INT(s.left_y, 255);
    XCP_CHECK_EQ_INT(s.right_x, 192);
    XCP_CHECK_EQ_INT(s.right_y, 0);
}

XCP_TEST(switch_pro_49_byte_report)
{
    uint8_t       buf[64];
    xcp_pad_state s;

    make_switch_pro(buf,
                    XCP_SWPRO_Y | XCP_SWPRO_B | XCP_SWPRO_A | XCP_SWPRO_X |
                    XCP_SWPRO_R | XCP_SWPRO_ZR,
                    XCP_SWPRO_MINUS | XCP_SWPRO_PLUS |
                    XCP_SWPRO_L3 | XCP_SWPRO_R3 |
                    XCP_SWPRO_HOME | XCP_SWPRO_CAPTURE,
                    XCP_SWPRO_LEFT | XCP_SWPRO_L | XCP_SWPRO_ZL,
                    2048, 2048, 2048, 2048);

    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_SWITCH_PRO, buf, 49));
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_SWITCH_PRO, buf, 64));
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 49, NULL, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_SQUARE | XCP_BTN_CROSS | XCP_BTN_CIRCLE |
                     XCP_BTN_TRIANGLE | XCP_BTN_R1 | XCP_BTN_R2 |
                     XCP_BTN_SHARE | XCP_BTN_OPTIONS | XCP_BTN_L3 |
                     XCP_BTN_R3 | XCP_BTN_PS | XCP_BTN_LEFT |
                     XCP_BTN_L1 | XCP_BTN_L2);
    XCP_CHECK_EQ_INT(s.left_x, 128);
    XCP_CHECK_EQ_INT(s.left_y, 128);
    XCP_CHECK_EQ_INT(s.right_x, 128);
    XCP_CHECK_EQ_INT(s.right_y, 128);
}

XCP_TEST(switch_pro_y_axis_orientation)
{
    uint8_t       buf[64];
    xcp_pad_state s;
    xcp_config    cfg;

    xcp_config_default_switch_pro(&cfg);
    cfg.stick_deadzone = 0;

    /* Raw 0 is the top of the stick; after inversion it is PS4 "up" (255). */
    make_switch_pro(buf, 0, 0, 0, 2048, 0, 2048, 4095);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, &cfg, &s), 1);
    XCP_CHECK_EQ_INT(s.left_y, 255);
    XCP_CHECK_EQ_INT(s.right_y, 0);

    make_switch_pro(buf, 0, 0, 0, 2048, 4095, 2048, 0);
    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH_PRO, buf, 64, &cfg, &s), 1);
    XCP_CHECK_EQ_INT(s.left_y, 0);
    XCP_CHECK_EQ_INT(s.right_y, 255);
}

XCP_TEST(switch_pdp_full_report)
{
    uint8_t       buf[7];
    xcp_pad_state s;
    xcp_config    cfg;
    size_t        len = make_switch(buf,
                                    XCP_SWITCH_Y | XCP_SWITCH_B |
                                    XCP_SWITCH_A | XCP_SWITCH_X |
                                    XCP_SWITCH_L | XCP_SWITCH_R |
                                    XCP_SWITCH_ZL | XCP_SWITCH_ZR,
                                    XCP_SWITCH_MINUS | XCP_SWITCH_PLUS |
                                    XCP_SWITCH_L3 | XCP_SWITCH_R3 |
                                    XCP_SWITCH_HOME,
                                    XCP_SWITCH_HAT_DOWN_LEFT,
                                    0, 255, 255, 0);

    xcp_config_default_switch(&cfg);
    cfg.stick_deadzone = 0;

    XCP_CHECK_EQ_INT(xcp_decode(XCP_CTRL_SWITCH, buf, len, &cfg, &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons,
                     XCP_BTN_SQUARE | XCP_BTN_CROSS | XCP_BTN_CIRCLE |
                     XCP_BTN_TRIANGLE | XCP_BTN_L1 | XCP_BTN_R1 |
                     XCP_BTN_L2 | XCP_BTN_R2 | XCP_BTN_SHARE |
                     XCP_BTN_OPTIONS | XCP_BTN_L3 | XCP_BTN_R3 |
                     XCP_BTN_PS | XCP_BTN_DOWN | XCP_BTN_LEFT);
    XCP_CHECK_EQ_INT(s.left_x, 0);
    XCP_CHECK_EQ_INT(s.left_y, 255); /* Switch PDP is not Y-inverted */
    XCP_CHECK_EQ_INT(s.right_x, 255);
    XCP_CHECK_EQ_INT(s.right_y, 0);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 255);
}

/* ---------------------------------------------------------------------------
 * Report validity helper
 * ------------------------------------------------------------------------ */

XCP_TEST(report_looks_valid)
{
    uint8_t b[64];

    memset(b, 0, sizeof(b));
    b[0] = 0x00; b[1] = 0x14;
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_XBOX360, b, 20));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_XBOX360, b, 19));

    b[0] = 0x20;
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_XBOXONE, b, 18));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_XBOXONE, b, 17));
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_XBOX_SERIES, b, 18));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_XBOX_SERIES, b, 17));

    b[0] = XCP_DUALSENSE_REPORT_ID;
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_DUALSENSE, b,
                                     XCP_DUALSENSE_MIN_SIZE));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_DUALSENSE, b,
                                           XCP_DUALSENSE_MIN_SIZE - 1));

    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_SWITCH, b, 7));
    b[0] = XCP_SWITCH_PRO_REPORT_ID;
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_SWITCH_PRO, b,
                                     XCP_SWITCH_PRO_MIN_SIZE));
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_SWITCH_PRO, b, 49));
    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_SWITCH_PRO, b, 64));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_SWITCH_PRO, b,
                                           XCP_SWITCH_PRO_MIN_SIZE - 1));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_NONE, b, 7));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_XBOX360, NULL, 20));
}

XCP_TEST(controller_type_for_ids)
{
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x028E), XCP_CTRL_XBOX360);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x02EA), XCP_CTRL_XBOXONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x0B12),
                     XCP_CTRL_XBOX_SERIES);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x0B13),
                     XCP_CTRL_XBOX_SERIES);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x0B20),
                     XCP_CTRL_XBOX_SERIES);
    /* Unknown Microsoft pid is assumed to be a GIP pad. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x1234), XCP_CTRL_XBOXONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x054C, 0x0CE6),
                     XCP_CTRL_DUALSENSE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x054C, 0x0DF2),
                     XCP_CTRL_DUALSENSE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x0E6F, 0x0187), XCP_CTRL_SWITCH);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x057E, 0x2009),
                     XCP_CTRL_SWITCH_PRO);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x0E6F, 0x9999), XCP_CTRL_NONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x054C, 0x09CC), XCP_CTRL_NONE);

    XCP_CHECK(xcp_is_xbox_series_pid(0x0B12));
    XCP_CHECK_FALSE(xcp_is_xbox_series_pid(0x02EA));
    XCP_CHECK(xcp_is_xboxone_pid(0x0B12));
    XCP_CHECK(xcp_is_dualsense_pid(0x0CE6));
    XCP_CHECK(xcp_is_dualsense_pid(0x0DF2));
    XCP_CHECK_FALSE(xcp_is_dualsense_pid(0x09CC));
}

void xcp_register_translate_tests(void)
{
    XCP_RUN(signed_axis_maps_to_ps4_range);
    XCP_RUN(ten_bit_trigger_maps_to_eight_bit);
    XCP_RUN(deadzone_snaps_inside_and_rescales_outside);
    XCP_RUN(dpad_bits_to_mask);
    XCP_RUN(switch_hat_masks);

    XCP_RUN(xbox360_centred_report);
    XCP_RUN(xbox360_report_skips_rubbish);
    XCP_RUN(xbox360_face_buttons_and_shoulders);
    XCP_RUN(xbox360_menu_sticks_guide_dpad);
    XCP_RUN(xbox360_l3r3_chord_is_touchpad_click);
    XCP_RUN(xbox360_trigger_threshold_and_analog);
    XCP_RUN(xbox360_stick_extremes_and_y_inversion);
    XCP_RUN(xbox360_y_inversion_can_be_disabled);
    XCP_RUN(xbox360_ab_swap_option);
    XCP_RUN(xbox360_post_switch_face_layout);
    XCP_RUN(config_for_source_only_flags_switch_to_xbox_transition);
    XCP_RUN(xbox360_deadzone_zero_keeps_raw_values);

    XCP_RUN(xboxone_centred_report);
    XCP_RUN(xboxone_report_skips_rubbish);
    XCP_RUN(xboxone_full_button_and_axis_decode);
    XCP_RUN(xboxone_view_stickclicks_and_dpad_bits);

    XCP_RUN(xbox_series_share_is_at_the_end_of_the_packet);
    XCP_RUN(xbox_series_standard_buttons_decode_like_xbox_one);

    XCP_RUN(dualsense_centred_report);
    XCP_RUN(dualsense_face_buttons_shoulders_and_menu);
    XCP_RUN(dualsense_triggers_hat_and_sticks);
    XCP_RUN(dualsense_all_hat_directions_and_trigger_release);
    XCP_RUN(dualsense_edge_extras_are_not_mapped);
    XCP_RUN(dualsense_report_skips_rubbish);

    XCP_RUN(switch_positional_face_buttons_and_hat);
    XCP_RUN(switch_zl_zr_are_digital);
    XCP_RUN(switch_pro_full_report);
    XCP_RUN(switch_pro_face_bits_are_the_real_report_layout);
    XCP_RUN(wireless_360_receiver_is_not_treated_as_gip);
    XCP_RUN(gip_guide_message_is_decoded);
    XCP_RUN(switch_pro_49_byte_report);
    XCP_RUN(switch_pro_12bit_sticks);
    XCP_RUN(switch_pro_default_config_inverts_y);
    XCP_RUN(switch_pro_y_axis_orientation);
    XCP_RUN(switch_pdp_full_report);

    XCP_RUN(report_looks_valid);
    XCP_RUN(controller_type_for_ids);
}
