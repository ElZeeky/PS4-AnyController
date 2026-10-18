/*
 * core/src/pad_state.c - pad state helpers.
 *
 * The merge function is what lets a real DS4 and the Xbox pad drive the same
 * player slot without fighting each other.
 */
#include "xcp/pad_state.h"

#include <stdio.h>
#include <string.h>

void xcp_pad_state_centre_sticks(xcp_pad_state *s)
{
    if (!s) return;
    s->left_x  = (uint8_t)XCP_STICK_CENTER;
    s->left_y  = (uint8_t)XCP_STICK_CENTER;
    s->right_x = (uint8_t)XCP_STICK_CENTER;
    s->right_y = (uint8_t)XCP_STICK_CENTER;
    s->left_trigger  = 0;
    s->right_trigger = 0;
    s->buttons       = 0;
}

/* Signed distance from centre, used to decide which pad "owns" an axis. */
static int axis_distance(uint8_t value)
{
    return (int)value - (int)XCP_STICK_CENTER;
}

static uint8_t pick_further_axis(uint8_t a, uint8_t b)
{
    int da = axis_distance(a);
    int db = axis_distance(b);
    if (da < 0) da = -da;
    if (db < 0) db = -db;
    return (db > da) ? b : a;
}

void xcp_pad_state_merge(xcp_pad_state *dst, const xcp_pad_state *src)
{
    if (!dst || !src) return;

    if (!dst->connected) {
        *dst = *src;
        return;
    }
    if (!src->connected) return;

    dst->buttons |= src->buttons;

    dst->left_x  = pick_further_axis(dst->left_x,  src->left_x);
    dst->left_y  = pick_further_axis(dst->left_y,  src->left_y);
    dst->right_x = pick_further_axis(dst->right_x, src->right_x);
    dst->right_y = pick_further_axis(dst->right_y, src->right_y);

    if (src->left_trigger  > dst->left_trigger)  dst->left_trigger  = src->left_trigger;
    if (src->right_trigger > dst->right_trigger) dst->right_trigger = src->right_trigger;

    dst->connected = 1;
}

unsigned xcp_pad_state_button_count(uint32_t buttons)
{
    unsigned n = 0;
    while (buttons) {
        n += (unsigned)(buttons & 1u);
        buttons >>= 1;
    }
    return n;
}

int xcp_pad_state_equal(const xcp_pad_state *a, const xcp_pad_state *b)
{
    if (!a || !b) return a == b;
    return memcmp(a, b, sizeof(*a)) == 0;
}

/* ---------------------------------------------------------------------------
 * Button naming (diagnostics / CLI / klog)
 * ------------------------------------------------------------------------ */

typedef struct {
    uint32_t    bit;
    const char *name;
} button_name;

static const button_name g_button_names[] = {
    { XCP_BTN_SHARE,    "SHARE"    },
    { XCP_BTN_L3,       "L3"       },
    { XCP_BTN_R3,       "R3"       },
    { XCP_BTN_OPTIONS,  "OPTIONS"  },
    { XCP_BTN_UP,       "UP"       },
    { XCP_BTN_RIGHT,    "RIGHT"    },
    { XCP_BTN_DOWN,     "DOWN"     },
    { XCP_BTN_LEFT,     "LEFT"     },
    { XCP_BTN_L2,       "L2"       },
    { XCP_BTN_R2,       "R2"       },
    { XCP_BTN_L1,       "L1"       },
    { XCP_BTN_R1,       "R1"       },
    { XCP_BTN_TRIANGLE, "TRIANGLE" },
    { XCP_BTN_CIRCLE,   "CIRCLE"   },
    { XCP_BTN_CROSS,    "CROSS"    },
    { XCP_BTN_SQUARE,   "SQUARE"   },
    { XCP_BTN_PS,       "PS"       },
    { XCP_BTN_TOUCHPAD, "TOUCHPAD" },
};

const char *xcp_button_name(uint32_t single_bit)
{
    size_t i;
    for (i = 0; i < sizeof(g_button_names) / sizeof(g_button_names[0]); i++) {
        if (g_button_names[i].bit == single_bit) return g_button_names[i].name;
    }
    return "?";
}

void xcp_pad_state_format_buttons(uint32_t buttons, char *out, unsigned out_size)
{
    unsigned used = 0;
    size_t   i;

    if (!out || out_size == 0) return;
    out[0] = '\0';

    if (buttons == 0) {
        snprintf(out, out_size, "-");
        return;
    }

    for (i = 0; i < sizeof(g_button_names) / sizeof(g_button_names[0]); i++) {
        const char *name;
        int written;

        if (!(buttons & g_button_names[i].bit)) continue;

        name    = g_button_names[i].name;
        written = snprintf(out + used, out_size - used, "%s%s",
                           used ? " " : "", name);
        if (written < 0) break;
        if ((unsigned)written >= out_size - used) break; /* truncated */
        used += (unsigned)written;
    }
}
