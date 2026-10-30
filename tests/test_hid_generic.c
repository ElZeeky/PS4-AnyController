/*
 * tests/test_hid_generic.c - descriptor parsing, the generic decoder and the
 * interface-based classification that decides when to try it.
 */
#include "framework.h"

#include "xcp/hid_generic.h"
#include "xcp/report.h"
#include "xcp/usb_backend.h"

#include <string.h>

/*
 * A conventional DirectInput-style gamepad:
 *   11 buttons, X/Y/Z/Rz (left stick, right stick), a 4-bit hat.
 * Total input payload is 7 bytes: two button bytes, four axes, one hat.
 */
static const uint8_t g_gamepad_desc[] = {
    0x05, 0x01,             /* Usage Page (Generic Desktop)        */
    0x09, 0x05,             /* Usage (Game Pad)                    */
    0xA1, 0x01,             /* Collection (Application)            */
    0x05, 0x09,             /*   Usage Page (Button)               */
    0x19, 0x01,             /*   Usage Minimum (1)                 */
    0x29, 0x0B,             /*   Usage Maximum (11)                */
    0x15, 0x00,             /*   Logical Minimum (0)               */
    0x25, 0x01,             /*   Logical Maximum (1)               */
    0x75, 0x01,             /*   Report Size (1)                   */
    0x95, 0x0B,             /*   Report Count (11)                 */
    0x81, 0x02,             /*   Input (Data,Var,Abs)              */
    0x75, 0x01,             /*   Report Size (1)                   */
    0x95, 0x01,             /*   Report Count (1)                  */
    0x81, 0x01,             /*   Input (Const)                     */
    0x75, 0x04,             /*   Report Size (4)  (byte-align)     */
    0x95, 0x01,             /*   Report Count (1)                  */
    0x81, 0x01,             /*   Input (Const)                     */
    0x05, 0x01,             /*   Usage Page (Generic Desktop)      */
    0x09, 0x30,             /*   Usage (X)                         */
    0x09, 0x31,             /*   Usage (Y)                         */
    0x09, 0x32,             /*   Usage (Z)                         */
    0x09, 0x35,             /*   Usage (Rz)                        */
    0x15, 0x00,             /*   Logical Minimum (0)               */
    0x26, 0xFF, 0x00,       /*   Logical Maximum (255)             */
    0x75, 0x08,             /*   Report Size (8)                   */
    0x95, 0x04,             /*   Report Count (4)                  */
    0x81, 0x02,             /*   Input (Data,Var,Abs)              */
    0x09, 0x39,             /*   Usage (Hat switch)                */
    0x15, 0x00,             /*   Logical Minimum (0)               */
    0x25, 0x07,             /*   Logical Maximum (7)               */
    0x75, 0x04,             /*   Report Size (4)                   */
    0x95, 0x01,             /*   Report Count (1)                  */
    0x81, 0x02,             /*   Input (Data,Var,Abs)              */
    0x75, 0x04,             /*   Report Size (4)                   */
    0x95, 0x01,             /*   Report Count (1)                  */
    0x81, 0x01,             /*   Input (Const)                     */
    0xC0,                   /* End Collection                      */
};

/* The same layout with Report ID 7. */
static const uint8_t g_gamepad_report_id_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x07,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0B,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0B, 0x81, 0x02,
    0x75, 0x01, 0x95, 0x01, 0x81, 0x01,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01, 0xC0,
};

/* A mouse collection: X/Y plus three buttons. It must not be accepted. */
static const uint8_t g_mouse_desc[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x75, 0x05, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
    0xC0, 0xC0,
};

/* Four buttons, then signed 8-bit X/Y (-127..127). */
static const uint8_t g_signed_axes_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0,
};

/* Report 1 carries buttons, report 2 carries X/Y. */
static const uint8_t g_report_ids_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x85, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0,
};

/* Nonstandard hat range: logical 1..8 instead of the usual 0..7. */
static const uint8_t g_hat_min_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x09, 0x39,
    0x15, 0x01, 0x25, 0x08, 0x75, 0x04, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0xC0,
};

/*
 * Logitech/DirectInput-style axes: X/Y left, Z/Rz right, Rx/Ry triggers.
 * This is the layout the generic decoder's axis heuristic targets.
 */
static const uint8_t g_triggers_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x09, 0x33, 0x09, 0x34,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02,
    0xC0,
};

/* A keyboard collection: LED/button usages on page 0x07, no gamepad axes. */
static const uint8_t g_keyboard_desc[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x05, 0x07, 0x19, 0x00, 0x29, 0xFF,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x00,
    0xC0,
};

XCP_TEST(hid_generic_parses_buttons_axes_and_hat)
{
    xcp_hid_map m;

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_gamepad_desc,
                                              sizeof(g_gamepad_desc),
                                              0x1234, 0x5678, &m), 1);
    XCP_CHECK_EQ_INT(m.has_report_ids, 0);
    XCP_CHECK_EQ_INT(m.button_count, 11);
    XCP_CHECK_EQ_INT(m.axis[XCP_HID_AXIS_X].bit_offset, 16);
    XCP_CHECK_EQ_INT(m.axis[XCP_HID_AXIS_Y].bit_offset, 24);
    XCP_CHECK_EQ_INT(m.axis[XCP_HID_AXIS_Z].bit_offset, 32);
    XCP_CHECK_EQ_INT(m.axis[XCP_HID_AXIS_RZ].bit_offset, 40);
    XCP_CHECK_EQ_INT(m.hat.bit_offset, 48);
    XCP_CHECK_EQ_INT(m.hat.bit_size, 4);

    /* 11 buttons is the DirectInput order: 1=Square, 2=Cross, ... */
    XCP_CHECK_EQ_HEX(m.button_bits[0], XCP_BTN_SQUARE);
    XCP_CHECK_EQ_HEX(m.button_bits[1], XCP_BTN_CROSS);
    XCP_CHECK_EQ_HEX(m.button_bits[2], XCP_BTN_CIRCLE);
    XCP_CHECK_EQ_HEX(m.button_bits[3], XCP_BTN_TRIANGLE);
}

XCP_TEST(hid_generic_decodes_a_report)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[7];

    xcp_hid_parse_descriptor(g_gamepad_desc, sizeof(g_gamepad_desc),
                             0x1234, 0x5678, &m);
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);

    memset(r, 0, sizeof(r));
    r[0] = 0x02;      /* button 2 = Cross                           */
    r[2] = 0xFF;      /* X maximum                                  */
    r[3] = 0x00;      /* Y minimum                                  */
    r[4] = 0x00;      /* Z minimum                                  */
    r[5] = 0xFF;      /* Rz maximum                                 */
    r[6] = 0x02;      /* hat: right                                 */

    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK(s.connected);
    XCP_CHECK(s.buttons & XCP_BTN_CROSS);
    XCP_CHECK_FALSE(s.buttons & XCP_BTN_SQUARE);
    XCP_CHECK(s.buttons & XCP_BTN_RIGHT);
    XCP_CHECK_FALSE(s.buttons & XCP_BTN_LEFT);
    XCP_CHECK_EQ_INT(s.left_x, 255);
    XCP_CHECK_EQ_INT(s.left_y, 0);
    XCP_CHECK_EQ_INT(s.right_x, 0);
    XCP_CHECK_EQ_INT(s.right_y, 255);
}

XCP_TEST(hid_generic_uses_the_report_id)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[8];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_gamepad_report_id_desc,
                                              sizeof(g_gamepad_report_id_desc),
                                              0x1234, 0x5678, &m), 1);
    XCP_CHECK_EQ_INT(m.has_report_ids, 1);
    XCP_CHECK_EQ_INT(m.button[0].report_id, 7);

    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    memset(r, 0, sizeof(r));
    r[0] = 7;
    r[1] = 0x04;      /* button 3 = Circle in DirectInput order     */
    r[2] = 0x80; r[3] = 0x80; r[4] = 0x80; r[5] = 0x80;
    r[6] = 0x0F;      /* hat null                                   */

    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK(s.buttons & XCP_BTN_CIRCLE);
    XCP_CHECK_FALSE(s.buttons & XCP_BTN_CROSS);

    /* A different report id matches nothing and must not clobber state. */
    r[0] = 8;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 0);
    XCP_CHECK(s.buttons & XCP_BTN_CIRCLE);
}

XCP_TEST(hid_generic_rejects_a_mouse)
{
    xcp_hid_map m;

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_mouse_desc,
                                              sizeof(g_mouse_desc),
                                              0x1234, 0x5678, &m), 0);
}

XCP_TEST(hid_generic_decodes_signed_axes)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[3];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_signed_axes_desc,
                                              sizeof(g_signed_axes_desc),
                                              0x1234, 0x5678, &m), 1);
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    s.buttons = XCP_BTN_CROSS;

    memset(r, 0, sizeof(r));
    r[1] = 0x81; /* -127 */
    r[2] = 0x7F; /* +127 */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 0);
    XCP_CHECK_EQ_INT(s.left_y, 255);
    XCP_CHECK_EQ_HEX(s.buttons, 0); /* button fields are present and released */

    r[1] = 0x00;
    r[2] = 0x00;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 127);
    XCP_CHECK_EQ_INT(s.left_y, 127);
}

XCP_TEST(hid_generic_report_ids_split_fields)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[3];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_report_ids_desc,
                                              sizeof(g_report_ids_desc),
                                              0x1234, 0x5678, &m), 1);
    XCP_CHECK_EQ_INT(m.has_report_ids, 1);

    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);

    r[0] = 1;
    r[1] = 0x02; /* button 2 = Cross */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, 2, &s), 1);
    XCP_CHECK(s.buttons & XCP_BTN_CROSS);

    r[0] = 2;
    r[1] = 0xFF;
    r[2] = 0x00;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 255);
    XCP_CHECK_EQ_INT(s.left_y, 0);
    /* Report 2 carries no buttons, so the previous button state survives. */
    XCP_CHECK(s.buttons & XCP_BTN_CROSS);

    /* A short report for report 2 must be rejected, not decoded as zeros. */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, 2, &s), 0);
    XCP_CHECK_EQ_INT(s.left_x, 255);
    XCP_CHECK_EQ_INT(s.left_y, 0);

    r[0] = 3; /* no fields for this id */
    r[1] = 0;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, 2, &s), 0);
}

XCP_TEST(hid_generic_nonstandard_hat_range)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[4];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_hat_min_desc,
                                              sizeof(g_hat_min_desc),
                                              0x1234, 0x5678, &m), 1);

    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    memset(r, 0, sizeof(r));
    r[1] = 128;
    r[2] = 128;

    r[3] = 1; /* logical 1 = up */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_UP);

    r[3] = 3; /* logical 3 = right */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_RIGHT);

    r[3] = 8; /* logical 8 = up-left */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_UP | XCP_BTN_LEFT);

    r[3] = 0; /* below the declared minimum: centred */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);

    r[3] = 15; /* outside the declared range: centred */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
}

XCP_TEST(hid_generic_triggers_release)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[7];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_triggers_desc,
                                              sizeof(g_triggers_desc),
                                              0x1234, 0x5678, &m), 1);
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    memset(r, 0, sizeof(r));
    r[1] = 128; /* X */
    r[2] = 128; /* Y */
    r[3] = 0;   /* Z  = right X */
    r[4] = 255; /* Rz = right Y */
    r[5] = 255; /* Rx = left trigger */
    r[6] = 0;   /* Ry = right trigger */

    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 0);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_L2);
    XCP_CHECK_EQ_INT(s.right_x, 0);
    XCP_CHECK_EQ_INT(s.right_y, 255);

    /* Release L2 and press R2: L2 must not stay stuck. */
    r[5] = 0;
    r[6] = 255;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
    XCP_CHECK_EQ_INT(s.right_trigger, 255);
    XCP_CHECK_EQ_HEX(s.buttons, XCP_BTN_R2);

    /* At the threshold the digital bit is still off. */
    r[5] = 30;
    r[6] = 30;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 30);
    XCP_CHECK_EQ_INT(s.right_trigger, 30);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
}

XCP_TEST(hid_generic_rejects_a_keyboard)
{
    xcp_hid_map m;

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_keyboard_desc,
                                              sizeof(g_keyboard_desc),
                                              0x1234, 0x5678, &m), 0);
}

XCP_TEST(hid_generic_rejects_malformed_descriptors)
{
    xcp_hid_map m;
    static const uint8_t truncated_item[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
        0x26, 0xFF, /* two-byte item missing its second byte */
    };
    static const uint8_t zero_report_size[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x00, 0x95, 0x04, 0x81, 0x02,
        0xC0,
    };
    static const uint8_t stray_end_collection[] = { 0xC0 };
    static const uint8_t truncated_long_item[] = { 0xFE, 0x05 };
    static const uint8_t unbalanced_pop[] = { 0xB4 };

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_gamepad_desc,
                                              sizeof(g_gamepad_desc) - 1,
                                              0x1234, 0x5678, &m), 0);
    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(truncated_item,
                                              sizeof(truncated_item),
                                              0x1234, 0x5678, &m), 0);
    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(zero_report_size,
                                              sizeof(zero_report_size),
                                              0x1234, 0x5678, &m), 0);
    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(stray_end_collection,
                                              sizeof(stray_end_collection),
                                              0x1234, 0x5678, &m), 0);
    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(truncated_long_item,
                                              sizeof(truncated_long_item),
                                              0x1234, 0x5678, &m), 0);
    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(unbalanced_pop,
                                              sizeof(unbalanced_pop),
                                              0x1234, 0x5678, &m), 0);
}

XCP_TEST(hid_generic_rejects_truncated_reports)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[7];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_gamepad_desc,
                                              sizeof(g_gamepad_desc),
                                              0x1234, 0x5678, &m), 1);
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    s.buttons = XCP_BTN_CROSS;
    s.left_x = 42;
    s.left_y = 43;
    memset(r, 0, sizeof(r));
    r[0] = 0x02; /* a valid button */
    r[2] = 0xFF; /* X */
    r[3] = 0x00; /* Y */
    r[4] = 0x00; /* Z */
    r[5] = 0xFF; /* Rz */
    r[6] = 0x02; /* hat */

    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 255);
    XCP_CHECK(s.buttons & XCP_BTN_CROSS);

    /* One byte short: reject the report and keep every previous value. */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r) - 1, &s), 0);
    XCP_CHECK_EQ_INT(s.left_x, 255);
    XCP_CHECK_EQ_INT(s.left_y, 0);
    XCP_CHECK(s.buttons & XCP_BTN_CROSS);

    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, 0, &s), 0);
}

XCP_TEST(interface_classification_rules)
{
    /* Known ids keep their profile regardless of interface class. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x045E, 0x028E,
                                                       0xFF, 0x5D, 0x01, 1),
                     XCP_CTRL_XBOX360);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x057E, 0x2009,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_SWITCH_PRO);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x054C, 0x0CE6,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_DUALSENSE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x054C, 0x0DF2,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_DUALSENSE);

    /* Class-only signatures are for the pads whose ids we cannot list. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x1234, 0x0001,
                                                       0xFF, 0x5D, 0x01, 1),
                     XCP_CTRL_XBOX360);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x1234, 0x0001,
                                                       0xFF, 0x47, 0xD0, 1),
                     XCP_CTRL_XBOXONE);

    /* Unknown generic HID is only a candidate, and needs an interrupt IN. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x1234, 0x0001,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_HID_GENERIC);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x1234, 0x0001,
                                                       0x03, 0x00, 0x00, 0),
                     XCP_CTRL_NONE);
    /* Boot keyboard/mouse subclasses are not gamepads. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x1234, 0x0001,
                                                       0x03, 0x01, 0x01, 1),
                     XCP_CTRL_NONE);
    /* Sony's unknown pads stay untouched rather than becoming Generic HID. */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x054C, 0x09CC,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_NONE);
}

XCP_TEST(gamesir_receiver_interfaces)
{
    /*
     * GameSir Nova Lite 2.4 GHz receiver. PID 1040 is the XInput group and
     * speaks the Xbox 360 wire format; PID 1041 is a small vendor HID status
     * stream (the captured 9-byte reports look like battery/charging state),
     * not a gamepad. The latter must stay a descriptor-gated candidate so the
     * generic parser, not its id, decides whether it is usable.
     */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x3537, 0x1040,
                                                       0xFF, 0x5D, 0x01, 1),
                     XCP_CTRL_XBOX360);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x3537, 0x1041,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_HID_GENERIC);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x3537, 0x1041,
                                                       0x03, 0x00, 0x00, 0),
                     XCP_CTRL_NONE);
}

XCP_TEST(third_party_modes_reuse_existing_profiles)
{
    /*
     * Vendor ids are deliberately not in the id table until someone captures
     * the actual reports. What is safe today is the wire signature:
     *   - 8BitDo Xbox mode spoofs Microsoft 045E:028E (Xbox 360 wire).
     *   - 8BitDo Switch mode spoofs Nintendo 057E:2009 (Switch Pro 0x30).
     *   - 8BitDo D-Input stays vendor HID and must pass the descriptor gate.
     *   - GameSir XInput/GIP modes use the standard FF/5D/01 or FF/47/D0.
     *   - A DS4-mode clone (054C:05C4) is left to the console's own driver.
     */
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x045E, 0x028E,
                                                       0xFF, 0x5D, 0x01, 1),
                     XCP_CTRL_XBOX360);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x057E, 0x2009,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_SWITCH_PRO);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x2DC8, 0x6001,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_HID_GENERIC);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x2DC8, 0x6101,
                                                       0x03, 0x01, 0x01, 1),
                     XCP_CTRL_NONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x3537, 0x1023,
                                                       0xFF, 0x5D, 0x01, 1),
                     XCP_CTRL_XBOX360);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x3537, 0x1001,
                                                       0xFF, 0x47, 0xD0, 1),
                     XCP_CTRL_XBOXONE);
    XCP_CHECK_EQ_INT(xcp_controller_type_for_interface(0x054C, 0x05C4,
                                                       0x03, 0x00, 0x00, 1),
                     XCP_CTRL_NONE);
}

static int preference_enumerate(void *ctx, xcp_usb_device_info *out, int max)
{
    (void)ctx;
    if (max < 2) return 0;

    memset(out, 0, sizeof(*out) * 2);
    out[0].vid = 0x1234;
    out[0].pid = 0x0001;
    out[0].type = XCP_CTRL_HID_GENERIC;
    out[1].vid = 0x045E;
    out[1].pid = 0x02EA;
    out[1].type = XCP_CTRL_XBOXONE;
    return 2;
}

static xcp_usb_backend g_preference_backend = {
    .name = "preference",
    .enumerate = preference_enumerate,
};

XCP_TEST(usb_find_prefers_a_named_profile_over_generic_hid)
{
    xcp_usb_device_info found;

    XCP_CHECK_EQ_INT(xcp_usb_find(&g_preference_backend, NULL,
                                  XCP_CTRL_NONE, &found), 1);
    XCP_CHECK_EQ_INT(found.type, XCP_CTRL_XBOXONE);

    XCP_CHECK_EQ_INT(xcp_usb_find(&g_preference_backend, NULL,
                                  XCP_CTRL_HID_GENERIC, &found), 1);
    XCP_CHECK_EQ_INT(found.type, XCP_CTRL_HID_GENERIC);
}

/*
 * Descriptors that declare Physical Minimum/Maximum (global tags 3 and 4).
 * Many real pads do; the parser must not let them overwrite the Logical range.
 */

/* Hat with Logical 1..8 and Physical 0..315 degrees. */
static const uint8_t g_hat_physical_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x09, 0x39,
    0x15, 0x01, 0x25, 0x08,             /* Logical 1..8              */
    0x35, 0x00, 0x46, 0x3B, 0x01,       /* Physical 0..315           */
    0x75, 0x04, 0x95, 0x01, 0x81, 0x42, /* Data,Var,Abs,Null state   */
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0xC0,
};

/* Signed 8-bit X/Y: Logical -127..127 and Physical -127..127. */
static const uint8_t g_signed_physical_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x81, 0x25, 0x7F,             /* Logical -127..127         */
    0x35, 0x81, 0x45, 0x7F,             /* Physical -127..127        */
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0,
};

/*
 * Android-mode gamepad: X/Y left stick, Z/Rz right stick, and the triggers as
 * Simulation Controls (Brake = LT, Accelerator = RT).
 */
static const uint8_t g_brake_accel_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
    0x05, 0x02, 0x09, 0xC5, 0x09, 0xC4, /* Brake, Accelerator        */
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0,
};

XCP_TEST(hid_generic_physical_range_does_not_replace_logical)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[4];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_hat_physical_desc,
                                              sizeof(g_hat_physical_desc),
                                              0x1234, 0x5678, &m), 1);
    XCP_CHECK_EQ_INT(m.hat.logical_min, 1);
    XCP_CHECK_EQ_INT(m.hat.logical_max, 8);

    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    memset(r, 0, sizeof(r));
    r[1] = 0x80;
    r[2] = 0x80;
    r[3] = 0x01; /* Logical 1 = up */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK(s.buttons & XCP_BTN_UP);
    XCP_CHECK_FALSE(s.buttons & XCP_BTN_RIGHT);

    r[3] = 0x03; /* Logical 3 = right */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK(s.buttons & XCP_BTN_RIGHT);
    XCP_CHECK_FALSE(s.buttons & XCP_BTN_UP);
}

XCP_TEST(hid_generic_signed_physical_minimum)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[3];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_signed_physical_desc,
                                              sizeof(g_signed_physical_desc),
                                              0x1234, 0x5678, &m), 1);
    XCP_CHECK_EQ_INT(m.axis[XCP_HID_AXIS_X].logical_min, -127);
    XCP_CHECK_EQ_INT(m.axis[XCP_HID_AXIS_X].logical_max, 127);

    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    memset(r, 0, sizeof(r));
    r[1] = 0x81; /* -127 */
    r[2] = 0x7F; /* +127 */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_x, 0);
    XCP_CHECK_EQ_INT(s.left_y, 255);
}

XCP_TEST(hid_generic_brake_and_accelerator_are_triggers)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[7];

    XCP_CHECK_EQ_INT(xcp_hid_parse_descriptor(g_brake_accel_desc,
                                              sizeof(g_brake_accel_desc),
                                              0x1234, 0x5678, &m), 1);
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);

    memset(r, 0, sizeof(r));
    r[1] = 0x80; r[2] = 0x80; r[3] = 0x80; r[4] = 0x80;
    r[5] = 0xFF; /* Brake = LT fully pressed        */
    r[6] = 0x00; /* Accelerator = RT released       */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 0);
    XCP_CHECK(s.buttons & XCP_BTN_L2);
    XCP_CHECK_FALSE(s.buttons & XCP_BTN_R2);

    r[5] = 0x00;
    r[6] = 0xFF;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
    XCP_CHECK_EQ_INT(s.right_trigger, 255);
    XCP_CHECK(s.buttons & XCP_BTN_R2);
}

XCP_TEST(hid_generic_digital_triggers_set_analog_value)
{
    xcp_hid_map   m;
    xcp_pad_state s;
    uint8_t       r[7];

    /* g_gamepad_desc is DirectInput order: button 7 = L2, button 8 = R2. */
    xcp_hid_parse_descriptor(g_gamepad_desc, sizeof(g_gamepad_desc),
                             0x1234, 0x5678, &m);
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);

    memset(r, 0, sizeof(r));
    r[0] = 0x40; /* button 7 */
    r[2] = r[3] = r[4] = r[5] = 0x80;
    r[6] = 0x08; /* hat centred */
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK(s.buttons & XCP_BTN_L2);
    XCP_CHECK_EQ_INT(s.left_trigger, 255);
    XCP_CHECK_EQ_INT(s.right_trigger, 0);

    r[0] = 0x00;
    XCP_CHECK_EQ_INT(xcp_hid_decode(&m, r, sizeof(r), &s), 1);
    XCP_CHECK_FALSE(s.buttons & XCP_BTN_L2);
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
}

void xcp_register_hid_generic_tests(void)
{
    XCP_RUN(hid_generic_physical_range_does_not_replace_logical);
    XCP_RUN(hid_generic_signed_physical_minimum);
    XCP_RUN(hid_generic_brake_and_accelerator_are_triggers);
    XCP_RUN(hid_generic_digital_triggers_set_analog_value);
    XCP_RUN(hid_generic_parses_buttons_axes_and_hat);
    XCP_RUN(hid_generic_decodes_a_report);
    XCP_RUN(hid_generic_uses_the_report_id);
    XCP_RUN(hid_generic_rejects_a_mouse);
    XCP_RUN(hid_generic_decodes_signed_axes);
    XCP_RUN(hid_generic_report_ids_split_fields);
    XCP_RUN(hid_generic_nonstandard_hat_range);
    XCP_RUN(hid_generic_triggers_release);
    XCP_RUN(hid_generic_rejects_a_keyboard);
    XCP_RUN(hid_generic_rejects_malformed_descriptors);
    XCP_RUN(hid_generic_rejects_truncated_reports);
    XCP_RUN(interface_classification_rules);
    XCP_RUN(gamesir_receiver_interfaces);
    XCP_RUN(third_party_modes_reuse_existing_profiles);
    XCP_RUN(usb_find_prefers_a_named_profile_over_generic_hid);
}
