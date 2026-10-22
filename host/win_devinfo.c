/*
 * host/win_devinfo.c - see win_devinfo.h.
 *
 * Read-only: opening sysfs/registry keys, never a device. Requires no
 * administrator rights.
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

#elif defined(__linux__)

#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

static void xcp_linux_trim(char *s)
{
    size_t n = strlen(s);

    while (n > 0) {
        char c = s[n - 1];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') {
            break;
        }
        s[--n] = '\0';
    }
}

static int xcp_linux_read_text(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "r");

    if (!f || !out || size == 0) {
        if (f) fclose(f);
        return 0;
    }
    if (!fgets(out, (int)size, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    xcp_linux_trim(out);
    return 1;
}

static int xcp_linux_read_hex(const char *path, unsigned *out)
{
    char          text[32];
    char         *end = NULL;
    unsigned long v;

    if (!xcp_linux_read_text(path, text, sizeof(text))) {
        return 0;
    }
    v = strtoul(text, &end, 16);
    if (end == text || v > 0xFFFFul) {
        return 0;
    }
    *out = (unsigned)v;
    return 1;
}

static void xcp_linux_entry(xcp_dev_entry *e, const char *group,
                            const char *desc, const char *instance,
                            unsigned vid, unsigned pid, int has_ids)
{
    memset(e, 0, sizeof(*e));
    snprintf(e->group, sizeof(e->group), "%s", group);
    snprintf(e->desc, sizeof(e->desc), "%s", desc);
    snprintf(e->instance, sizeof(e->instance), "%s", instance);
    e->vid = (uint16_t)vid;
    e->pid = (uint16_t)pid;
    e->has_ids = has_ids;
}

int xcp_enumerate_devices(xcp_dev_entry *out, int max)
{
    DIR           *dir;
    struct dirent *ent;
    int            count = 0;

    if (!out || max <= 0) {
        return 0;
    }

    dir = opendir("/sys/class/hidraw");
    if (dir) {
        while ((ent = readdir(dir)) != NULL && count < max) {
            char     uevent[PATH_MAX];
            char     line[512];
            char     name[XCP_DEV_DESC_MAX] = "";
            unsigned vid = 0;
            unsigned pid = 0;
            int      have_id = 0;
            FILE    *f;

            if (strncmp(ent->d_name, "hidraw", 6) != 0) {
                continue;
            }
            snprintf(uevent, sizeof(uevent),
                     "/sys/class/hidraw/%s/device/uevent", ent->d_name);
            f = fopen(uevent, "r");
            if (!f) {
                continue;
            }
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "HID_NAME=", 9) == 0) {
                    snprintf(name, sizeof(name), "%s", line + 9);
                    xcp_linux_trim(name);
                } else if (strncmp(line, "HID_ID=", 7) == 0) {
                    const char *c1 = strchr(line + 7, ':');
                    const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;

                    if (c1 && c2) {
                        vid = (unsigned)strtoul(c1 + 1, NULL, 16);
                        pid = (unsigned)strtoul(c2 + 1, NULL, 16);
                        have_id = 1;
                    }
                }
            }
            fclose(f);
            if (!have_id) {
                continue;
            }

            {
                char instance[64];
                char desc[XCP_DEV_DESC_MAX];

                snprintf(instance, sizeof(instance), "/dev/%s", ent->d_name);
                snprintf(desc, sizeof(desc), "%s",
                         name[0] ? name : "(no name)");
                xcp_linux_entry(&out[count], "hidraw", desc, instance,
                                vid, pid, 1);
                count++;
            }
        }
        closedir(dir);
    }

    dir = opendir("/sys/bus/usb/devices");
    if (dir) {
        while ((ent = readdir(dir)) != NULL && count < max) {
            char     path[PATH_MAX];
            char     product[XCP_DEV_DESC_MAX] = "";
            char     manufacturer[XCP_DEV_DESC_MAX] = "";
            unsigned vid = 0;
            unsigned pid = 0;

            if (ent->d_name[0] == '.') {
                continue;
            }
            snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor",
                     ent->d_name);
            if (!xcp_linux_read_hex(path, &vid)) {
                continue;
            }
            snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct",
                     ent->d_name);
            if (!xcp_linux_read_hex(path, &pid)) {
                continue;
            }
            snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/product",
                     ent->d_name);
            (void)xcp_linux_read_text(path, product, sizeof(product));
            snprintf(path, sizeof(path),
                     "/sys/bus/usb/devices/%s/manufacturer",
                     ent->d_name);
            (void)xcp_linux_read_text(path, manufacturer,
                                      sizeof(manufacturer));

            {
                char desc[XCP_DEV_DESC_MAX];

                if (product[0]) {
                    snprintf(desc, sizeof(desc), "%s", product);
                } else if (manufacturer[0]) {
                    snprintf(desc, sizeof(desc), "%s", manufacturer);
                } else {
                    snprintf(desc, sizeof(desc), "USB device");
                }
                xcp_linux_entry(&out[count], "usb", desc, ent->d_name,
                                vid, pid, 1);
                count++;
            }
        }
        closedir(dir);
    }

    return count;
}

int xcp_enumerate_available(void)
{
    return 1;
}

#else /* other platforms */

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
