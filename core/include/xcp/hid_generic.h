/*
 * xcp/hid_generic.h - a HID gamepad described by its own report descriptor.
 *
 * The standard profiles in report.h cover the pads we can name. This is the
 * fallback: an unknown HID interface is accepted only when its report
 * descriptor contains gamepad controls, and the descriptor tells us where the
 * buttons, sticks and hat switch sit in the input report. A small default
 * mapping covers the two common button orders; a per-device override can
 * adjust the button bits after parsing.
 */
#ifndef XCP_HID_GENERIC_H
#define XCP_HID_GENERIC_H

#include <stddef.h>
#include <stdint.h>

#include "xcp/pad_state.h"

#define XCP_HID_MAX_BUTTONS 24
#define XCP_HID_AXES        6

/* Axis indices, in USB HID Generic Desktop order. */
enum {
    XCP_HID_AXIS_X = 0,
    XCP_HID_AXIS_Y,
    XCP_HID_AXIS_Z,
    XCP_HID_AXIS_RX,
    XCP_HID_AXIS_RY,
    XCP_HID_AXIS_RZ
};

typedef struct xcp_hid_field {
    uint16_t bit_offset;      /* from the first byte after a report id */
    uint16_t bit_size;        /* 1..32 */
    int32_t  logical_min;
    int32_t  logical_max;
    uint8_t  report_id;
    uint8_t  present;
} xcp_hid_field;

typedef struct xcp_hid_map {
    uint8_t      has_report_ids;
    uint8_t      button_count;
    xcp_hid_field axis[XCP_HID_AXES];
    xcp_hid_field hat;
    xcp_hid_field button[XCP_HID_MAX_BUTTONS];
    uint32_t     button_bits[XCP_HID_MAX_BUTTONS];
} xcp_hid_map;

/*
 * Parse a HID report descriptor and fill in the fields we understand.
 * Returns 1 when the descriptor has enough gamepad controls to use, 0 when it
 * is malformed or is not a gamepad (keyboard, mouse, vendor page, ...).
 */
int xcp_hid_parse_descriptor(const uint8_t *desc, size_t len,
                             uint16_t vid, uint16_t pid, xcp_hid_map *out);

/*
 * Apply the default button order for a parsed map. Exposed so a future
 * per-device table (or a map file) can replace individual button bits without
 * touching the parser.
 */
void xcp_hid_apply_defaults(xcp_hid_map *map);

/* Override one button (1-based, HID button usage) with an XCP_BTN_* mask. */
void xcp_hid_set_button(xcp_hid_map *map, unsigned button, uint32_t bits);

/*
 * Decode one input report. Only the controls carried by that report are
 * updated, so pads that spread buttons over several report ids keep their
 * previous state. Returns 1 when the report matched at least one field.
 */
int xcp_hid_decode(const xcp_hid_map *map, const uint8_t *buf, size_t len,
                   xcp_pad_state *out);

#endif /* XCP_HID_GENERIC_H */
