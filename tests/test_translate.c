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

    XCP_CHECK(xcp_report_looks_valid(XCP_CTRL_SWITCH, b, 7));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_NONE, b, 7));
    XCP_CHECK_FALSE(xcp_report_looks_valid(XCP_CTRL_XBOX360, NULL, 20));
}

XCP_TEST(controller_type_for_ids)
{
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x028E), XCP_CTRL_XBOX360);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x02EA), XCP_CTRL_XBOXONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x0B12), XCP_CTRL_XBOXONE);
    /* Unknown Microsoft pid is assumed to be a GIP pad. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x045E, 0x1234), XCP_CTRL_XBOXONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x0E6F, 0x0187), XCP_CTRL_SWITCH);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x0E6F, 0x9999), XCP_CTRL_NONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_ids(0x054C, 0x09CC), XCP_CTRL_NONE);
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
    XCP_RUN(xbox360_deadzone_zero_keeps_raw_values);

    XCP_RUN(xboxone_centred_report);
    XCP_RUN(xboxone_report_skips_rubbish);
    XCP_RUN(xboxone_full_button_and_axis_decode);
    XCP_RUN(xboxone_view_stickclicks_and_dpad_bits);

    XCP_RUN(switch_positional_face_buttons_and_hat);
    XCP_RUN(switch_zl_zr_are_digital);

    XCP_RUN(report_looks_valid);
    XCP_RUN(controller_type_for_ids);
}
