/*
 * ps4/include/orbis_glue.h - the boundary between Orbis types and our payload.
 *
 * Two rules govern everything here:
 *
 *  1. Only the fields listed in xcp/pad_data.h are ever converted. Motion,
 *     touch and the trailing padding a game may also read are left exactly as
 *     the physical DS4 produced them.
 *  2. No motion-field name appears anywhere, because OpenOrbis git HEAD calls
 *     them orientation/acceleration/angularVelocity while the release (which we
 *     build against) calls them quat/vel/acell. Naming none of them means this
 *     source compiles against either. See PROGRESS.md 4.2.
 */
#ifndef XCP_ORBIS_GLUE_H
#define XCP_ORBIS_GLUE_H

#include <orbis/_types/pad.h>

#include "xcp/pad_data.h"
#include "xcp/pad_state.h"

/*
 * Compile-time check that our restated button constants still match the real
 * Orbis enum. Expressed as a negative array size so it works in C99 as well as
 * C11 and fails the build rather than mis-mapping at runtime.
 */
#define XCP_ASSERT_EQUAL(real, ours, tag) \
    typedef char xcp_assert_##tag[(real) == (ours) ? 1 : -1]

XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_L3,        XCP_BTN_L3,         l3);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_R3,        XCP_BTN_R3,         r3);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_OPTIONS,   XCP_BTN_OPTIONS,    options);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_UP,        XCP_BTN_UP,         up);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_RIGHT,     XCP_BTN_RIGHT,      right);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_DOWN,      XCP_BTN_DOWN,       down);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_LEFT,      XCP_BTN_LEFT,       left);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_L2,        XCP_BTN_L2,         l2);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_R2,        XCP_BTN_R2,         r2);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_L1,        XCP_BTN_L1,         l1);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_R1,        XCP_BTN_R1,         r1);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_TRIANGLE,  XCP_BTN_TRIANGLE,   triangle);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_CIRCLE,    XCP_BTN_CIRCLE,     circle);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_CROSS,     XCP_BTN_CROSS,      cross);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_SQUARE,    XCP_BTN_SQUARE,     square);
XCP_ASSERT_EQUAL(ORBIS_PAD_BUTTON_TOUCH_PAD, XCP_BTN_TOUCHPAD,   touchpad);

/* Port types, so the router's "standard only" rule is not a magic 0. */
#define XCP_ORBIS_PORT_TYPE_STANDARD ORBIS_PAD_PORT_TYPE_STANDARD

/* Read the managed subset out of a real pad sample. */
void xcp_orbis_to_pad_data(const OrbisPadData *in, xcp_pad_data *out);

/*
 * Write the managed subset back. Everything else in `out` is untouched, so the
 * caller must have filled it from a real read first.
 */
void xcp_pad_data_to_orbis(const xcp_pad_data *in, OrbisPadData *out);

#endif /* XCP_ORBIS_GLUE_H */
