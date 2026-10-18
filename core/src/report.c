/*
 * core/src/report.c - controller identification.
 *
 * Product id lists come from the reference plugin (crucifix86, MIT) and from
 * the Xbox One GIP documentation. They are a best-effort identification aid:
 * an unknown Microsoft-VID product id is still treated as GIP, because that is
 * what every Xbox One/Series-generation pad speaks.
 */
#include "xcp/report.h"

const char *xcp_controller_type_name(xcp_controller_type type)
{
    switch (type) {
    case XCP_CTRL_XBOX360: return "Xbox 360";
    case XCP_CTRL_XBOXONE: return "Xbox One/Series";
    case XCP_CTRL_SWITCH:  return "Switch (PDP)";
    case XCP_CTRL_NONE:
    default:               return "none";
    }
}

static const uint16_t g_xboxone_pids[] = {
    0x02D1, /* original Xbox One pad        */
    0x02DD, /* newer Xbox One pad           */
    0x02E0, /* Xbox One S (Bluetooth)       */
    0x02E3, /* Xbox Elite                   */
    0x02EA, /* Xbox One S (USB)             */
    0x0B00, /* Xbox Elite 2                 */
    0x0B0A, /* Xbox Adaptive                */
    0x0B12, /* Xbox Series X|S (USB)        */
    0x0B13, /* Xbox Series X|S (Bluetooth)  */
    0x0B20, /* 2021 Xbox pad                */
};

static const uint16_t g_pdp_switch_pids[] = {
    0x0187, /* PDP Rock Candy wired            */
    0x0180, /* PDP Faceoff wired pro           */
    0x0181, /* PDP Faceoff deluxe wired pro    */
    0x0185, /* PDP wired fight pad pro         */
};

int xcp_is_xboxone_pid(uint16_t pid)
{
    unsigned i;
    for (i = 0; i < sizeof(g_xboxone_pids) / sizeof(g_xboxone_pids[0]); i++) {
        if (g_xboxone_pids[i] == pid) return 1;
    }
    return 0;
}

int xcp_is_pdp_switch_pid(uint16_t pid)
{
    unsigned i;
    for (i = 0; i < sizeof(g_pdp_switch_pids) / sizeof(g_pdp_switch_pids[0]); i++) {
        if (g_pdp_switch_pids[i] == pid) return 1;
    }
    return 0;
}

xcp_controller_type xcp_controller_type_for_ids(uint16_t vid, uint16_t pid)
{
    if (vid == XCP_VID_MICROSOFT) {
        if (pid == XCP_PID_XBOX360) return XCP_CTRL_XBOX360;
        /* 0x0719 (wireless receiver) and anything else Microsoft is treated as
         * GIP: third-party pads re-badge themselves under VID 0x045E with
         * product ids nobody has catalogued. */
        return XCP_CTRL_XBOXONE;
    }

    if (vid == XCP_VID_PDP && xcp_is_pdp_switch_pid(pid)) {
        return XCP_CTRL_SWITCH;
    }

    return XCP_CTRL_NONE;
}
