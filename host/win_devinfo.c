/*
 * host/win_devinfo.c - see win_devinfo.h.
 *
 * Read-only: opening keys, never a device. Requires no administrator rights.
 */
#include "win_devinfo.h"

#include <stdio.h>
#include <string.h>

int xcp_enumerate_available(void);

#if defined(_WIN32)

#include <windows.h>
#include <setupapi.h>

/*
 * Device class GUIDs, written out literally so we do not depend on devguid.h /
 * usbiodef.h being present in whatever cross toolchain is in use.
 */
static const GUID XCP_GUID_HIDCLASS =
    { 0x745A17A0, 0x74D3, 0x11D0, { 0xB6, 0xFE, 0x00, 0xA0, 0xC9, 0x0F, 0x57, 0xDA } };
static const GUID XCP_GUID_USB =
    { 0x36FC9E60, 0xC465, 0x11CF, { 0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static int hex_digit(char c, unsigned *out)
{
    if (c >= '0' && c <= '9')      { *out = (unsigned)(c - '0');      return 1; }
    if (c >= 'a' && c <= 'f')      { *out = (unsigned)(c - 'a' + 10); return 1; }
    if (c >= 'A' && c <= 'F')      { *out = (unsigned)(c - 'A' + 10); return 1; }
    return 0;
}

/*
 * Find "VID_1234" / "PID_1234" inside a device instance id, case-insensitively.
 *
 * The instance id looks like
 *   HID\VID_045E&PID_028E&IG_00\8&22264F53&0&0000
 * so the literal tag is three letters followed by an underscore - matching only
 * on the first letter of the tag silently finds nothing at all.
 */
static int parse_hex_tag(const char *s, const char *tag3, uint16_t *out)
{
    const char *p = s;

    while (p[0] && p[1] && p[2] && p[3]) {
        if (upper(p[0]) == tag3[0] && upper(p[1]) == tag3[1] &&
            upper(p[2]) == tag3[2] && p[3] == '_') {
            unsigned value = 0;
            int      i;
            for (i = 0; i < 4; i++) {
                unsigned d;
                if (!hex_digit(p[4 + i], &d)) return 0;
                value = (value << 4) | d;
            }
            *out = (uint16_t)value;
            return 1;
        }
        p++;
    }
    return 0;
}

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);
}

static int enumerate_class(const GUID *guid, const char *group,
                           xcp_dev_entry *out, int max, int count)
{
    HDEVINFO         set;
    SP_DEVINFO_DATA  info;
    DWORD            index;

    if (count >= max) return count;

    set = SetupDiGetClassDevsA(guid, NULL, NULL, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return count;

    for (index = 0; count < max; index++) {
        char  instance[XCP_DEV_INSTANCE_MAX];
        char  desc[XCP_DEV_DESC_MAX];
        DWORD required = 0;
        uint16_t vid = 0, pid = 0;

        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);

        if (!SetupDiEnumDeviceInfo(set, index, &info)) break;

        instance[0] = '\0';
        if (!SetupDiGetDeviceInstanceIdA(set, &info, instance,
                                         sizeof(instance), &required)) {
            continue; /* unnamed device: nothing useful to report */
        }

        desc[0] = '\0';
        SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_DEVICEDESC,
                                          NULL, (PBYTE)desc, sizeof(desc), NULL);

        memset(&out[count], 0, sizeof(out[count]));
        copy_str(out[count].group, XCP_DEV_GROUP_MAX, group);
        copy_str(out[count].instance, XCP_DEV_INSTANCE_MAX, instance);
        copy_str(out[count].desc, XCP_DEV_DESC_MAX, desc);

        if (parse_hex_tag(instance, "VID", &vid) && parse_hex_tag(instance, "PID", &pid)) {
            out[count].vid     = vid;
            out[count].pid     = pid;
            out[count].has_ids = 1;
        }
        count++;
    }

    SetupDiDestroyDeviceInfoList(set);
    return count;
}

int xcp_enumerate_devices(xcp_dev_entry *out, int max)
{
    int count = 0;

    if (!out || max <= 0) return 0;

    /* HID first: that is where a plain HID gamepad would show up. */
    count = enumerate_class(&XCP_GUID_HIDCLASS, "hid", out, max, count);
    /* Then raw USB: an Xbox/GIP pad in XInput mode is bound to the Xbox driver
     * and never appears in the HID class. */
    count = enumerate_class(&XCP_GUID_USB, "usb", out, max, count);
    return count;
}

int xcp_enumerate_available(void)
{
    return 1;
}

#else /* !_WIN32 */

int xcp_enumerate_devices(xcp_dev_entry *out, int max)
{
    (void)out;
    (void)max;
    return -1;
}

int xcp_enumerate_available(void)
{
    return 0;
}

#endif
