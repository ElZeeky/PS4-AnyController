/*
 * core/src/klog_line.c - see xcp/klog_line.h.
 *
 * Written from the log format as observed on the console, not copied from any
 * project: the event name, the "subType:2" marker and the DeviceId spelling are
 * facts about the log, and everything below is plain string work.
 */
#include "xcp/klog_line.h"

#include <ctype.h>
#include <string.h>

/* The login manager's event for a device appearing on the MBus. */
#define XCP_KLOG_EVENT "SCE_MBUS_EVENT_DEVICE_ADDED"

/* subType 2 is the Remote Play pad, which is what a virtual device is. */
#define XCP_KLOG_SUBTYPE_COLON "[subType:2]"
#define XCP_KLOG_SUBTYPE_EQUALS "subType=2"

int xcp_klog_is_virtual_pad_added(const char *line)
{
    if (!line) return 0;
    if (!strstr(line, XCP_KLOG_EVENT)) return 0;

    if (strstr(line, XCP_KLOG_SUBTYPE_COLON)) return 1;
    if (strstr(line, XCP_KLOG_SUBTYPE_EQUALS)) return 1;
    return 0;
}

/*
 * Read a hex number that follows `key`. Accepts `DeviceId:0x7030d`,
 * `DeviceId = 0x7030d` and `DeviceId 7030d`. Returns 0 when the key is absent or
 * is not followed by a hex digit.
 */
static uint64_t xcp_hex_after(const char *line, const char *key)
{
    const char *p;
    uint64_t    value = 0;

    p = strstr(line, key);
    if (!p) return 0;

    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!isxdigit((unsigned char)*p)) return 0;

    while (isxdigit((unsigned char)*p)) {
        char c = *p++;
        value <<= 4;
        if (c >= '0' && c <= '9')      value |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') value |= (uint64_t)(10 + c - 'a');
        else                           value |= (uint64_t)(10 + c - 'A');
    }
    return value;
}

uint64_t xcp_klog_device_id(const char *line)
{
    static const char *keys[] = { "DeviceId", "DeviceID", "deviceId", "deviceID" };
    unsigned i;

    if (!line) return 0;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        uint64_t id = xcp_hex_after(line, keys[i]);
        if (id) return id;
    }
    return 0;
}
