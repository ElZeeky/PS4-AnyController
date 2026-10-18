/*
 * ps4/src/orbis_glue.c - conversions across the Orbis boundary.
 *
 * Only the fields in xcp/pad_data.h are read or written. In particular this file
 * must never mention the motion fields, whose names differ between OpenOrbis git
 * HEAD and the release headers we build against. See orbis_glue.h.
 */
#include "orbis_glue.h"

#include <string.h>

void xcp_orbis_to_pad_data(const OrbisPadData *in, xcp_pad_data *out)
{
    if (!out) return;

    memset(out, 0, sizeof(*out));
    if (!in) return;

    out->buttons       = in->buttons;
    out->left_x        = in->leftStick.x;
    out->left_y        = in->leftStick.y;
    out->right_x       = in->rightStick.x;
    out->right_y       = in->rightStick.y;
    out->left_trigger  = in->analogButtons.l2;
    out->right_trigger = in->analogButtons.r2;
    out->connected     = in->connected ? 1u : 0u;
}

void xcp_pad_data_to_orbis(const xcp_pad_data *in, OrbisPadData *out)
{
    if (!in || !out) return;

    out->buttons          = in->buttons;
    out->leftStick.x      = in->left_x;
    out->leftStick.y      = in->left_y;
    out->rightStick.x     = in->right_x;
    out->rightStick.y     = in->right_y;
    out->analogButtons.l2 = in->left_trigger;
    out->analogButtons.r2 = in->right_trigger;
    out->connected        = in->connected ? 1u : 0u;

    /*
     * Everything else - motion, touch, the padding, count, unknown - is left
     * exactly as the real controller reported it. That is why this function
     * takes a pointer to a buffer that a real read already filled.
     */
}
