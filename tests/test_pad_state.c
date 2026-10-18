/*
 * tests/test_pad_state.c - pad-state helpers, especially the DS4/Xbox merge.
 */
#include "framework.h"

#include "xcp/pad_state.h"

#include <string.h>

static xcp_pad_state centred_state(void)
{
    xcp_pad_state s;
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    s.connected = 1;
    return s;
}

XCP_TEST(centre_sticks_puts_everything_neutral)
{
    xcp_pad_state s;
    memset(&s, 0xAB, sizeof(s));

    xcp_pad_state_centre_sticks(&s);

    XCP_CHECK_EQ_INT(s.left_x, 128);
    XCP_CHECK_EQ_INT(s.left_y, 128);
    XCP_CHECK_EQ_INT(s.right_x, 128);
    XCP_CHECK_EQ_INT(s.right_y, 128);
    XCP_CHECK_EQ_INT(s.left_trigger, 0);
    XCP_CHECK_EQ_INT(s.right_trigger, 0);
    XCP_CHECK_EQ_HEX(s.buttons, 0);
}

XCP_TEST(button_count_and_names)
{
    XCP_CHECK_EQ_INT(xcp_pad_state_button_count(0), 0);
    XCP_CHECK_EQ_INT(xcp_pad_state_button_count(XCP_BTN_CROSS), 1);
    XCP_CHECK_EQ_INT(xcp_pad_state_button_count(XCP_BTN_CROSS | XCP_BTN_L1), 2);
    XCP_CHECK_EQ_INT(xcp_pad_state_button_count(0xFFFFFFFFu), 32);

    XCP_CHECK(strcmp(xcp_button_name(XCP_BTN_CROSS), "CROSS") == 0);
    XCP_CHECK(strcmp(xcp_button_name(XCP_BTN_PS), "PS") == 0);
    XCP_CHECK(strcmp(xcp_button_name(0x40000000u), "?") == 0);
}

XCP_TEST(format_buttons_is_readable)
{
    char line[128];

    xcp_pad_state_format_buttons(0, line, sizeof(line));
    XCP_CHECK(strcmp(line, "-") == 0);

    xcp_pad_state_format_buttons(XCP_BTN_CROSS | XCP_BTN_L1, line, sizeof(line));
    XCP_CHECK(strstr(line, "CROSS") != NULL);
    XCP_CHECK(strstr(line, "L1") != NULL);
    /* never overflow a small buffer */
    xcp_pad_state_format_buttons(0xFFFFFFFFu, line, 16);
    XCP_CHECK(strlen(line) < 16);
}

XCP_TEST(merge_or_of_buttons_and_max_of_triggers)
{
    xcp_pad_state ds4 = centred_state();
    xcp_pad_state xb  = centred_state();

    ds4.buttons = XCP_BTN_SQUARE;
    ds4.left_trigger = 40;
    xb.buttons = XCP_BTN_CROSS;
    xb.right_trigger = 200;

    xcp_pad_state_merge(&ds4, &xb);

    XCP_CHECK_EQ_HEX(ds4.buttons, XCP_BTN_SQUARE | XCP_BTN_CROSS);
    XCP_CHECK_EQ_INT(ds4.left_trigger, 40);
    XCP_CHECK_EQ_INT(ds4.right_trigger, 200);
}

XCP_TEST(merge_lets_the_deviated_axis_win)
{
    xcp_pad_state ds4 = centred_state();
    xcp_pad_state xb  = centred_state();

    /* The DS4 axis jitters a little; the Xbox axis is fully deflected. */
    ds4.left_x = 131;
    xb.left_x  = 20;
    /* Mirror case: the DS4 axis is the deflected one. */
    ds4.right_x = 250;
    xb.right_x  = 126;

    xcp_pad_state_merge(&ds4, &xb);

    XCP_CHECK_EQ_INT(ds4.left_x, 20);
    XCP_CHECK_EQ_INT(ds4.right_x, 250);
}

XCP_TEST(merge_adopts_an_unconnected_destination)
{
    xcp_pad_state dst;
    xcp_pad_state src = centred_state();

    memset(&dst, 0, sizeof(dst));
    dst.connected = 0;
    dst.left_x = 12;

    src.buttons = XCP_BTN_CROSS;
    src.left_y  = 30;

    xcp_pad_state_merge(&dst, &src);

    XCP_CHECK(xcp_pad_state_equal(&dst, &src));
}

XCP_TEST(merge_ignores_an_absent_source)
{
    xcp_pad_state dst = centred_state();
    xcp_pad_state src;

    memset(&src, 0, sizeof(src));
    src.connected = 0;
    src.buttons = XCP_BTN_CROSS; /* garbage in an absent pad */
    src.left_x = 4;

    dst.buttons = XCP_BTN_SQUARE;
    xcp_pad_state_merge(&dst, &src);

    XCP_CHECK_EQ_HEX(dst.buttons, XCP_BTN_SQUARE);
    XCP_CHECK_EQ_INT(dst.left_x, 128);
}

XCP_TEST(merge_tolerates_null)
{
    xcp_pad_state s = centred_state();
    xcp_pad_state_merge(&s, NULL);
    xcp_pad_state_merge(NULL, &s);
    XCP_CHECK_EQ_INT(s.left_x, 128);
}

XCP_TEST(pad_state_equality)
{
    xcp_pad_state a = centred_state();
    xcp_pad_state b = centred_state();

    XCP_CHECK(xcp_pad_state_equal(&a, &b));
    b.buttons = XCP_BTN_CROSS;
    XCP_CHECK_FALSE(xcp_pad_state_equal(&a, &b));
}

void xcp_register_pad_state_tests(void)
{
    XCP_RUN(centre_sticks_puts_everything_neutral);
    XCP_RUN(button_count_and_names);
    XCP_RUN(format_buttons_is_readable);
    XCP_RUN(merge_or_of_buttons_and_max_of_triggers);
    XCP_RUN(merge_lets_the_deviated_axis_win);
    XCP_RUN(merge_adopts_an_unconnected_destination);
    XCP_RUN(merge_ignores_an_absent_source);
    XCP_RUN(merge_tolerates_null);
    XCP_RUN(pad_state_equality);
}
