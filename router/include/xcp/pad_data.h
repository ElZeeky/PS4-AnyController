/*
 * xcp/pad_data.h - the pad payload as the router sees it.
 *
 * Deliberately NOT layout-compatible with OrbisPadData: the PS4 layer converts
 * field by field, so a toolchain struct change cannot silently corrupt us.
 *
 * It carries ONLY the fields the plugin is allowed to rewrite: buttons, the two
 * stick pairs, the two triggers and the connected flag. Everything else a game
 * reads from a real pad - motion, touch, the trailing unknown bytes - is left
 * exactly as the physical controller produced it, because the platform layer
 * copies back only the fields below.
 *
 * That is not just tidiness. The OpenOrbis *release* headers call the motion
 * fields quat/vel/acell while the repository HEAD calls them
 * orientation/acceleration/angularVelocity, and the reference plugin only
 * compiles against the release. By never naming those fields we compile against
 * either header set. See PROGRESS.md OPEN-1.
 */
#ifndef XCP_PAD_DATA_H
#define XCP_PAD_DATA_H

#include <stdint.h>

typedef struct xcp_pad_data {
    uint32_t buttons;
    uint8_t  left_x;
    uint8_t  left_y;
    uint8_t  right_x;
    uint8_t  right_y;
    uint8_t  left_trigger;
    uint8_t  right_trigger;
    uint8_t  connected;
} xcp_pad_data;

/*
 * Buttons cleared, sticks centred, triggers released. `connected` is the
 * caller's decision: a real pad reports 1 even when it is idle.
 */
void xcp_pad_data_centre(xcp_pad_data *d);

/*
 * Copy only the managed fields of `src` onto `dst`, field by field.
 * Provided so the platform layer has one obvious place to do the copy-back.
 */
void xcp_pad_data_copy_managed(xcp_pad_data *dst, const xcp_pad_data *src);

#endif /* XCP_PAD_DATA_H */
