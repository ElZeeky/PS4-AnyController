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
    case XCP_CTRL_XBOX360:     return "Xbox 360";
    case XCP_CTRL_XBOXONE:     return "Xbox One";
    case XCP_CTRL_XBOX_SERIES: return "Xbox Series";
    case XCP_CTRL_SWITCH:      return "Switch (PDP)";
    case XCP_CTRL_SWITCH_PRO:  return "Switch Pro";
    case XCP_CTRL_DUALSENSE:   return "DualSense";
    case XCP_CTRL_HID_GENERIC: return "Generic HID";
    case XCP_CTRL_NONE:
    default:                   return "none";
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

static const uint16_t g_xbox_series_pids[] = {
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

int xcp_is_xbox_series_pid(uint16_t pid)
{
    unsigned i;
    for (i = 0; i < sizeof(g_xbox_series_pids) / sizeof(g_xbox_series_pids[0]); i++) {
        if (g_xbox_series_pids[i] == pid) return 1;
    }
    return 0;
}

int xcp_is_dualsense_pid(uint16_t pid)
{
    return pid == XCP_PID_DUALSENSE || pid == XCP_PID_DUALSENSE_EDGE;
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
        /* The wireless receiver speaks a different, framed protocol that no
         * decoder here understands. Claiming it as GIP opens the device and
         * then reads nothing, so report it as unsupported instead. */
        if (pid == XCP_PID_360_WLESS) return XCP_CTRL_NONE;
        if (xcp_is_xbox_series_pid(pid)) return XCP_CTRL_XBOX_SERIES;
        /* Anything else Microsoft is treated as GIP: third-party pads
         * re-badge themselves under VID 0x045E with product ids nobody has
         * catalogued. */
        return XCP_CTRL_XBOXONE;
    }

    if (vid == XCP_VID_SONY && xcp_is_dualsense_pid(pid)) {
        return XCP_CTRL_DUALSENSE;
    }

    if (vid == XCP_VID_PDP && xcp_is_pdp_switch_pid(pid)) {
        return XCP_CTRL_SWITCH;
    }

    if (vid == XCP_VID_NINTENDO && pid == XCP_PID_SWITCH_PRO) {
        return XCP_CTRL_SWITCH_PRO;
    }

    return XCP_CTRL_NONE;
}

static int microsoft_pid_is_known(uint16_t pid)
{
    return pid == XCP_PID_XBOX360 || xcp_is_xboxone_pid(pid);
}

xcp_controller_type xcp_controller_type_for_interface(uint16_t vid, uint16_t pid,
                                                      uint8_t iface_class,
                                                      uint8_t iface_subclass,
                                                      uint8_t iface_protocol,
                                                      int has_interrupt_in)
{
    xcp_controller_type by_id = xcp_controller_type_for_ids(vid, pid);

    /*
     * Sony's pads are the console's, except the DualSense we already know.
     * Do not turn a DualShock 4 into a Generic HID device just because it is
     * class 03 and we have no profile for it.
     */
    if (vid == XCP_VID_SONY) return by_id;

    /*
     * A known vendor/id pair always wins. For Microsoft, an uncatalogued pid
     * is only assumed to be GIP when the interface actually looks like GIP
     * (or when we have no descriptor information at all, which keeps the old
     * id-only behaviour for callers that cannot read one).
     */
    if (by_id != XCP_CTRL_NONE) {
        if (vid != XCP_VID_MICROSOFT || microsoft_pid_is_known(pid) ||
            iface_class == 0) {
            return by_id;
        }
    }

    if (!has_interrupt_in) return XCP_CTRL_NONE;

    if (iface_class == 0xFFu) {
        if (iface_subclass == 0x5Du && iface_protocol == 0x01u) {
            return XCP_CTRL_XBOX360;
        }
        if (iface_subclass == 0x47u && iface_protocol == 0xD0u) {
            return XCP_CTRL_XBOXONE;
        }
        return XCP_CTRL_NONE;
    }

    /*
     * Boot subclasses are keyboards (1) and mice (2). A HID gamepad normally
     * uses subclass/protocol 0. It is still only a candidate until its report
     * descriptor says it really has gamepad controls.
     */
    if (iface_class == 0x03u && iface_subclass == 0 && iface_protocol == 0) {
        return XCP_CTRL_HID_GENERIC;
    }

    return XCP_CTRL_NONE;
}
