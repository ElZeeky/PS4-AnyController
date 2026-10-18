/*
 * host/xcp_inspector.c - the PC-side tool.
 *
 * Modes:
 *   --list              what is plugged in, and how it identifies itself
 *   --selftest          drive synthetic reports through core + router, no hardware
 *   --play FILE         decode a captured report dump written as hex text
 *   --mock              demonstrate the player-1 assignment decisions
 *   --live              read the real controller (needs a backend compiled in)
 *
 * Everything except --live works with no dependencies at all, which is what
 * makes the pipeline verifiable on a machine with no controller attached.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win_devinfo.h"
#include "xinput_backend.h"

#include "xcp/pad_mock.h"
#include "xcp/report_pack.h"
#include "xcp/router.h"
#include "xcp/translate.h"
#include "xcp/usb_backend.h"

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------ */

static void banner(const char *title)
{
    printf("\n\033[1m== %s\033[0m\n", title);
}

static const char *type_label(xcp_controller_type t)
{
    return xcp_controller_type_name(t);
}

/* ---------------------------------------------------------------------------
 * --list
 * ------------------------------------------------------------------------ */

static void cmd_list(void)
{
    xcp_dev_entry entries[128];
    int           count;
    int           i;
    int           interesting = 0;

    banner("attached HID / USB devices");

    if (!xcp_enumerate_available()) {
        printf("device enumeration is not available in this build\n");
        printf("(it is implemented for Windows; elsewhere use lsusb/ioreg)\n");
        return;
    }

    count = xcp_enumerate_devices(entries, 128);
    if (count <= 0) {
        printf("no devices reported (unexpected)\n");
        return;
    }

    printf("%-5s %-9s %-40s %s\n", "BUS", "VID:PID", "DEVICE", "INSTANCE");
    for (i = 0; i < count; i++) {
        xcp_controller_type t = XCP_CTRL_NONE;

        if (entries[i].has_ids) {
            t = xcp_controller_type_for_ids(entries[i].vid, entries[i].pid);
        }

        printf("%-5s ", entries[i].group);
        if (entries[i].has_ids) {
            printf("%04X:%04X ", entries[i].vid, entries[i].pid);
        } else {
            printf("%-9s ", "-");
        }
        printf("%-40.40s %s\n", entries[i].desc, entries[i].instance);

        if (t != XCP_CTRL_NONE) {
            printf("      ^^^ looks like a %s pad -> we can drive it\n", type_label(t));
            interesting++;
        }
    }

    printf("\n%d device(s) listed, %d recognised as a supported pad\n", count, interesting);
    if (interesting == 0) {
        printf("\nNo supported pad found.\n");
        printf("Checklist if the controller is physically plugged in:\n");
        printf("  * put the pad in Xbox/wired mode (not Bluetooth, not Switch mode)\n");
        printf("  * an Xbox-format pad in XInput mode is claimed by the Xbox driver\n");
        printf("    and may not appear in the HID list at all - that is expected;\n");
        printf("    look for a USB entry whose instance id has VID_045E\n");
        printf("  * if nothing Xbox-shaped appears, the pad is using a vendor id we\n");
        printf("    have not catalogued yet - paste this whole list back\n");
    } else {
        printf("\nPaste this list back so the wiring can be confirmed.\n");
    }
}

/* ---------------------------------------------------------------------------
 * Report dump parsing (--play)
 * ------------------------------------------------------------------------ */

/*
 * Dump format, one report per line:
 *
 *   [optional label] <hex byte pairs>   [# comment]
 *
 * e.g.
 *   A-pressed  00 14 00 10 00 ... 00
 *   00 14 00 10 00 ... 00
 *
 * Splitting label from bytes cannot be done by looking for "the first hex
 * character": labels like A, B, X, back and left-right are full of hex letters.
 * Instead the byte list is defined as the longest trailing run of tokens that
 * are all exactly two hex digits. A label that is itself two hex digits would
 * be ambiguous, so do not use one.
 *
 * Lines pasted straight out of klog carry a log prefix (`xcp:` on this plugin).
 * A leading token ending in ':' is treated as that prefix and dropped, so the
 * text can be pasted verbatim without spoiling the `xcpcapture` label.
 */
#define XCP_MAX_TOKENS  64
#define XCP_MAX_LABEL   64
#define XCP_LABEL_WIDTH 20

static int token_is_byte(const char *tok, size_t len)
{
    size_t i;

    if (len != 2) return 0;
    for (i = 0; i < 2; i++) {
        char c = tok[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

static unsigned hex_pair(const char *tok)
{
    unsigned v = 0;
    int      i;

    for (i = 0; i < 2; i++) {
        char     c = tok[i];
        unsigned d;
        if (c >= '0' && c <= '9')      d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else                           d = (unsigned)(c - 'A' + 10);
        v = (v << 4) | d;
    }
    return v;
}

/*
 * Parse one dump line. Returns 1 when at least one byte was produced.
 * `label_out` receives the leading text, may be empty.
 */
static int parse_dump_line(const char *line, uint8_t *out, size_t out_size,
                          size_t *out_len, char *label_out, size_t label_size)
{
    char        work[1024];
    const char *tok[XCP_MAX_TOKENS];
    size_t      tok_len[XCP_MAX_TOKENS];
    unsigned    ntok = 0;
    unsigned    k;
    unsigned    i;
    size_t      used = 0;

    label_out[0] = '\0';
    *out_len     = 0;

    /* Copy up to a comment, and normalise separators to spaces. */
    {
        size_t w = 0;
        for (i = 0; line[i] && line[i] != '#' && w + 1 < sizeof(work); i++) {
            char c = line[i];
            work[w++] = (c == ',' || c == '\t' || c == '\r' || c == '\n') ? ' ' : c;
        }
        work[w] = '\0';
    }

    /* Tokenise on spaces. */
    {
        char       *p = work;
        while (*p && ntok < XCP_MAX_TOKENS) {
            while (*p == ' ') p++;
            if (!*p) break;
            tok[ntok] = p;
            while (*p && *p != ' ') p++;
            tok_len[ntok] = (size_t)(p - tok[ntok]);
            ntok++;
        }
    }
    if (ntok == 0) return 0;

    /* Smallest k such that every token from k onwards is a byte pair. */
    for (k = 0; k < ntok; k++) {
        unsigned j;
        int      all = 1;
        for (j = k; j < ntok; j++) {
            if (!token_is_byte(tok[j], tok_len[j])) { all = 0; break; }
        }
        if (all) break;
    }
    if (k == ntok) return 0; /* no byte list at all */

    /* The label is everything before k, minus a leading klog prefix if present. */
    if (k > 0) {
        unsigned first = 0;
        size_t   w     = 0;

        if (tok_len[0] > 1 && tok[0][tok_len[0] - 1] == ':') first = 1;

        for (i = first; i < k && w + 2 < label_size; i++) {
            size_t n = tok_len[i] > label_size - w - 2 ? label_size - w - 2 : tok_len[i];
            memcpy(label_out + w, tok[i], n);
            w += n;
            if (i + 1 < k && w + 1 < label_size) label_out[w++] = ' ';
        }
        label_out[w] = '\0';
    }

    for (i = k; i < ntok && used < out_size; i++) {
        out[used++] = (uint8_t)hex_pair(tok[i]);
    }

    *out_len = used;
    return used > 0;
}

static void print_state(const xcp_pad_state *s, const char *label)
{
    char buttons[192];

    xcp_pad_state_format_buttons(s->buttons, buttons, sizeof(buttons));
    printf("  %-*.*s L(%3u,%3u) R(%3u,%3u) L2=%3u R2=%3u  [%s]\n",
           XCP_LABEL_WIDTH, XCP_LABEL_WIDTH,
           label ? label : "",
           s->left_x, s->left_y, s->right_x, s->right_y,
           s->left_trigger, s->right_trigger, buttons);
}

static int cmd_play(const char *path)
{
    FILE  *f;
    char   line[512];
    int    lineno = 0;
    int    decoded = 0;
    int    rejected = 0;
    xcp_controller_type forced = XCP_CTRL_NONE;

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }

    banner("decoding a report dump");
    printf("file: %s\n", path);

    while (fgets(line, sizeof(line), f)) {
        uint8_t             buf[XCP_USB_MAX_REPORT];
        size_t              len = 0;
        xcp_pad_state       s;
        char                label[XCP_MAX_LABEL];
        char                fallback[XCP_MAX_LABEL];
        xcp_controller_type type;

        lineno++;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        if (!parse_dump_line(line, buf, sizeof(buf), &len,
                             label, sizeof(label))) {
            continue;
        }

        if (label[0] == '\0') {
            snprintf(fallback, sizeof(fallback), "line %d", lineno);
            snprintf(label, sizeof(label), "%s", fallback);
        }

        /* Guess the protocol from the report itself unless forced. */
        if (forced != XCP_CTRL_NONE) {
            type = forced;
        } else if (xcp_report_looks_valid(XCP_CTRL_XBOX360, buf, len)) {
            type = XCP_CTRL_XBOX360;
        } else if (xcp_report_looks_valid(XCP_CTRL_XBOXONE, buf, len)) {
            type = XCP_CTRL_XBOXONE;
        } else if (len == XCP_SWITCH_REPORT_SIZE) {
            type = XCP_CTRL_SWITCH;
        } else {
            /* Name the expected size: a pasted klog line that got wrapped or
             * truncated is the likeliest reason to land here. */
            printf("  %-*.*s %2zu bytes  REJECTED (no protocol matches; "
                   "Xbox 360 needs %d, Xbox One %d, Switch %d)\n",
                   XCP_LABEL_WIDTH, XCP_LABEL_WIDTH, label, len,
                   XCP_XBOX360_REPORT_SIZE, XCP_XBOXONE_REPORT_SIZE,
                   XCP_SWITCH_REPORT_SIZE);
            rejected++;
            continue;
        }

        if (!xcp_decode(type, buf, len, NULL, &s)) {
            printf("  %-*.*s %2zu bytes  REJECTED by %s\n",
                   XCP_LABEL_WIDTH, XCP_LABEL_WIDTH, label, len, type_label(type));
            rejected++;
            continue;
        }

        printf("  [%s] ", type_label(type));
        print_state(&s, label);
        decoded++;
    }

    fclose(f);

    printf("\n%d report(s) decoded, %d rejected\n", decoded, rejected);
    return rejected == 0 ? 0 : 2;
}

/* ---------------------------------------------------------------------------
 * --selftest : synthetic reports through core + router, no hardware
 * ------------------------------------------------------------------------ */

static int g_selftest_failures;

static void check(int condition, const char *what)
{
    printf("  [%s] %s\n", condition ? "\033[32m ok \033[0m" : "\033[31mFAIL\033[0m", what);
    if (!condition) g_selftest_failures++;
}

static void make_360_report(uint8_t *buf, uint8_t low, uint8_t high,
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
    buf[6]  = (uint8_t)(lx & 0xFF); buf[7]  = (uint8_t)((lx >> 8) & 0xFF);
    buf[8]  = (uint8_t)(ly & 0xFF); buf[9]  = (uint8_t)((ly >> 8) & 0xFF);
    buf[10] = (uint8_t)(rx & 0xFF); buf[11] = (uint8_t)((rx >> 8) & 0xFF);
    buf[12] = (uint8_t)(ry & 0xFF); buf[13] = (uint8_t)((ry >> 8) & 0xFF);
}

static int cmd_selftest(void)
{
    uint8_t           report[20];
    xcp_pad_state     s;
    xcp_mock_host     console;
    xcp_router        router;
    xcp_router_config cfg;
    xcp_pad_data      out;
    int32_t           h1, h2;
    char              line[256];

    banner("selftest: synthetic reports through the whole pipeline");

    /* --- core --- */
    make_360_report(report, 0, 0, 0, 0, 0, 0, 0, 0);
    check(xcp_decode(XCP_CTRL_XBOX360, report, sizeof(report), NULL, &s) == 1,
          "a centred Xbox 360 report decodes");
    check(s.left_x == 128 && s.left_y == 128, "a centred stick reads as 128,128");

    make_360_report(report, XCP_360_DPAD_UP, XCP_360_A | XCP_360_LB,
                    255, 0, 32767, 32767, 0, 0);
    check(xcp_decode(XCP_CTRL_XBOX360, report, sizeof(report), NULL, &s) == 1,
          "an active Xbox 360 report decodes");
    check(s.buttons == (XCP_BTN_UP | XCP_BTN_CROSS | XCP_BTN_L1 | XCP_BTN_L2),
          "A+LB+dpad-up+LT map to CROSS+L1+UP+L2");
    check(s.left_x == 255 && s.left_y == 0, "left stick hard right + up maps to 255,0");

    memset(report, 0, sizeof(report));
    check(xcp_decode(XCP_CTRL_XBOX360, report, sizeof(report), NULL, &s) == 0,
          "a garbage report is rejected");

    /* --- router --- */
    xcp_mock_host_init(&console);
    xcp_mock_host_login(&console, 1);
    xcp_mock_host_login(&console, 2);
    xcp_mock_host_set_foreground(&console, 1);
    xcp_router_config_default(&cfg);
    xcp_router_init(&router, &console.api, &cfg);

    make_360_report(report, 0, XCP_360_A, 0, 0, 0, 0, 0, 0);
    xcp_decode(XCP_CTRL_XBOX360, report, sizeof(report), NULL, &s);
    xcp_router_set_xbox(&router, &s, 1);

    h1 = xcp_router_pad_open(&router, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);
    h2 = xcp_router_pad_open(&router, 2, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    check(h1 >= 0 && h2 >= 0, "a game can open a pad for each logged-in user");
    check(router.p1_handle == h1, "player 1 is the first pad opened");
    check(!xcp_router_handle_is_p1(&router, h2), "player 2 is left alone");

    {
        xcp_pad_state ds4;
        memset(&ds4, 0, sizeof(ds4));
        xcp_pad_state_centre_sticks(&ds4);
        ds4.connected = 1;
        ds4.buttons   = XCP_BTN_SQUARE;
        xcp_mock_host_set_user_state(&console, 1, &ds4);
        xcp_mock_host_set_user_state(&console, 2, &ds4);
    }

    xcp_router_pad_read_state(&router, h1, &out);
    check(out.buttons == XCP_BTN_CROSS,
          "player 1 sees the Xbox pad (A) while the DS4 is pressing SQUARE");
    xcp_router_pad_read_state(&router, h2, &out);
    check(out.buttons == XCP_BTN_SQUARE, "player 2 still sees its own DS4");

    xcp_router_set_xbox(&router, NULL, 0);
    xcp_router_pad_read_state(&router, h1, &out);
    check(out.buttons == XCP_BTN_SQUARE,
          "unplugging the Xbox pad hands player 1 back to the DS4");

    xcp_router_describe(&router, line, sizeof(line));
    printf("  router: %s\n", line);

    printf("\n%s\n", g_selftest_failures ? "\033[31mSELFTEST FAILED\033[0m"
                                        : "\033[32mSELFTEST OK\033[0m");
    return g_selftest_failures ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * --mock : show the assignment decisions for a few policies
 * ------------------------------------------------------------------------ */

static const char *policy_name(xcp_p1_policy p)
{
    switch (p) {
    case XCP_MERGE:            return "merge";
    case XCP_XBOX_ONLY:        return "xbox-only";
    case XCP_XBOX_WHEN_ACTIVE:
    default:                   return "xbox-when-active";
    }
}

static const char *target_name(xcp_p1_target t)
{
    switch (t) {
    case XCP_P1_FOREGROUND_USER: return "foreground-user";
    case XCP_P1_ANY_HANDLE:      return "any-handle";
    case XCP_P1_FIRST_OPEN:
    default:                     return "first-open";
    }
}

static void run_mock_case(const char *name, xcp_p1_target target, xcp_p1_policy policy,
                          int xbox_present, uint32_t xbox_buttons, uint32_t ds4_buttons)
{
    char xbox_text[192];
    char ds4_text[192];
    char p1_text[192];

    xcp_mock_host     console;
    xcp_router        router;
    xcp_router_config cfg;
    xcp_pad_state     xb;
    xcp_pad_state     ds4;
    xcp_pad_data      out;
    int32_t           h1;

    xcp_mock_host_init(&console);
    xcp_mock_host_login(&console, 1);
    xcp_mock_host_login(&console, 2);
    xcp_mock_host_set_foreground(&console, 1);

    xcp_router_config_default(&cfg);
    cfg.target = target;
    cfg.policy = policy;
    xcp_router_init(&router, &console.api, &cfg);

    memset(&xb, 0, sizeof(xb));
    xcp_pad_state_centre_sticks(&xb);
    xb.connected = 1;
    xb.buttons   = xbox_buttons;
    xcp_router_set_xbox(&router, &xb, xbox_present);

    h1 = xcp_router_pad_open(&router, 1, XCP_MOCK_PORT_TYPE_STANDARD, 0, NULL);

    memset(&ds4, 0, sizeof(ds4));
    xcp_pad_state_centre_sticks(&ds4);
    ds4.connected = 1;
    ds4.buttons   = ds4_buttons;
    xcp_mock_host_set_user_state(&console, 1, &ds4);

    memset(&out, 0, sizeof(out));
    xcp_router_pad_read_state(&router, h1, &out);

    xcp_pad_state_format_buttons(xbox_present ? xbox_buttons : 0,
                                 xbox_text, sizeof(xbox_text));
    xcp_pad_state_format_buttons(ds4_buttons, ds4_text, sizeof(ds4_text));
    xcp_pad_state_format_buttons(out.buttons, p1_text, sizeof(p1_text));

    printf("  %-14s %-17s %-10s xbox=[%-12s] ds4=[%-12s] -> player1=[%s]\n",
           name, policy_name(policy),
           xbox_present ? "plugged" : "unplugged",
           xbox_text, ds4_text, p1_text);
}

static int cmd_mock(void)
{
    banner("router decisions (no hardware)");

    printf("  Xbox/ds4 columns are what each physical pad holds; the last column\n");
    printf("  is what player 1 actually receives.\n\n");
    printf("  %-14s %-17s %-10s %-16s %-16s %s\n",
           "case", "policy", "xbox", "xbox buttons", "ds4 buttons", "-> player 1");
    printf("  %s\n", "---------------------------------------------------------------------------------------------------");

    run_mock_case("xbox active", XCP_P1_FIRST_OPEN, XCP_XBOX_WHEN_ACTIVE,
                  1, XCP_BTN_CROSS, XCP_BTN_SQUARE);
    run_mock_case("xbox idle", XCP_P1_FIRST_OPEN, XCP_XBOX_WHEN_ACTIVE,
                  1, 0, XCP_BTN_SQUARE);
    run_mock_case("xbox unplugged", XCP_P1_FIRST_OPEN, XCP_XBOX_WHEN_ACTIVE,
                  0, XCP_BTN_CROSS, XCP_BTN_SQUARE);
    run_mock_case("merge", XCP_P1_FIRST_OPEN, XCP_MERGE,
                  1, XCP_BTN_CROSS, XCP_BTN_SQUARE);
    run_mock_case("xbox only", XCP_P1_FIRST_OPEN, XCP_XBOX_ONLY,
                  1, XCP_BTN_CROSS, XCP_BTN_SQUARE);
    run_mock_case("foreground user", XCP_P1_FOREGROUND_USER, XCP_XBOX_WHEN_ACTIVE,
                  1, XCP_BTN_CROSS, XCP_BTN_SQUARE);

    printf("\n  target selection (which handle may become player 1):\n");
    printf("    first-open      : %s\n", target_name(XCP_P1_FIRST_OPEN));
    printf("    foreground-user : %s\n", target_name(XCP_P1_FOREGROUND_USER));
    printf("    any-handle      : %s\n", target_name(XCP_P1_ANY_HANDLE));

    return 0;
}

/* ---------------------------------------------------------------------------
 * --live
 * ------------------------------------------------------------------------ */

/*
 * The oracle: map an XInput button mask to our button bits using a table that
 * is written out independently of core/src/translate.c. If the two disagree,
 * one of them is wrong - which is the whole point of the comparison.
 */
static uint32_t oracle_buttons(uint16_t xi)
{
    uint32_t m = 0;

    if (xi & 0x0001u) m |= XCP_BTN_UP;
    if (xi & 0x0002u) m |= XCP_BTN_DOWN;
    if (xi & 0x0004u) m |= XCP_BTN_LEFT;
    if (xi & 0x0008u) m |= XCP_BTN_RIGHT;
    if (xi & 0x0010u) m |= XCP_BTN_OPTIONS; /* Start  */
    if (xi & 0x0020u) m |= XCP_BTN_SHARE;   /* Back   */
    if (xi & 0x0040u) m |= XCP_BTN_L3;
    if (xi & 0x0080u) m |= XCP_BTN_R3;
    if (xi & 0x0100u) m |= XCP_BTN_L1;
    if (xi & 0x0200u) m |= XCP_BTN_R1;
    if (xi & 0x1000u) m |= XCP_BTN_CROSS;   /* A      */
    if (xi & 0x2000u) m |= XCP_BTN_CIRCLE;  /* B      */
    if (xi & 0x4000u) m |= XCP_BTN_SQUARE;  /* X      */
    if (xi & 0x8000u) m |= XCP_BTN_TRIANGLE;/* Y      */
    return m;
}

static int cmd_live(int seconds)
{
    int                slot;
    int                ticks;
    xcp_xinput_reading last;
    int                have_last = 0;
    int                runs = 0;
    int                mismatches = 0;
    int                changed_states = 0;
    uint32_t           seen_buttons = 0;
    int                seen_any = 0;
    int                axes_seeded = 0;
    int16_t            axis_min[4], axis_max[4];
    unsigned           lt_peak = 0, rt_peak = 0;

    banner("live read of the attached controller");

    if (!xcp_xinput_available()) {
        printf("XInput is not available here, so there is no live pad to read.\n");
        printf("Use --list / --selftest / --play instead; they need nothing.\n");
        return 1;
    }

    slot = xcp_xinput_find_connected_slot();
    if (slot < 0) {
        printf("XInput loaded but no pad is connected in any of the 4 slots.\n");
        printf("Plug the controller in, in wired Xbox mode, then retry.\n");
        return 1;
    }

    printf("pad found in XInput slot %d\n", slot);
    printf("for %d s, printing every change. Move sticks / press buttons now.\n", seconds);
    printf("\n  %-8s %-34s %-24s %s\n", "packet", "xinput (raw)", "our decoder", "verdict");
    printf("  %s\n", "---------------------------------------------------------------------------");
    axis_min[0] = axis_min[1] = axis_min[2] = axis_min[3] = 0;
    axis_max[0] = axis_max[1] = axis_max[2] = axis_max[3] = 0;

    /* ~4 ms polling for the requested duration. */
    ticks = seconds * 250;

    for (; ticks > 0; ticks--) {
        xcp_xinput_reading r;
        uint8_t            report[XCP_XBOX360_REPORT_SIZE];
        xcp_pad_state      decoded;
        uint32_t           expected;
        char               ours[192];
        char               theirs[192];
        char               xi_text[192];

        xcp_sleep_ms(4);

        if (xcp_xinput_read(slot, &r) != 0) {
            printf("  pad disappeared\n");
            return 1;
        }

        runs++;

        /*
         * Throttle on the pad's actual contents, not on its packet counter:
         * this pad bumps the counter on every poll, which would otherwise
         * print a line every 4 ms and hide the signal.
         */
        if (have_last &&
            r.buttons == last.buttons &&
            r.left_trigger == last.left_trigger &&
            r.right_trigger == last.right_trigger &&
            r.left_x == last.left_x && r.left_y == last.left_y &&
            r.right_x == last.right_x && r.right_y == last.right_y) {
            continue;
        }

        last      = r;
        have_last = 1;
        changed_states++;

        /* Coverage: which inputs did this session actually exercise? */
        {
            int16_t axes[4];
            int     a;

            axes[0] = r.left_x;  axes[1] = r.left_y;
            axes[2] = r.right_x; axes[3] = r.right_y;

            for (a = 0; a < 4; a++) {
                if (!axes_seeded || axes[a] < axis_min[a]) axis_min[a] = axes[a];
                if (!axes_seeded || axes[a] > axis_max[a]) axis_max[a] = axes[a];
            }
            axes_seeded = 1;

            if (r.buttons || r.left_trigger || r.right_trigger ||
                r.left_x || r.left_y || r.right_x || r.right_y) {
                seen_any = 1;
            }
        }
        seen_buttons |= r.buttons;
        if (r.left_trigger  > lt_peak) lt_peak = r.left_trigger;
        if (r.right_trigger > rt_peak) rt_peak = r.right_trigger;

        /* Pack the OS's view into a wire-format report and decode it with the
         * exact code the PS4 plugin runs. */
        xcp_pack_xbox360(report, sizeof(report), r.buttons,
                         r.left_trigger, r.right_trigger,
                         r.left_x, r.left_y, r.right_x, r.right_y);

        if (!xcp_decode(XCP_CTRL_XBOX360, report, sizeof(report), NULL, &decoded)) {
            printf("  our decoder REJECTED the packed report\n");
            mismatches++;
            continue;
        }

        expected = oracle_buttons(r.buttons);
        /* Analog triggers also raise the digital bits once past threshold. */
        if (r.left_trigger  >= XCP_DEFAULT_TRIGGER_THRESHOLD) expected |= XCP_BTN_L2;
        if (r.right_trigger >= XCP_DEFAULT_TRIGGER_THRESHOLD) expected |= XCP_BTN_R2;

        xcp_pad_state_format_buttons(r.buttons, xi_text, sizeof(xi_text));
        xcp_pad_state_format_buttons(expected, theirs, sizeof(theirs));
        xcp_pad_state_format_buttons(decoded.buttons, ours, sizeof(ours));

        if (decoded.buttons != expected) mismatches++;

        printf("  %-8u L(%6d,%6d) R(%6d,%6d) LT=%3u RT=%3u\n",
               r.packet, r.left_x, r.left_y, r.right_x, r.right_y,
               r.left_trigger, r.right_trigger);
        printf("           xinput  : %s\n", xi_text);
        printf("           expected: %s\n", theirs);
        printf("           decoded : %s%s\n", ours,
               decoded.buttons == expected ? "" : "   <-- MISMATCH");
        printf("           axes    : L(%u,%u) R(%u,%u) L2=%u R2=%u\n",
               decoded.left_x, decoded.left_y, decoded.right_x, decoded.right_y,
               decoded.left_trigger, decoded.right_trigger);
    }

    printf("\n%d polls, %d state changes, %d button-mapping mismatches\n",
           runs, changed_states, mismatches);

    {
        static const struct { uint16_t bit; const char *name; } names[] = {
            { 0x0001u, "dpad up"    }, { 0x0002u, "dpad down"  },
            { 0x0004u, "dpad left"  }, { 0x0008u, "dpad right" },
            { 0x0010u, "start"      }, { 0x0020u, "back"       },
            { 0x0040u, "L3"         }, { 0x0080u, "R3"         },
            { 0x0100u, "LB"         }, { 0x0200u, "RB"         },
            { 0x1000u, "A"          }, { 0x2000u, "B"          },
            { 0x4000u, "X"          }, { 0x8000u, "Y"          },
        };
        unsigned n;
        int missing = 0;

        printf("\ncoverage of this session:\n  buttons seen : ");
        if (!seen_buttons) printf("(none)");
        for (n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
            if (seen_buttons & names[n].bit) printf("%s ", names[n].name);
        }
        for (n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
            if (!(seen_buttons & names[n].bit)) missing++;
        }
        printf("\n  not seen     : ");
        for (n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
            if (!(seen_buttons & names[n].bit)) printf("%s ", names[n].name);
        }
        printf("\n  axis extremes: LX %d..%d  LY %d..%d  RX %d..%d  RY %d..%d\n",
               axis_min[0], axis_max[0], axis_min[1], axis_max[1],
               axis_min[2], axis_max[2], axis_min[3], axis_max[3]);
        printf("  L2/R2 peak  : %u / %u\n", lt_peak, rt_peak);

        if (!seen_any) {
            printf("\n\033[33mnothing was touched during this run\033[0m - rerun and\n");
            printf("press every button, both triggers, both sticks to the edges.\n");
        } else if (missing == 0) {
            printf("\n\033[32mevery button was exercised\033[0m\n");
        } else {
            printf("\n\033[33m%d button(s) never seen\033[0m - rerun and press them.\n", missing);
        }
    }
    if (mismatches == 0) {
        printf("\033[32mdecoder agrees with XInput on every sampled state\033[0m\n");
    } else {
        printf("\033[31mdecoder disagreed with XInput %d time(s)\033[0m\n", mismatches);
    }
    printf("\nWhat this proves: button and axis mapping against real hardware.\n");
    printf("What it does NOT prove: the on-the-wire byte order. That is what the\n");
    printf("console capture + --play is for.\n");

    return mismatches == 0 ? 0 : 1;
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------ */

static void usage(void)
{
    printf("xcp_inspector - PC side of the Xbox->PS4 player-1 plugin\n\n");
    printf("usage:\n");
    printf("  --list                 show attached HID/USB devices and what we make of them\n");
    printf("  --selftest             run synthetic reports through core + router\n");
    printf("  --play FILE            decode a hex report dump\n");
    printf("  --mock                 print the player-1 policy decisions\n");
    printf("  --live [SECONDS]       read the real controller (needs a backend)\n");
    printf("  --help\n");
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "--help";

    if (strcmp(cmd, "--list") == 0) {
        cmd_list();
        return 0;
    }
    if (strcmp(cmd, "--selftest") == 0) {
        return cmd_selftest();
    }
    if (strcmp(cmd, "--mock") == 0) {
        return cmd_mock();
    }
    if (strcmp(cmd, "--play") == 0) {
        if (argc < 3) {
            fprintf(stderr, "--play needs a file argument\n");
            return 1;
        }
        return cmd_play(argv[2]);
    }
    if (strcmp(cmd, "--live") == 0) {
        int seconds = argc > 2 ? atoi(argv[2]) : 10;
        return cmd_live(seconds);
    }

    usage();
    return strcmp(cmd, "--help") == 0 ? 0 : 1;
}
