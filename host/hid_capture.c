/*
 * host/hid_capture.c - raw HID capture for the PC tool.
 *
 * Windows uses SetupAPI and overlapped I/O; Linux uses /dev/hidraw and poll().
 * Both prefer the Generic Desktop gamepad/joystick usages, write the dump next
 * to the executable and only keep reports that changed (like xcpcapture).
 */
#include "hid_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#define XCP_MAX_CANDIDATES 32
#define XCP_MAX_INTERFACES 256
#define XCP_MAX_REPORT 1024
#define XCP_DEFAULT_SECONDS 30

typedef struct {
    char path[512];
    char product[256];
    unsigned short vid;
    unsigned short pid;
    unsigned short usage_page;
    unsigned short usage;
    unsigned short input_len;
    int gamepad;
} xcp_hid_cand;

static const GUID xcp_hid_guid = {
    0x4D1E55B2, 0xF16F, 0x11CF,
    {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}
};

static HANDLE xcp_hid_open(const char *path, int query_only)
{
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    HANDLE h;

    h = CreateFileA(path,
                    query_only ? 0 : (GENERIC_READ | GENERIC_WRITE),
                    share, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE && !query_only) {
        h = CreateFileA(path, GENERIC_READ, share, NULL,
                        OPEN_EXISTING, 0, NULL);
    }
    return h;
}

static int xcp_hid_is_gamepad(const HIDP_CAPS *caps)
{
    if (caps->UsagePage != 0x01) {
        return 0;
    }
    return caps->Usage == 0x04 || caps->Usage == 0x05 ||
           caps->Usage == 0x08;
}

static int xcp_hid_is_excluded(const HIDP_CAPS *caps)
{
    if (caps->UsagePage == 0x0B || caps->UsagePage == 0x0C ||
        caps->UsagePage == 0x0D) {
        return 1;
    }
    if (caps->UsagePage != 0x01) {
        return 0;
    }
    return caps->Usage == 0x01 || caps->Usage == 0x02 ||
           caps->Usage == 0x06 || caps->Usage == 0x80;
}

static void xcp_hid_utf8(const WCHAR *in, char *out, size_t out_size)
{
    int n;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    n = WideCharToMultiByte(CP_UTF8, 0, in, -1, out, (int)out_size,
                            NULL, NULL);
    if (n <= 0) {
        out[0] = '\0';
    }
}

static int xcp_hid_contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    if (n == 0) {
        return 1;
    }
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i]) {
            char a = hay[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

static int xcp_hid_name_says_gamepad(const char *name)
{
    return xcp_hid_contains_ci(name, "gamepad") ||
           xcp_hid_contains_ci(name, "game pad") ||
           xcp_hid_contains_ci(name, "controller") ||
           xcp_hid_contains_ci(name, "joystick");
}

static int xcp_hid_enumerate(xcp_hid_cand *out, int max)
{
    HDEVINFO set;
    DWORD index;
    int count = 0;

    set = SetupDiGetClassDevsA(&xcp_hid_guid, NULL, NULL,
                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return -1;
    }

    for (index = 0; index < XCP_MAX_INTERFACES; index++) {
        SP_DEVICE_INTERFACE_DATA did;
        SP_DEVINFO_DATA dinfo;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail;
        DWORD need = 0;
        HIDD_ATTRIBUTES attr;
        HIDP_CAPS caps;
        PHIDP_PREPARSED_DATA prep = NULL;
        HANDLE h;
        int have_caps = 0;

        memset(&did, 0, sizeof(did));
        did.cbSize = sizeof(did);
        if (!SetupDiEnumDeviceInterfaces(set, NULL, &xcp_hid_guid, index,
                                         &did)) {
            break;
        }

        (void)SetupDiGetDeviceInterfaceDetailA(set, &did, NULL, 0, &need,
                                               NULL);
        if (need == 0) {
            continue;
        }
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(need);
        if (!detail) {
            break;
        }
        memset(detail, 0, need);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        memset(&dinfo, 0, sizeof(dinfo));
        dinfo.cbSize = sizeof(dinfo);
        if (!SetupDiGetDeviceInterfaceDetailA(set, &did, detail, need, NULL,
                                              &dinfo)) {
            free(detail);
            continue;
        }

        h = xcp_hid_open(detail->DevicePath, 0);
        if (h == INVALID_HANDLE_VALUE) {
            h = xcp_hid_open(detail->DevicePath, 1);
        }
        if (h == INVALID_HANDLE_VALUE) {
            free(detail);
            continue;
        }

        memset(&attr, 0, sizeof(attr));
        attr.Size = sizeof(attr);
        memset(&caps, 0, sizeof(caps));
        if (HidD_GetAttributes(h, &attr) &&
            HidD_GetPreparsedData(h, &prep)) {
            if (HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS) {
                have_caps = 1;
            }
            HidD_FreePreparsedData(prep);
        }

        if (have_caps && caps.InputReportByteLength > 0 &&
            caps.InputReportByteLength <= XCP_MAX_REPORT &&
            !xcp_hid_is_excluded(&caps) && count < max) {
            xcp_hid_cand *c = &out[count];
            WCHAR desc[256];

            memset(c, 0, sizeof(*c));
            snprintf(c->path, sizeof(c->path), "%s", detail->DevicePath);
            desc[0] = 0;
            if (!HidD_GetProductString(h, desc, (ULONG)sizeof(desc)) ||
                desc[0] == 0) {
                (void)SetupDiGetDeviceRegistryPropertyW(
                    set, &dinfo, SPDRP_DEVICEDESC, NULL,
                    (PBYTE)desc, (DWORD)sizeof(desc), NULL);
            }
            xcp_hid_utf8(desc, c->product, sizeof(c->product));
            c->vid = attr.VendorID;
            c->pid = attr.ProductID;
            c->usage_page = caps.UsagePage;
            c->usage = caps.Usage;
            c->input_len = caps.InputReportByteLength;
            c->gamepad = xcp_hid_is_gamepad(&caps) ||
                         xcp_hid_name_says_gamepad(c->product);
            count++;
        }

        CloseHandle(h);
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(set);
    return count;
}

static int xcp_hid_choose(const xcp_hid_cand *list, int n, int known)
{
    char line[64];
    int i;
    int sel;

    if (known && n == 1) {
        return 0;
    }

    if (known) {
        printf("\nControllers found:\n");
    } else {
        printf("\nNo standard gamepad found. Other HID devices:\n");
        printf("Pick your controller only if you recognize it below.\n");
        printf("XInput pads are not exposed as HID; use --live for those.\n");
    }
    for (i = 0; i < n; i++) {
        printf("  %d) %04X:%04X  usage %04X:%04X  %s\n", i + 1,
               list[i].vid, list[i].pid, list[i].usage_page,
               list[i].usage,
               list[i].product[0] ? list[i].product : "(no name)");
    }
    printf("\nSelect [1-%d] (empty = cancel): ", n);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
        return -1;
    }
    sel = atoi(line);
    if (sel < 1 || sel > n) {
        return -1;
    }
    return sel - 1;
}

static int xcp_hid_read(HANDLE h, HANDLE ev, unsigned char *buf, DWORD len,
                        DWORD *got, DWORD timeout)
{
    OVERLAPPED ov;
    DWORD err;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;
    *got = 0;

    if (!ReadFile(h, buf, len, got, &ov)) {
        err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            return -1;
        }
        if (WaitForSingleObject(ev, timeout) != WAIT_OBJECT_0) {
            (void)CancelIoEx(h, &ov);
            (void)WaitForSingleObject(ev, INFINITE);
            return 0;
        }
        if (!GetOverlappedResult(h, &ov, got, FALSE)) {
            err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) {
                return 0;
            }
            return -1;
        }
    }

    return *got > 0 ? 1 : 0;
}

static void xcp_hid_write_line(FILE *f, unsigned seq,
                               const unsigned char *buf, DWORD len)
{
    DWORD i;

    printf("%04u ", seq);
    fprintf(f, "%04u ", seq);
    for (i = 0; i < len; i++) {
        printf(" %02X", buf[i]);
        fprintf(f, " %02X", buf[i]);
    }
    printf("\n");
    fprintf(f, "\n");
}

static void xcp_hid_dump_caps(FILE *f, PHIDP_PREPARSED_DATA prep,
                              const HIDP_CAPS *caps)
{
    PHIDP_BUTTON_CAPS buttons;
    PHIDP_VALUE_CAPS values;
    USHORT len;
    USHORT i;

    fprintf(f, "#\n# HID capabilities\n");
    fprintf(f, "# output_report_length: %u\n",
            (unsigned)caps->OutputReportByteLength);
    fprintf(f, "# feature_report_length: %u\n",
            (unsigned)caps->FeatureReportByteLength);
    fprintf(f, "# input_button_collections: %u\n",
            (unsigned)caps->NumberInputButtonCaps);
    fprintf(f, "# input_value_collections: %u\n",
            (unsigned)caps->NumberInputValueCaps);

    if (caps->NumberInputButtonCaps > 0) {
        len = caps->NumberInputButtonCaps;
        buttons = (PHIDP_BUTTON_CAPS)calloc(len, sizeof(*buttons));
        if (buttons &&
            HidP_GetButtonCaps(HidP_Input, buttons, &len, prep) ==
                HIDP_STATUS_SUCCESS) {
            for (i = 0; i < len; i++) {
                USAGE usage_min = buttons[i].IsRange ?
                    buttons[i].Range.UsageMin : buttons[i].NotRange.Usage;
                USAGE usage_max = buttons[i].IsRange ?
                    buttons[i].Range.UsageMax : buttons[i].NotRange.Usage;
                fprintf(f,
                        "# button: page=0x%02X usage=0x%02X..0x%02X "
                        "report=0x%02X bitfield=%u\n",
                        (unsigned)buttons[i].UsagePage,
                        (unsigned)usage_min, (unsigned)usage_max,
                        (unsigned)buttons[i].ReportID,
                        (unsigned)buttons[i].BitField);
            }
        }
        free(buttons);
    }

    if (caps->NumberInputValueCaps > 0) {
        len = caps->NumberInputValueCaps;
        values = (PHIDP_VALUE_CAPS)calloc(len, sizeof(*values));
        if (values &&
            HidP_GetValueCaps(HidP_Input, values, &len, prep) ==
                HIDP_STATUS_SUCCESS) {
            for (i = 0; i < len; i++) {
                USAGE usage_min = values[i].IsRange ?
                    values[i].Range.UsageMin : values[i].NotRange.Usage;
                USAGE usage_max = values[i].IsRange ?
                    values[i].Range.UsageMax : values[i].NotRange.Usage;
                fprintf(f,
                        "# value: page=0x%02X usage=0x%02X..0x%02X "
                        "report=0x%02X bits=%u count=%u "
                        "logical=%ld..%ld physical=%ld..%ld abs=%u\n",
                        (unsigned)values[i].UsagePage,
                        (unsigned)usage_min, (unsigned)usage_max,
                        (unsigned)values[i].ReportID,
                        (unsigned)values[i].BitSize,
                        (unsigned)values[i].ReportCount,
                        (long)values[i].LogicalMin,
                        (long)values[i].LogicalMax,
                        (long)values[i].PhysicalMin,
                        (long)values[i].PhysicalMax,
                        (unsigned)values[i].IsAbsolute);
            }
        }
        free(values);
    }
}

static void xcp_hid_output_path(const char *dir, const xcp_hid_cand *c,
                                char *out, size_t size)
{
    int n;

    snprintf(out, size, "%s\\AnyController-capture-%04X-%04X.txt",
             dir, (unsigned)c->vid, (unsigned)c->pid);
    if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    for (n = 2; n < 100; n++) {
        snprintf(out, size,
                 "%s\\AnyController-capture-%04X-%04X-%d.txt",
                 dir, (unsigned)c->vid, (unsigned)c->pid, n);
        if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) {
            return;
        }
    }
}

static int xcp_hid_capture(const xcp_hid_cand *c, int seconds,
                           char *out_path, size_t out_path_size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD len;
    DWORD start;
    DWORD deadline;
    HANDLE h;
    HANDLE ev;
    unsigned char *buf;
    unsigned char *last;
    DWORD last_len = 0;
    unsigned seq = 0;
    int ret = 1;
    FILE *f;

    if (!out_path || out_path_size == 0) {
        return 1;
    }
    if (seconds <= 0) {
        seconds = XCP_DEFAULT_SECONDS;
    }
    len = c->input_len;
    if (len == 0 || len > XCP_MAX_REPORT) {
        printf("Unsupported input report length (%u).\n",
               (unsigned)c->input_len);
        return 1;
    }

    if (GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe)) == 0) {
        printf("Could not locate the executable folder.\n");
        return 1;
    }
    slash = strrchr(exe, '\\');
    if (slash) {
        *slash = '\0';
    }
    xcp_hid_output_path(exe, c, out_path, out_path_size);

    h = CreateFileA(c->path, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        h = CreateFileA(c->path, GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    }
    if (h == INVALID_HANDLE_VALUE) {
        printf("Could not open the controller (error %lu).\n",
               (unsigned long)GetLastError());
        printf("Close Steam, DS4Windows or anything else using it and retry.\n");
        return 1;
    }

    ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    buf = (unsigned char *)malloc(len);
    last = (unsigned char *)malloc(len);
    if (!ev || !buf || !last) {
        printf("Out of memory.\n");
        goto done;
    }

    f = fopen(out_path, "wb");
    if (!f) {
        printf("Could not create %s\n", out_path);
        goto done;
    }

    fprintf(f, "# AnyController-Inspector capture\n");
    fprintf(f, "# device: %s\n",
            c->product[0] ? c->product : "(no name)");
    fprintf(f, "# vid: 0x%04X\n", (unsigned)c->vid);
    fprintf(f, "# pid: 0x%04X\n", (unsigned)c->pid);
    fprintf(f, "# usage_page: 0x%02X\n", (unsigned)c->usage_page);
    fprintf(f, "# usage: 0x%02X\n", (unsigned)c->usage);
    fprintf(f, "# input_report_length: %u\n", (unsigned)c->input_len);
    fprintf(f, "# path: %s\n", c->path);
    {
        PHIDP_PREPARSED_DATA prep = NULL;
        HIDP_CAPS caps;

        memset(&caps, 0, sizeof(caps));
        if (HidD_GetPreparsedData(h, &prep)) {
            if (HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS) {
                xcp_hid_dump_caps(f, prep, &caps);
            }
            HidD_FreePreparsedData(prep);
        }
    }
    fprintf(f, "#\n");
    fflush(f);

    printf("\nController: %s (%04X:%04X, usage %04X:%04X)\n",
           c->product[0] ? c->product : "(no name)", (unsigned)c->vid,
           (unsigned)c->pid, (unsigned)c->usage_page, (unsigned)c->usage);
    printf("Capturing for %d s. Press every button and move both sticks.\n",
           seconds);
    printf("Only changes are written. Do not unplug the controller.\n\n");
    fflush(stdout);

    start = GetTickCount();
    deadline = (DWORD)seconds * 1000u;

    while ((DWORD)(GetTickCount() - start) < deadline) {
        DWORD got = 0;
        int r = xcp_hid_read(h, ev, buf, len, &got, 200);

        if (r < 0) {
            printf("\nLost the controller (error %lu).\n",
                   (unsigned long)GetLastError());
            break;
        }
        if (r == 0) {
            continue;
        }
        if (got > len) {
            got = len;
        }
        if (seq > 0 && got == last_len && memcmp(buf, last, got) == 0) {
            continue;
        }

        seq++;
        xcp_hid_write_line(f, seq, buf, got);
        fflush(f);
        last_len = got;
        memcpy(last, buf, got);

        if (seq >= 2000) {
            printf("\nReached the capture limit.\n");
            break;
        }
    }

    fclose(f);

    if (seq == 0) {
        remove(out_path);
        printf("\nNo input reports arrived from %s.\n", c->path);
        printf("The HID interface was readable, but the device did not stream.\n");
        printf("Xbox pads in XInput/GIP mode are usually owned by their kernel\n");
        printf("driver; capture from /data/AnyController.log on the console.\n");
        goto done;
    }

    ret = 0;

done:
    if (ev) {
        CloseHandle(ev);
    }
    free(buf);
    free(last);
    CloseHandle(h);

    if (ret == 0) {
        printf("\nSaved: %s\n", out_path);
        printf("Send this file together with the controller model.\n");
    }
    fflush(stdout);
    return ret;
}

int xcp_hid_capture_available(void)
{
    return 1;
}

int xcp_hid_capture_auto(int seconds, char *out_path, size_t out_path_size)
{
    xcp_hid_cand list[XCP_MAX_CANDIDATES];
    int total;
    int i;
    int n;
    int known;
    int pick;

    printf("AnyController-Inspector: scanning for a controller...\n");
    total = xcp_hid_enumerate(list, XCP_MAX_CANDIDATES);
    if (total < 0) {
        printf("Could not enumerate HID devices.\n");
        return 1;
    }
    if (total == 0) {
        printf("No controller found.\n");
        printf("Connect it wired, select the same mode you use on the PS4\n");
        printf("(DirectInput, Switch...) and run this again.\n");
        printf("XInput pads are not exposed as HID; use --live for those.\n");
        return 1;
    }

    n = 0;
    for (i = 0; i < total; i++) {
        if (list[i].gamepad) {
            list[n++] = list[i];
        }
    }
    known = n > 0;
    if (n == 0) {
        n = total;
    }

    pick = xcp_hid_choose(list, n, known);
    if (pick < 0) {
        printf("No controller selected.\n");
        return 1;
    }
    return xcp_hid_capture(&list[pick], seconds, out_path, out_path_size);
}

#elif defined(__linux__)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <linux/hidraw.h>

#ifndef HID_MAX_DESCRIPTOR_SIZE
#define HID_MAX_DESCRIPTOR_SIZE 4096
#endif

#define XCP_MAX_CANDIDATES 32
#define XCP_MAX_REPORT    1024
#define XCP_DEFAULT_SECONDS 30

typedef struct {
    char path[512];
    char product[256];
    unsigned short vid;
    unsigned short pid;
    unsigned short usage_page;
    unsigned short usage;
    unsigned short input_len;
    int gamepad;
    int readable;
} xcp_hid_cand;

static int xcp_linux_contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    if (n == 0) {
        return 1;
    }
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i]) {
            char a = hay[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

static int xcp_linux_name_looks_gamepad(const char *name)
{
    static const char *needles[] = {
        "gamepad", "game pad", "controller", "joystick", "dualsense",
        "dualshock", "xbox", "switch", "joy-con", "pro controller",
        "8bitdo", "pdp", "hori", "powera", "steam controller", "steam deck",
        NULL
    };
    unsigned i;

    for (i = 0; needles[i]; i++) {
        if (xcp_linux_contains_ci(name, needles[i])) {
            return 1;
        }
    }
    return 0;
}

static unsigned xcp_linux_item_value(const unsigned char *p, unsigned size)
{
    unsigned v = 0;
    unsigned i;

    for (i = 0; i < size; i++) {
        v |= (unsigned)p[i] << (8u * i);
    }
    return v;
}

/*
 * A HID report descriptor starts with the application collection. Look for
 * the Generic Desktop gamepad/joystick/multi-axis usage so a mouse that also
 * has buttons is not mistaken for a controller.
 */
static int xcp_linux_descriptor_is_gamepad(int fd,
                                           unsigned short *usage_page,
                                           unsigned short *usage)
{
    struct hidraw_report_descriptor desc;
    int                          size = 0;
    size_t                       i = 0;
    unsigned                     page = 0;
    unsigned                     local_usage = 0;
    int                          have_usage = 0;

    if (ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 ||
        size <= 0 || size > HID_MAX_DESCRIPTOR_SIZE) {
        return 0;
    }
    memset(&desc, 0, sizeof(desc));
    desc.size = (__u32)size;
    if (ioctl(fd, HIDIOCGRDESC, &desc) < 0) {
        return 0;
    }

    while (i < (size_t)desc.size) {
        unsigned prefix = desc.value[i++];
        unsigned item_size;
        unsigned type;
        unsigned tag;
        unsigned value;

        if (prefix == 0xFEu) { /* long item: size, tag, data */
            if (i + 2u > (size_t)desc.size) {
                break;
            }
            item_size = desc.value[i++];
            i++; /* tag */
            if (i + item_size > (size_t)desc.size) {
                break;
            }
            i += item_size;
            continue;
        }

        item_size = prefix & 0x03u;
        if (item_size == 3u) {
            item_size = 4u;
        }
        if (i + item_size > (size_t)desc.size) {
            break;
        }
        type  = (prefix >> 2) & 0x03u;
        tag   = (prefix >> 4) & 0x0Fu;
        value = xcp_linux_item_value(desc.value + i, item_size);
        i += item_size;

        if (type == 1u) { /* global */
            if (tag == 0x0u) {
                page = value;
            }
        } else if (type == 2u) { /* local */
            if (tag == 0x0u) {
                local_usage = value;
                have_usage = 1;
            }
        } else if (type == 0u) { /* main */
            if (tag == 0xAu && value == 0x01u && have_usage) {
                if (page == 0x01u &&
                    (local_usage == 0x04u || local_usage == 0x05u ||
                     local_usage == 0x08u)) {
                    if (usage_page) {
                        *usage_page = (unsigned short)page;
                    }
                    if (usage) {
                        *usage = (unsigned short)local_usage;
                    }
                    return 1;
                }
            }
            have_usage = 0;
            local_usage = 0;
        }
    }
    return 0;
}

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

static int xcp_linux_read_uevent(const char *path, char *name, size_t name_size,
                                 unsigned short *vid, unsigned short *pid)
{
    FILE *f;
    char  line[512];
    int   have_id = 0;

    f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    if (name && name_size) {
        name[0] = '\0';
    }
    *vid = 0;
    *pid = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "HID_NAME=", 9) == 0) {
            if (name && name_size) {
                snprintf(name, name_size, "%s", line + 9);
                xcp_linux_trim(name);
            }
        } else if (strncmp(line, "HID_ID=", 7) == 0) {
            const char *c1 = strchr(line + 7, ':');
            const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
            unsigned long v;
            unsigned long p;

            if (!c1 || !c2) {
                continue;
            }
            v = strtoul(c1 + 1, NULL, 16);
            p = strtoul(c2 + 1, NULL, 16);
            if (v > 0xFFFFul || p > 0xFFFFul) {
                continue;
            }
            *vid = (unsigned short)v;
            *pid = (unsigned short)p;
            have_id = 1;
        }
    }
    fclose(f);
    return have_id;
}

static int xcp_linux_enumerate(xcp_hid_cand *out, int max)
{
    DIR           *dir;
    struct dirent *ent;
    int            n = 0;

    dir = opendir("/sys/class/hidraw");
    if (!dir) {
        return -1;
    }

    while ((ent = readdir(dir)) != NULL && n < max) {
        char           uevent[PATH_MAX];
        char           name[256];
        char           dev_path[64];
        unsigned short vid = 0;
        unsigned short pid = 0;
        unsigned short up = 0;
        unsigned short us = 0;
        int            fd;
        int            gamepad = 0;

        if (strncmp(ent->d_name, "hidraw", 6) != 0) {
            continue;
        }
        snprintf(uevent, sizeof(uevent),
                 "/sys/class/hidraw/%s/device/uevent", ent->d_name);
        if (!xcp_linux_read_uevent(uevent, name, sizeof(name), &vid, &pid)) {
            continue;
        }
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", ent->d_name);

        fd = open(dev_path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            gamepad = xcp_linux_descriptor_is_gamepad(fd, &up, &us);
            close(fd);
        }
        if (!gamepad && xcp_linux_name_looks_gamepad(name)) {
            gamepad = 1;
        }

        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].path, sizeof(out[n].path), "%s", dev_path);
        snprintf(out[n].product, sizeof(out[n].product), "%s",
                 name[0] ? name : "(no name)");
        out[n].vid = vid;
        out[n].pid = pid;
        out[n].usage_page = up;
        out[n].usage = us;
        out[n].input_len = 0;
        out[n].gamepad = gamepad;
        out[n].readable = (fd >= 0);
        n++;
    }
    closedir(dir);
    return n;
}

static int xcp_linux_choose(xcp_hid_cand *list, int n)
{
    char line[64];
    int  i;
    int  sel;

    if (n == 1) {
        return 0;
    }

    printf("\nControllers found:\n");
    for (i = 0; i < n; i++) {
        printf("  %d) %04X:%04X  %s  (%s)%s\n", i + 1,
               (unsigned)list[i].vid, (unsigned)list[i].pid,
               list[i].product[0] ? list[i].product : "(no name)",
               list[i].path, list[i].readable ? "" : "  [no permission]");
    }
    printf("\nSelect [1-%d] (empty = cancel): ", n);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
        return -1;
    }
    sel = atoi(line);
    if (sel < 1 || sel > n) {
        return -1;
    }
    return sel - 1;
}

static void xcp_linux_exe_dir(char *out, size_t size)
{
    ssize_t n;

    if (!out || size == 0) {
        return;
    }
    n = readlink("/proc/self/exe", out, size - 1);
    if (n <= 0) {
        snprintf(out, size, ".");
        return;
    }
    out[n] = '\0';
    {
        char *slash = strrchr(out, '/');
        if (slash && slash != out) {
            *slash = '\0';
        } else {
            snprintf(out, size, ".");
        }
    }
}

static void xcp_linux_output_path(const char *dir, const xcp_hid_cand *c,
                                  char *out, size_t size)
{
    int n;

    snprintf(out, size, "%s/AnyController-capture-%04X-%04X.txt",
             dir, (unsigned)c->vid, (unsigned)c->pid);
    if (access(out, F_OK) != 0) {
        return;
    }
    for (n = 2; n < 100; n++) {
        snprintf(out, size,
                 "%s/AnyController-capture-%04X-%04X-%d.txt",
                 dir, (unsigned)c->vid, (unsigned)c->pid, n);
        if (access(out, F_OK) != 0) {
            return;
        }
    }
}

static void xcp_linux_write_line(FILE *f, unsigned seq,
                                 const unsigned char *buf, size_t len)
{
    size_t i;

    printf("%04u ", seq);
    fprintf(f, "%04u ", seq);
    for (i = 0; i < len; i++) {
        printf(" %02X", buf[i]);
        fprintf(f, " %02X", buf[i]);
    }
    printf("\n");
    fprintf(f, "\n");
}

static int xcp_hid_capture(const xcp_hid_cand *c, int seconds,
                           char *out_path, size_t out_path_size)
{
    int            fd;
    FILE          *f;
    struct pollfd  pfd;
    unsigned char  buf[XCP_MAX_REPORT];
    unsigned char  last[XCP_MAX_REPORT];
    size_t         last_len = 0;
    unsigned       seq = 0;
    time_t         start;
    char           dir[PATH_MAX];
    long           len_pos;

    if (!out_path || out_path_size == 0) {
        return 1;
    }
    if (seconds <= 0) {
        seconds = XCP_DEFAULT_SECONDS;
    }
    if (!c->readable) {
        printf("\nNo permission to read %s.\n", c->path);
        printf("Run ./setup-linux.sh once, or run this tool with sudo.\n");
        return 1;
    }

    fd = open(c->path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("\nCould not open %s: %s\n", c->path, strerror(errno));
        printf("Run ./setup-linux.sh once, or run this tool with sudo.\n");
        return 1;
    }

    xcp_linux_exe_dir(dir, sizeof(dir));
    if (access(dir, W_OK) != 0) {
        snprintf(dir, sizeof(dir), ".");
    }
    xcp_linux_output_path(dir, c, out_path, out_path_size);

    f = fopen(out_path, "wb");
    if (!f) {
        printf("\nCould not create %s: %s\n", out_path, strerror(errno));
        close(fd);
        return 1;
    }

    fprintf(f, "# AnyController-Inspector capture\n");
    fprintf(f, "# device: %s\n",
            c->product[0] ? c->product : "(no name)");
    fprintf(f, "# vid: 0x%04X\n", (unsigned)c->vid);
    fprintf(f, "# pid: 0x%04X\n", (unsigned)c->pid);
    fprintf(f, "# usage_page: 0x%02X\n", (unsigned)c->usage_page);
    fprintf(f, "# usage: 0x%02X\n", (unsigned)c->usage);
    len_pos = ftell(f);
    fprintf(f, "# input_report_length: %5u\n", 0u);
    fprintf(f, "# path: %s\n", c->path);
    fprintf(f, "# os: linux\n");
    fprintf(f, "#\n");
    fflush(f);

    printf("\nController: %s (%04X:%04X)\n",
           c->product[0] ? c->product : "(no name)",
           (unsigned)c->vid, (unsigned)c->pid);
    printf("Capturing for %d s. Press every button and move both sticks.\n",
           seconds);
    printf("Only changes are written. Do not unplug the controller.\n\n");
    fflush(stdout);

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    start = time(NULL);

    while (time(NULL) - start < (time_t)seconds) {
        int pr = poll(&pfd, 1, 200);
        ssize_t got;

        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            printf("\nLost the controller: %s\n", strerror(errno));
            break;
        }
        if (pr == 0) {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            printf("\nLost the controller.\n");
            break;
        }
        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        got = read(fd, buf, sizeof(buf));
        if (got < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            printf("\nLost the controller: %s\n", strerror(errno));
            break;
        }
        if (got == 0) {
            continue;
        }
        if (seq > 0 && (size_t)got == last_len &&
            memcmp(buf, last, (size_t)got) == 0) {
            continue;
        }

        seq++;
        xcp_linux_write_line(f, seq, buf, (size_t)got);
        last_len = (size_t)got;
        memcpy(last, buf, last_len);
        if (seq == 1) {
            long end = ftell(f);
            if (len_pos >= 0 && end >= 0 &&
                fseek(f, len_pos, SEEK_SET) == 0) {
                fprintf(f, "# input_report_length: %5u\n",
                        (unsigned)last_len);
                (void)fseek(f, end, SEEK_SET);
            }
        }
        fflush(f);

        if (seq >= 2000) {
            printf("\nReached the capture limit.\n");
            break;
        }
    }

    fclose(f);
    close(fd);

    if (seq == 0) {
        remove(out_path);
        printf("\nNo input reports arrived from %s.\n", c->path);
        printf("The descriptor was readable, but the device did not stream.\n");
        printf("Some pads expose only a vendor/control interface on hidraw;\n");
        printf("others (Xbox XInput/GIP) are owned by their kernel driver.\n");
        printf("For those, capture from /data/AnyController.log on the console\n");
        printf("and decode that payload log with --play instead.\n");
        fflush(stdout);
        return 1;
    }

    printf("\nSaved: %s\n", out_path);
    printf("Send this file together with the controller model.\n");
    fflush(stdout);
    return 0;
}

int xcp_hid_capture_available(void)
{
    return access("/sys/class/hidraw", F_OK) == 0 ? 1 : 0;
}

int xcp_hid_capture_auto(int seconds, char *out_path, size_t out_path_size)
{
    xcp_hid_cand list[XCP_MAX_CANDIDATES];
    xcp_hid_cand gamepads[XCP_MAX_CANDIDATES];
    int          total;
    int          i;
    int          n = 0;
    int          pick;

    printf("AnyController-Inspector: scanning for a controller...\n");
    total = xcp_linux_enumerate(list, XCP_MAX_CANDIDATES);
    if (total < 0) {
        printf("Could not read /sys/class/hidraw.\n");
        printf("Is this a Linux system with HID support?\n");
        return 1;
    }
    if (total == 0) {
        printf("No HID devices found.\n");
        printf("If a controller is plugged in:\n");
        printf("  * DualSense / DualShock / Switch: check /dev/hidraw* and run\n");
        printf("    ./setup-linux.sh once (or run this tool with sudo).\n");
        printf("  * Xbox 360/One/Series in XInput/GIP mode: these usually do\n");
        printf("    not expose /dev/hidraw. Capture from /data/AnyController.log\n");
        printf("    on the console and decode it with --play instead.\n");
        return 1;
    }

    for (i = 0; i < total; i++) {
        if (list[i].gamepad) {
            gamepads[n++] = list[i];
        }
    }

    if (n == 0) {
        printf("Found %d HID device(s), but none look like a gamepad:\n", total);
        for (i = 0; i < total; i++) {
            printf("  %04X:%04X  %s  (%s)\n",
                   (unsigned)list[i].vid, (unsigned)list[i].pid,
                   list[i].product[0] ? list[i].product : "(no name)",
                   list[i].path);
        }
        printf("\nIf your controller is listed above, it is probably a\n");
        printf("permission problem: run ./setup-linux.sh once, or sudo.\n");
        printf("Xbox pads in XInput/GIP mode do not expose hidraw; use the\n");
        printf("payload log for those. Run --list for the full device list.\n");
        return 1;
    }

    pick = xcp_linux_choose(gamepads, n);
    if (pick < 0) {
        printf("No controller selected.\n");
        return 1;
    }
    return xcp_hid_capture(&gamepads[pick], seconds, out_path, out_path_size);
}

#else /* other platforms */

#include <stdio.h>

int xcp_hid_capture_available(void)
{
    return 0;
}

int xcp_hid_capture_auto(int seconds, char *out_path, size_t out_path_size)
{
    (void)seconds;
    (void)out_path;
    (void)out_path_size;
    printf("Raw HID capture is not available on this platform.\n");
    printf("Use --play with a capture or a payload log instead.\n");
    return 1;
}

#endif
