/*
 * tests/test_router.c - phase PC-3.
 *
 * The scenario the plan asks for: a fake PS4 with two logged-in users where the
 * DS4 belongs to user 1 and nothing else is attached, plus the edge cases
 * (controller unplugged, reopened pad, late drop-in, failed open, handle 0).
 */
#include "framework.h"

#include "xcp/pad_mock.h"
#include "xcp/router.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Fixtures
 * ------------------------------------------------------------------------ */

static xcp_pad_state state_with(uint32_t buttons)
{
    xcp_pad_state s;
    memset(&s, 0, sizeof(s));
    xcp_pad_state_centre_sticks(&s);
    s.buttons   = buttons;
    s.connected = 1;
    return s;
}

/* The user's console: DS4 as user 1, a second user also logged in. */
static void setup_console(xcp_mock_host *m, xcp_router *r, xcp_router_config *cfg)
{
    xcp_mock_host_init(m);
    xcp_mock_host_login(m, 1);
    xcp_mock_host_login(m, 2);
    xcp_mock_host_set_foreground(m, 1);

    xcp_router_config_default(cfg);
    xcp_router_init(r, &m->api, cfg);
}

/* An Xbox pad that is connected and actively pressing CROSS. */
static void attach_active_xbox(xcp_router *r)
{
    xcp_pad_state xb = state_with(XCP_BTN_CROSS);
    xcp_router_set_xbox(r, &xb, 1);
}

/* ---------------------------------------------------------------------------
 * The happy path
 * ------------------------------------------------------------------------ */

XCP_TEST(player1_gets_the_xbox_pad_and_player2_is_untouched)
{
    xcp_mock_host      m;
    xcp_router         r;
    xcp_router_config  cfg;
    xcp_pad_data       out;
    int32_t            h1, h2;
    char               line[256];

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);

    XCP_CHECK_EQ_INT(r.foreground_user, 1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    h2 = xcp_router_pad_open(&r, 2, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK(h1 >= 0);
    XCP_CHECK(h2 >= 0);
    XCP_CHECK(h1 != h2);

    /* The first standard pad opened is player 1. */
    XCP_CHECK_EQ_INT(r.p1_handle, h1);
    XCP_CHECK_EQ_INT(r.p1_user_id, 1);
    XCP_CHECK(xcp_router_handle_is_p1(&r, h1));
    XCP_CHECK_FALSE(xcp_router_handle_is_p1(&r, h2));

    /* Both users are "holding a DS4" pressing SQUARE. */
    {
        xcp_pad_state ds4 = state_with(XCP_BTN_SQUARE);
        XCP_CHECK_EQ_INT(xcp_mock_host_set_user_state(&m, 1, &ds4), 0);
        XCP_CHECK_EQ_INT(xcp_mock_host_set_user_state(&m, 2, &ds4), 0);
    }

    /* Player 1 sees the Xbox pad (active -> it owns the slot). */
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_CROSS);
    XCP_CHECK_EQ_INT(out.connected, 1);

    /* Player 2 still sees its own DS4, untouched. */
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h2, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_SQUARE);

    xcp_router_describe(&r, line, sizeof(line));
    XCP_CHECK(strstr(line, "xbox=up") != NULL);
    XCP_CHECK(strstr(line, "injected=1") != NULL);
}

XCP_TEST(idle_xbox_leaves_the_ds4_in_charge)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1;
    xcp_pad_state     idle;
    xcp_pad_state     ds4;

    setup_console(&m, &r, &cfg);

    /* Xbox present but completely untouched. */
    memset(&idle, 0, sizeof(idle));
    xcp_pad_state_centre_sticks(&idle);
    idle.connected = 1;
    xcp_router_set_xbox(&r, &idle, 1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    ds4 = state_with(XCP_BTN_SQUARE);
    xcp_mock_host_set_user_state(&m, 1, &ds4);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_SQUARE);
    XCP_CHECK_EQ_INT(r.reads_injected, 0);
}

XCP_TEST(stick_drift_does_not_count_as_activity)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1;
    xcp_pad_state     drift;
    xcp_pad_state     ds4;

    setup_console(&m, &r, &cfg);

    /* 20 units off centre; default activity deadzone is 24. */
    memset(&drift, 0, sizeof(drift));
    xcp_pad_state_centre_sticks(&drift);
    drift.connected = 1;
    drift.left_x = 128 + 20;
    xcp_router_set_xbox(&r, &drift, 1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    ds4 = state_with(XCP_BTN_SQUARE);
    xcp_mock_host_set_user_state(&m, 1, &ds4);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_SQUARE);

    /* Now push it past the deadzone and the Xbox takes over. */
    drift.left_x = 128 + 60;
    xcp_router_set_xbox(&r, &drift, 1);
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_INT(out.left_x, 188);
    XCP_CHECK_EQ_HEX(out.buttons, 0);
}

XCP_TEST(trigger_counts_as_activity)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1;
    xcp_pad_state     xb;

    setup_console(&m, &r, &cfg);

    memset(&xb, 0, sizeof(xb));
    xcp_pad_state_centre_sticks(&xb);
    xb.connected     = 1;
    xb.right_trigger = 200;
    xcp_router_set_xbox(&r, &xb, 1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_INT(out.right_trigger, 200);
}

/* ---------------------------------------------------------------------------
 * Policies
 * ------------------------------------------------------------------------ */

XCP_TEST(merge_policy_combines_both_pads)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1;
    xcp_pad_state     ds4;
    xcp_pad_state     xb;

    setup_console(&m, &r, &cfg);
    r.cfg.policy = XCP_MERGE;

    /* Even an idle Xbox contributes its ORed buttons under MERGE. */
    memset(&xb, 0, sizeof(xb));
    xcp_pad_state_centre_sticks(&xb);
    xb.connected = 1;
    xb.buttons   = XCP_BTN_CROSS;
    xb.left_x    = 10;
    xcp_router_set_xbox(&r, &xb, 1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    ds4 = state_with(XCP_BTN_SQUARE);
    ds4.left_x = 131; /* a jittery resting DS4 axis */
    xcp_mock_host_set_user_state(&m, 1, &ds4);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_CROSS | XCP_BTN_SQUARE);
    XCP_CHECK_EQ_INT(out.left_x, 10); /* the deflected axis wins */
}

XCP_TEST(xbox_only_policy_ignores_ds4_entirely)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1;
    xcp_pad_state     ds4;
    xcp_pad_state     idle;

    setup_console(&m, &r, &cfg);
    r.cfg.policy = XCP_XBOX_ONLY;

    memset(&idle, 0, sizeof(idle));
    xcp_pad_state_centre_sticks(&idle);
    idle.connected = 1;
    xcp_router_set_xbox(&r, &idle, 1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    ds4 = state_with(XCP_BTN_SQUARE | XCP_BTN_R1);
    xcp_mock_host_set_user_state(&m, 1, &ds4);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, 0);
    XCP_CHECK_EQ_INT(out.left_x, 128);
    XCP_CHECK_EQ_INT(out.connected, 1);
}

/* ---------------------------------------------------------------------------
 * Unplug / replug
 * ------------------------------------------------------------------------ */

XCP_TEST(unplugged_xbox_hands_the_slot_back_to_the_ds4)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1;
    xcp_pad_state     ds4;
    xcp_pad_state     xb;

    setup_console(&m, &r, &cfg);
    r.cfg.policy = XCP_XBOX_ONLY; /* the harshest case */
    attach_active_xbox(&r);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    ds4 = state_with(XCP_BTN_SQUARE);
    xcp_mock_host_set_user_state(&m, 1, &ds4);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_CROSS);

    /* Unplug. */
    xcp_router_set_xbox(&r, NULL, 0);
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_SQUARE);

    /* Replug: same handle, same slot, no reopen needed. */
    xb = state_with(XCP_BTN_CROSS);
    xcp_router_set_xbox(&r, &xb, 1);
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_CROSS);
}

XCP_TEST(disconnect_without_new_state_clears_the_pad)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1;
    xcp_pad_state     ds4;

    setup_console(&m, &r, &cfg);
    r.cfg.policy = XCP_XBOX_ONLY;
    attach_active_xbox(&r);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    ds4 = state_with(XCP_BTN_SQUARE);
    xcp_mock_host_set_user_state(&m, 1, &ds4);

    /* NULL state + not present must not leave a stale CROSS latched. */
    xcp_router_set_xbox(&r, NULL, 0);
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_SQUARE);
}

/* ---------------------------------------------------------------------------
 * Open / close / drop-in edge cases
 * ------------------------------------------------------------------------ */

XCP_TEST(reopened_pad_reclaims_the_slot)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1, h1b;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK_EQ_INT(r.p1_handle, h1);

    XCP_CHECK_EQ_INT(xcp_router_pad_close(&r, h1), 0);
    XCP_CHECK_EQ_INT(r.p1_handle, -1);
    XCP_CHECK_EQ_INT(r.p1_user_id, -1);
    XCP_CHECK_FALSE(xcp_router_handle_is_p1(&r, h1));

    /* A drop-in game closing and reopening between rounds must end up wired. */
    h1b = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK(h1b >= 0);
    XCP_CHECK_EQ_INT(r.p1_handle, h1b);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1b, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_CROSS);
}

XCP_TEST(late_second_player_drop_in_is_not_hijacked)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1, h2;
    xcp_pad_state     ds4;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    ds4 = state_with(XCP_BTN_OPTIONS);
    xcp_mock_host_set_user_state(&m, 1, &ds4);

    /* Player 2 joins much later: it must keep its own DS4. */
    h2 = xcp_router_pad_open(&r, 2, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK(h2 >= 0);
    XCP_CHECK_EQ_INT(r.p1_handle, h1);

    xcp_mock_host_set_user_state(&m, 2, &ds4);
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h2, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_OPTIONS);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_CROSS);
}

XCP_TEST(second_handle_for_the_same_user_is_not_p1)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1, h1b;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    /* Same user, different index: a game opening two pads for one player. */
    h1b = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 1, NULL);
    XCP_CHECK(h1b >= 0);
    XCP_CHECK_EQ_INT(r.p1_handle, h1);

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1b, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, 0); /* the DS4's own neutral state */
}

XCP_TEST(unknown_handle_reads_pass_through_untouched)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           ret;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);

    memset(&out, 0xCD, sizeof(out));
    ret = xcp_router_pad_read_state(&r, 4242, &out);
    XCP_CHECK_EQ_INT(ret, -1);          /* mock has no such handle */
    XCP_CHECK_EQ_INT(out.buttons, 0xCDCDCDCDu); /* we never wrote to it */
    XCP_CHECK_EQ_INT(r.reads_injected, 0);
}

XCP_TEST(failed_read_is_never_injected_into)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1, ret;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);
    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    memset(&out, 0xEF, sizeof(out));
    m.fail_next_read = 1;
    ret = xcp_router_pad_read_state(&r, h1, &out);

    XCP_CHECK_EQ_INT(ret, -1);
    XCP_CHECK_EQ_INT(out.buttons, 0xEFEFEFEFu);
    XCP_CHECK_EQ_INT(r.reads_injected, 0);

    /* A host that reports "not ready" is also left alone. */
    m.read_state_returns_minus_one = 1;
    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h1, &out), -1);
    XCP_CHECK_EQ_INT(r.reads_injected, 0);
}

XCP_TEST(failed_open_claims_nothing_and_can_be_retried)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int32_t           h1;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);

    m.fail_next_open = 1;
    XCP_CHECK_EQ_INT(xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL), -1);
    XCP_CHECK_EQ_INT(r.p1_handle, -1);
    XCP_CHECK_EQ_INT(m.open_calls, 1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK(h1 >= 0);
    XCP_CHECK_EQ_INT(r.p1_handle, h1);
}

XCP_TEST(handle_zero_is_a_valid_handle)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);
    r.cfg.policy = XCP_XBOX_ONLY;

    m.open_returns_zero = 1;
    h = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    XCP_CHECK_EQ_INT(h, 0);
    XCP_CHECK_EQ_INT(r.p1_handle, 0);   /* -1 must mean "none", not 0 */
    XCP_CHECK(xcp_router_handle_is_p1(&r, 0));

    XCP_CHECK_EQ_INT(xcp_router_pad_read_state(&r, h, &out), 0);
    XCP_CHECK_EQ_HEX(out.buttons, XCP_BTN_CROSS);
}

XCP_TEST(multi_read_fills_every_slot)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    xcp_pad_data      out[3];
    int32_t           h1;
    int               i;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);
    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    memset(out, 0, sizeof(out));
    XCP_CHECK_EQ_INT(xcp_router_pad_read(&r, h1, out, 3), 3);
    for (i = 0; i < 3; i++) {
        XCP_CHECK_EQ_HEX(out[i].buttons, XCP_BTN_CROSS);
        XCP_CHECK_EQ_INT(out[i].connected, 1);
    }
    XCP_CHECK_EQ_INT(xcp_router_pad_read(&r, h1, out, 0), -1);
}

/* ---------------------------------------------------------------------------
 * Target selection
 * ------------------------------------------------------------------------ */

XCP_TEST(foreground_user_target_waits_for_the_right_user)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int32_t           h2, h1;

    setup_console(&m, &r, &cfg);
    cfg.target = XCP_P1_FOREGROUND_USER;
    xcp_router_init(&r, &m.api, &cfg);
    attach_active_xbox(&r);

    /* Player 2's pad opens first and must NOT take the slot. */
    h2 = xcp_router_pad_open(&r, 2, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK(h2 >= 0);
    XCP_CHECK_EQ_INT(r.p1_handle, -1);

    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK_EQ_INT(r.p1_handle, h1);
    XCP_CHECK_EQ_INT(r.p1_user_id, 1);
}

XCP_TEST(any_handle_target_claims_the_first_pad)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int32_t           h2;

    setup_console(&m, &r, &cfg);
    cfg.target = XCP_P1_ANY_HANDLE;
    xcp_router_init(&r, &m.api, &cfg);
    attach_active_xbox(&r);

    h2 = xcp_router_pad_open(&r, 2, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK_EQ_INT(r.p1_handle, h2);
}

XCP_TEST(special_port_type_does_not_steal_the_slot)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int32_t           h_special, h_std;

    setup_console(&m, &r, &cfg);
    attach_active_xbox(&r);

    /* A wheel/fightstick opening first must not become player 1. */
    h_special = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_SPECIAL, 0, NULL);
    XCP_CHECK(h_special >= 0);
    XCP_CHECK_EQ_INT(r.p1_handle, -1);

    h_std = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK_EQ_INT(r.p1_handle, h_std);

    /* ...unless the operator opts out of the filter. */
    xcp_router_init(&r, &m.api, &cfg);
    r.cfg.require_standard_port_type = 0;
    xcp_mock_host_close_all(&m);
    h_special = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_SPECIAL, 0, NULL);
    XCP_CHECK_EQ_INT(r.p1_handle, h_special);
}

XCP_TEST(forced_user_id_overrides_the_host)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;

    setup_console(&m, &r, &cfg);
    cfg.target         = XCP_P1_FOREGROUND_USER;
    cfg.forced_user_id = 2;
    xcp_router_init(&r, &m.api, &cfg);

    XCP_CHECK_EQ_INT(r.foreground_user, 2);

    xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK_EQ_INT(r.p1_handle, -1);
    xcp_router_pad_open(&r, 2, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK(r.p1_handle >= 0);
}

XCP_TEST(missing_foreground_user_does_not_break_first_open)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int32_t           h1;

    xcp_mock_host_init(&m);            /* nobody logged in at all */
    xcp_router_config_default(&cfg);
    xcp_router_init(&r, &m.api, &cfg);
    attach_active_xbox(&r);

    XCP_CHECK_EQ_INT(r.foreground_user, -1);
    h1 = xcp_router_pad_open(&r, 0, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    XCP_CHECK(h1 >= 0);
    XCP_CHECK_EQ_INT(r.p1_handle, h1);
}

/* ---------------------------------------------------------------------------
 * Controller information & bookkeeping
 * ------------------------------------------------------------------------ */

XCP_TEST(controller_information_is_not_faked)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int32_t           h1;
    unsigned char     info[64];

    setup_console(&m, &r, &cfg);
    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    memset(info, 0, sizeof(info));
    XCP_CHECK_EQ_INT(xcp_router_get_controller_information(&r, h1, info), 0);
    XCP_CHECK_EQ_INT(m.info_calls, 1);

    /* The handle is real, so an unknown handle is a real failure. */
    XCP_CHECK_EQ_INT(xcp_router_get_controller_information(&r, 999, info), -1);
}

XCP_TEST(handle_table_is_bounded_and_never_corrupts)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int               i;

    setup_console(&m, &r, &cfg);

    /* Open far more handles than the table can hold. */
    for (i = 0; i < 40; i++) {
        xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, i, NULL);
    }
    XCP_CHECK(r.handle_count <= XCP_ROUTER_MAX_HANDLES);
    XCP_CHECK_EQ_INT(r.p1_handle, 1); /* the very first one still owns the slot */

    /* Closing an unknown handle must not touch the table. */
    XCP_CHECK_EQ_INT(xcp_router_pad_close(&r, 12345), -1);
    XCP_CHECK_EQ_INT(r.p1_handle, 1);
}

XCP_TEST(close_forgets_only_that_handle)
{
    xcp_mock_host     m;
    xcp_router        r;
    xcp_router_config cfg;
    int32_t           h1, h2;

    setup_console(&m, &r, &cfg);
    h1 = xcp_router_pad_open(&r, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    h2 = xcp_router_pad_open(&r, 2, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    XCP_CHECK_EQ_INT(r.handle_count, 2);
    xcp_router_pad_close(&r, h2);
    XCP_CHECK_EQ_INT(r.handle_count, 1);
    XCP_CHECK(xcp_router_find_handle(&r, h1) != NULL);
    XCP_CHECK(xcp_router_find_handle(&r, h2) == NULL);
    XCP_CHECK_EQ_INT(r.p1_handle, h1);
}

void xcp_register_router_tests(void)
{
    XCP_RUN(player1_gets_the_xbox_pad_and_player2_is_untouched);
    XCP_RUN(idle_xbox_leaves_the_ds4_in_charge);
    XCP_RUN(stick_drift_does_not_count_as_activity);
    XCP_RUN(trigger_counts_as_activity);

    XCP_RUN(merge_policy_combines_both_pads);
    XCP_RUN(xbox_only_policy_ignores_ds4_entirely);

    XCP_RUN(unplugged_xbox_hands_the_slot_back_to_the_ds4);
    XCP_RUN(disconnect_without_new_state_clears_the_pad);

    XCP_RUN(reopened_pad_reclaims_the_slot);
    XCP_RUN(late_second_player_drop_in_is_not_hijacked);
    XCP_RUN(second_handle_for_the_same_user_is_not_p1);
    XCP_RUN(unknown_handle_reads_pass_through_untouched);
    XCP_RUN(failed_read_is_never_injected_into);
    XCP_RUN(failed_open_claims_nothing_and_can_be_retried);
    XCP_RUN(handle_zero_is_a_valid_handle);
    XCP_RUN(multi_read_fills_every_slot);

    XCP_RUN(foreground_user_target_waits_for_the_right_user);
    XCP_RUN(any_handle_target_claims_the_first_pad);
    XCP_RUN(special_port_type_does_not_steal_the_slot);
    XCP_RUN(forced_user_id_overrides_the_host);
    XCP_RUN(missing_foreground_user_does_not_break_first_open);

    XCP_RUN(controller_information_is_not_faked);
    XCP_RUN(handle_table_is_bounded_and_never_corrupts);
    XCP_RUN(close_forgets_only_that_handle);
}
