/*
 * tests/test_klog_line.c - the kernel-log line that names a virtual pad.
 *
 * The real format, as logged on the console:
 *
 *   #LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x7030d][type:1][subType:2]
 *
 * Getting this wrong means addressing a device we do not own, so the negative
 * cases matter more than the positive one.
 */
#include "framework.h"

#include "xcp/klog_line.h"

#include <string.h>

static const char *const k_pad_added =
    "#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED "
    "[DeviceId:0x7030d][type:1][subType:2]";

XCP_TEST(klog_accepts_the_virtual_pad_line)
{
    XCP_CHECK(xcp_klog_is_virtual_pad_added(k_pad_added));
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id(k_pad_added), 0x7030d);
}

XCP_TEST(klog_accepts_the_equals_spelling)
{
    const char *line = "...SCE_MBUS_EVENT_DEVICE_ADDED[DeviceId:0x1ff][type=1][subType=2]";

    XCP_CHECK(xcp_klog_is_virtual_pad_added(line));
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id(line), 0x1ff);
}

XCP_TEST(klog_rejects_a_real_pad_being_plugged_in)
{
    /* subType 0/1: a physical device. Handing its DeviceId to InsertData would
     * send every button to somebody else's controller. */
    const char *line = "#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED "
                       "[DeviceId:0x1234][type:1][subType:0]";
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(line));

    /* Right event, no subType at all. */
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(
        "SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x1234][type:1]"));

    /* A headset is type 2, even if it somehow claims subType 2. */
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(
        "SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x1234][type:2][subType:2]"));

    /* A physical pad re-registering on its own port has a zero low byte. */
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(
        "SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x30300][type:1][subType:2]"));
}

XCP_TEST(klog_rejects_everything_else)
{
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(NULL));
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(""));
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(
        "SCE_MBUS_EVENT_DEVICE_REMOVED [DeviceId:0x7030d][type:1][subType:2]"));
    /* Our own mirrored log line must never look like the event. */
    XCP_CHECK_FALSE(xcp_klog_is_virtual_pad_added(
        "xcp: waiting for SCE_MBUS_EVENT_DEVICE_ADDED subType:2"));
}

XCP_TEST(klog_device_id_tolerates_spacing_and_case)
{
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id("no id here"), 0);
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id(NULL), 0);
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id("DeviceId"), 0);
    /* No hex digit after the key: not an id. */
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id("DeviceId:zz"), 0);
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id("DeviceId = 0xABC"), 0xabc);
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id("DeviceId 1234"), 0x1234);
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id("deviceID:0X10"), 0x10);
}

XCP_TEST(klog_device_id_uses_the_first_spelling_it_finds)
{
    /* The line carries one id; if a future log adds a second key we still take
     * the id that belongs to the event we matched. */
    XCP_CHECK_EQ_INT((int)xcp_klog_device_id("[DeviceId:0x7030d][type:1]"), 0x7030d);
}

void xcp_register_klog_line_tests(void)
{
    XCP_RUN(klog_accepts_the_virtual_pad_line);
    XCP_RUN(klog_accepts_the_equals_spelling);
    XCP_RUN(klog_rejects_a_real_pad_being_plugged_in);
    XCP_RUN(klog_rejects_everything_else);
    XCP_RUN(klog_device_id_tolerates_spacing_and_case);
    XCP_RUN(klog_device_id_uses_the_first_spelling_it_finds);
}
