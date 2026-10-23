/*
 * core/src/hid_generic.c - fallback for an unnamed HID gamepad.
 *
 * This is a small HID report-descriptor parser, not a table of clone ids.
 * It understands the standard input items: buttons (Usage Page 0x09),
 * Generic Desktop axes X/Y/Z/Rx/Ry/Rz and the hat switch (0x39). The mapping
 * from those fields to DualShock controls is intentionally conventional; a
 * per-device override can change button bits after parsing.
 *
 * Kept deliberately conservative: a mouse also has X/Y and buttons, so an
 * unknown interface is only accepted when the descriptor declares a Gamepad /
 * Joystick / Multi-axis application collection, or has a hat switch plus axes.
 */
#include "xcp/hid_generic.h"

#include <string.h>

#define XCP_HID_MAX_BITS 4096u
#define XCP_HID_MAX_USAGES 64u
#define XCP_HID_TRIGGER_PRESS 30u

typedef struct {
    uint16_t page;
    int32_t  lmin, lmax;
    uint32_t size, count;
    uint8_t  rid;
} hid_globals;

static int32_t item_value(const uint8_t *p, size_t size, int is_signed)
{
    uint32_t v = 0;
    size_t i;

    for (i = 0; i < size; i++) v |= (uint32_t)p[i] << (8u * i);
    if (is_signed && size > 0 && size < 4 &&
        (v & (1u << (8u * size - 1u))) != 0) {
        v |= ~0u << (8u * size);
    }
    return (int32_t)v;
}

static void put_field(xcp_hid_map *m, uint32_t usage, uint16_t page,
                      const hid_globals *gl, uint16_t offset)
{
    xcp_hid_field f;
    uint16_t pg = (usage >> 16) != 0 ? (uint16_t)(usage >> 16) : page;
    uint16_t u  = (uint16_t)usage;
    xcp_hid_field *slot = NULL;

    f.bit_offset  = offset;
    f.bit_size    = (uint16_t)gl->size;
    f.logical_min = gl->lmin;
    f.logical_max = gl->lmax;
    f.report_id   = gl->rid;
    f.present     = 1;

    if (pg == 0x01u && u >= 0x30u && u <= 0x35u) {
        slot = &m->axis[u - 0x30u];
    } else if (pg == 0x01u && u == 0x39u) {
        slot = &m->hat;
    } else if (pg == 0x09u && u >= 1u && u <= XCP_HID_MAX_BUTTONS) {
        slot = &m->button[u - 1u];
        if (u > m->button_count) m->button_count = (uint8_t)u;
    }
    if (slot && !slot->present) *slot = f; /* first definition wins */
}

int xcp_hid_parse_descriptor(const uint8_t *d, size_t n,
                             uint16_t vid, uint16_t pid, xcp_hid_map *out)
{
    hid_globals gl, stack[4];
    uint16_t offset[256];
    uint32_t usages[XCP_HID_MAX_USAGES];
    uint32_t umin = 0, umax = 0;
    size_t i = 0;
    int depth = 0;
    int collection_depth = 0;
    int nusages = 0;
    int has_range = 0;
    int application_gamepad = 0;
    int has_hat = 0;
    int has_axes = 0;
    int has_buttons = 0;

    (void)vid;
    (void)pid;
    if (!d || !out) return 0;

    memset(out, 0, sizeof(*out));
    memset(&gl, 0, sizeof(gl));
    memset(offset, 0, sizeof(offset));

    while (i < n) {
        uint8_t prefix = d[i++];
        size_t item_size;
        unsigned type, tag;
        const uint8_t *v;
        int32_t val;

        if (prefix == 0xFEu) { /* long item: size, tag, data */
            if (i + 2u > n) return 0;
            item_size = d[i];
            i += 2u;
            if (i + item_size > n) return 0;
            i += item_size;
            continue;
        }

        item_size = (prefix & 0x03u) == 3u ? 4u : (prefix & 0x03u);
        type = (prefix >> 2) & 0x03u;
        tag  = (prefix >> 4) & 0x0Fu;
        if (i + item_size > n) return 0;
        v = d + i;
        val = item_value(v, item_size, type == 1u && tag == 1u);
        i += item_size;

        if (type == 1u) { /* global */
            switch (tag) {
            case 0u: gl.page = (uint16_t)val; break;
            case 1u: gl.lmin = val; break;
            case 2u:
                gl.lmax = gl.lmin >= 0
                    ? (int32_t)item_value(v, item_size, 0)
                    : val;
                break;
            case 3u: gl.lmin = val; break;
            case 7u: gl.size = (uint32_t)val; break;
            case 8u:
                gl.rid = (uint8_t)val;
                out->has_report_ids = 1;
                break;
            case 9u: gl.count = (uint32_t)val; break;
            case 10u:
                if (depth >= 4) return 0;
                stack[depth++] = gl;
                break;
            case 11u:
                if (depth <= 0) return 0;
                gl = stack[--depth];
                break;
            default: break;
            }
        } else if (type == 2u) { /* local */
            uint32_t u = item_size == 4u ? (uint32_t)val
                                         : (uint32_t)val & 0xFFFFu;
            if (tag == 0u && nusages < (int)XCP_HID_MAX_USAGES) {
                usages[nusages++] = u;
            } else if (tag == 1u) {
                umin = u;
                has_range = 1;
            } else if (tag == 2u) {
                umax = u;
                has_range = 1;
            }
        } else if (type == 0u) { /* main */
            if (tag == 0xAu) { /* collection */
                collection_depth++;
                if (val == 1 && gl.page == 0x01u && nusages > 0) {
                    uint32_t u = usages[0] & 0xFFFFu;
                    if (u == 0x04u || u == 0x05u || u == 0x08u) {
                        application_gamepad = 1;
                    }
                }
            }
            if (tag == 0xCu) { /* end collection */
                if (collection_depth <= 0) return 0;
                collection_depth--;
            }
            if (tag == 8u) { /* input */
                uint32_t k, bits;
                uint16_t *off = &offset[gl.rid];

                if (gl.size == 0 || gl.size > 32u || gl.count > XCP_HID_MAX_BITS) {
                    return 0;
                }
                bits = gl.count * gl.size;
                if (*off + bits > XCP_HID_MAX_BITS) return 0;

                if ((val & 1) == 0) { /* data, not constant padding */
                    for (k = 0; k < gl.count; k++) {
                        uint32_t u;
                        if (nusages > 0) {
                            u = usages[k < (uint32_t)nusages
                                       ? k : (uint32_t)nusages - 1u];
                        } else if (has_range) {
                            u = umin + k > umax ? umax : umin + k;
                        } else {
                            break;
                        }
                        put_field(out, u, gl.page, &gl,
                                  (uint16_t)(*off + k * gl.size));
                    }
                }
                *off = (uint16_t)(*off + bits);
            }
            nusages = 0;
            has_range = 0;
            umin = umax = 0;
        }
    }

    if (collection_depth != 0) return 0;

    has_hat = out->hat.present != 0;
    has_axes = out->axis[XCP_HID_AXIS_X].present &&
               out->axis[XCP_HID_AXIS_Y].present;
    has_buttons = out->button_count >= 4;
    if (!application_gamepad && !(has_hat && has_axes && has_buttons)) {
        return 0;
    }
    if (!has_axes && !has_hat && !has_buttons) return 0;

    xcp_hid_apply_defaults(out);
    return 1;
}

void xcp_hid_set_button(xcp_hid_map *map, unsigned button, uint32_t bits)
{
    if (!map || button < 1u || button > XCP_HID_MAX_BUTTONS) return;
    map->button_bits[button - 1u] = bits;
}

void xcp_hid_apply_defaults(xcp_hid_map *map)
{
    static const uint32_t android[15] = {
        XCP_BTN_CROSS, XCP_BTN_CIRCLE, 0, XCP_BTN_SQUARE, XCP_BTN_TRIANGLE,
        0, XCP_BTN_L1, XCP_BTN_R1, XCP_BTN_L2, XCP_BTN_R2,
        XCP_BTN_SHARE, XCP_BTN_OPTIONS, XCP_BTN_PS, XCP_BTN_L3, XCP_BTN_R3,
    };
    static const uint32_t dinput[14] = {
        XCP_BTN_SQUARE, XCP_BTN_CROSS, XCP_BTN_CIRCLE, XCP_BTN_TRIANGLE,
        XCP_BTN_L1, XCP_BTN_R1, XCP_BTN_L2, XCP_BTN_R2,
        XCP_BTN_SHARE, XCP_BTN_OPTIONS, XCP_BTN_L3, XCP_BTN_R3,
        XCP_BTN_PS, XCP_BTN_TOUCHPAD,
    };
    unsigned i;

    if (!map) return;
    memset(map->button_bits, 0, sizeof(map->button_bits));
    if (map->button_count >= 15) {
        for (i = 0; i < 15; i++) map->button_bits[i] = android[i];
    } else {
        for (i = 0; i < 14; i++) map->button_bits[i] = dinput[i];
    }
}

static int32_t get_bits(const uint8_t *d, size_t len, const xcp_hid_field *f)
{
    uint32_t v = 0;
    size_t i;

    if (!f->present || f->bit_size == 0 || f->bit_size > 32u) return f->logical_min;
    if (((size_t)f->bit_offset + f->bit_size + 7u) / 8u > len) return f->logical_min;
    for (i = 0; i < f->bit_size; i++) {
        size_t bit = (size_t)f->bit_offset + i;
        if ((d[bit >> 3] & (1u << (bit & 7u))) != 0) v |= 1u << i;
    }
    if (f->logical_min < 0 && f->bit_size < 32u &&
        (v & (1u << (f->bit_size - 1u))) != 0) {
        v |= ~0u << f->bit_size;
    }
    return (int32_t)v;
}

static uint8_t to_byte(const xcp_hid_field *f, int32_t v)
{
    int64_t span = (int64_t)f->logical_max - f->logical_min;
    int64_t s;

    if (!f->present || span <= 0) return XCP_STICK_CENTER;
    s = ((int64_t)v - f->logical_min) * 255 / span;
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    return (uint8_t)s;
}

static int field_for_report(const xcp_hid_field *f, uint8_t rid)
{
    return f != NULL && f->present && f->report_id == rid;
}

/*
 * A HID input report has a fixed length for each report id. If the transfer is
 * shorter than the described controls, the missing bytes must not be decoded as
 * zero (that would move sticks to a corner); reject the whole report and keep
 * the previous state.
 */
static int field_fits(const xcp_hid_field *f, uint8_t rid, size_t len)
{
    size_t need;

    if (!field_for_report(f, rid)) return 1;
    need = ((size_t)f->bit_offset + f->bit_size + 7u) / 8u;
    return need <= len;
}

static int report_fits(const xcp_hid_map *map, uint8_t rid, size_t len)
{
    unsigned i;

    for (i = 0; i < XCP_HID_AXES; i++) {
        if (!field_fits(&map->axis[i], rid, len)) return 0;
    }
    if (!field_fits(&map->hat, rid, len)) return 0;
    for (i = 0; i < map->button_count; i++) {
        if (!field_fits(&map->button[i], rid, len)) return 0;
    }
    return 1;
}

int xcp_hid_decode(const xcp_hid_map *map, const uint8_t *buf, size_t len,
                   xcp_pad_state *out)
{
    static const uint32_t dirs[8] = {
        XCP_BTN_UP, XCP_BTN_UP | XCP_BTN_RIGHT, XCP_BTN_RIGHT,
        XCP_BTN_DOWN | XCP_BTN_RIGHT, XCP_BTN_DOWN,
        XCP_BTN_DOWN | XCP_BTN_LEFT, XCP_BTN_LEFT,
        XCP_BTN_UP | XCP_BTN_LEFT,
    };
    const uint8_t *d = buf;
    uint8_t rid = 0;
    uint32_t pressed = 0, mask = 0;
    int used = 0;
    unsigned i;

    if (!map || !buf || !out) return 0;
    if (map->has_report_ids) {
        if (len < 1) return 0;
        rid = d[0];
        d++;
        len--;
    }
    if (!report_fits(map, rid, len)) return 0;

    /*
     * Left stick is X/Y. If the descriptor has Z/Rz they are the right stick;
     * otherwise Rx/Ry are. The pair left over is a pair of analog triggers.
     */
    {
        const xcp_hid_field *lx = &map->axis[XCP_HID_AXIS_X];
        const xcp_hid_field *ly = &map->axis[XCP_HID_AXIS_Y];
        const xcp_hid_field *z  = &map->axis[XCP_HID_AXIS_Z];
        const xcp_hid_field *rz = &map->axis[XCP_HID_AXIS_RZ];
        const xcp_hid_field *rx = &map->axis[XCP_HID_AXIS_RX];
        const xcp_hid_field *ry = &map->axis[XCP_HID_AXIS_RY];
        const xcp_hid_field *l2 = NULL;
        const xcp_hid_field *r2 = NULL;
        const xcp_hid_field *right_x = NULL;
        const xcp_hid_field *right_y = NULL;

        if (z->present && rz->present) {
            right_x = z;
            right_y = rz;
            if (rx->present && ry->present) {
                l2 = rx;
                r2 = ry;
            }
        } else if (rx->present && ry->present) {
            right_x = rx;
            right_y = ry;
            if (z->present) l2 = z;
            if (rz->present) r2 = rz;
        }

        if (field_for_report(lx, rid)) {
            out->left_x = to_byte(lx, get_bits(d, len, lx));
            used = 1;
        }
        if (field_for_report(ly, rid)) {
            out->left_y = to_byte(ly, get_bits(d, len, ly));
            used = 1;
        }
        if (field_for_report(right_x, rid)) {
            out->right_x = to_byte(right_x, get_bits(d, len, right_x));
            used = 1;
        }
        if (field_for_report(right_y, rid)) {
            out->right_y = to_byte(right_y, get_bits(d, len, right_y));
            used = 1;
        }
        if (field_for_report(l2, rid)) {
            out->left_trigger = to_byte(l2, get_bits(d, len, l2));
            mask |= XCP_BTN_L2;
            if (out->left_trigger > XCP_HID_TRIGGER_PRESS) {
                pressed |= XCP_BTN_L2;
            }
            used = 1;
        }
        if (field_for_report(r2, rid)) {
            out->right_trigger = to_byte(r2, get_bits(d, len, r2));
            mask |= XCP_BTN_R2;
            if (out->right_trigger > XCP_HID_TRIGGER_PRESS) {
                pressed |= XCP_BTN_R2;
            }
            used = 1;
        }
    }

    for (i = 0; i < map->button_count; i++) {
        const xcp_hid_field *f = &map->button[i];

        if (!field_for_report(f, rid)) continue;
        if (map->button_bits[i] != 0) {
            mask |= map->button_bits[i];
            if (get_bits(d, len, f) != 0) pressed |= map->button_bits[i];
        }
        used = 1;
    }

    if (field_for_report(&map->hat, rid)) {
        int32_t h = get_bits(d, len, &map->hat) - map->hat.logical_min;
        mask |= XCP_BTN_UP | XCP_BTN_DOWN | XCP_BTN_LEFT | XCP_BTN_RIGHT;
        if (h >= 0 && h < 8) pressed |= dirs[h];
        used = 1;
    }

    if (!used) return 0;

    out->buttons = (out->buttons & ~mask) | pressed;
    out->connected = 1;
    return 1;
}
