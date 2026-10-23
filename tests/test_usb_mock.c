/*
 * tests/test_usb_mock.c - the backend interface itself.
 *
 * These pin the contract every backend must honour, so a new backend (hidapi,
 * sceUsbd) has something concrete to conform to.
 */
#include "framework.h"

#include "xcp/usb_backend.h"

#include <string.h>

XCP_TEST(mock_enumerates_and_classifies_a_xbox_pad)
{
    xcp_usb_backend     *b = xcp_usb_mock_backend();
    xcp_usb_device_info  list[8];
    xcp_usb_device_info  found;
    int                  n;
    int                  i;

    xcp_usb_mock_reset();
    xcp_usb_mock_set_device(0x045E, 0x02EA);
    XCP_CHECK_EQ_INT(b->init(NULL), XCP_USB_OK);

    n = b->enumerate(NULL, list, 8);
    XCP_CHECK_EQ_INT(n, 1);
    XCP_CHECK_EQ_INT(list[0].vid, 0x045E);
    XCP_CHECK_EQ_INT(list[0].pid, 0x02EA);
    XCP_CHECK_EQ_INT(list[0].type, XCP_CTRL_XBOXONE);

    XCP_CHECK_EQ_INT(xcp_usb_find(b, NULL, XCP_CTRL_NONE, &found), 1);
    XCP_CHECK_EQ_INT(found.type, XCP_CTRL_XBOXONE);
    XCP_CHECK_EQ_INT(xcp_usb_find(b, NULL, XCP_CTRL_XBOXONE, &found), 1);
    XCP_CHECK_EQ_INT(xcp_usb_find(b, NULL, XCP_CTRL_XBOX360, &found), 0);

    /* A device we do not know is reported but not drivable. */
    xcp_usb_mock_set_device(0x054C, 0x09CC);
    n = b->enumerate(NULL, list, 8);
    XCP_CHECK_EQ_INT(n, 1);
    for (i = 0; i < n; i++) {
        XCP_CHECK_EQ_INT(list[i].type, XCP_CTRL_NONE);
    }
    XCP_CHECK_EQ_INT(xcp_usb_find(b, NULL, XCP_CTRL_NONE, &found), 0);
}

XCP_TEST(mock_reports_disconnect_and_no_data)
{
    xcp_usb_backend    *b = xcp_usb_mock_backend();
    xcp_usb_device_info dev;
    uint8_t             buf[XCP_USB_MAX_REPORT];
    size_t              len = 0;
    void               *h;
    uint8_t             report[20];

    xcp_usb_mock_reset();
    b->init(NULL);

    /* Nothing attached: no enumeration, no open. */
    xcp_usb_mock_set_connected(0);
    XCP_CHECK_EQ_INT(b->enumerate(NULL, &dev, 1), 0);
    XCP_CHECK_FALSE(b->open(NULL, &dev) != NULL);

    xcp_usb_mock_set_connected(1);
    XCP_CHECK_EQ_INT(b->enumerate(NULL, &dev, 1), 1);
    h = b->open(NULL, &dev);
    XCP_CHECK(h != NULL);

    /* Empty queue -> NO_DATA, not an error. */
    XCP_CHECK_EQ_INT(b->read(NULL, h, buf, sizeof(buf), &len, 1), XCP_USB_NO_DATA);
    XCP_CHECK_EQ_INT(xcp_usb_mock_pending(), 0);

    /* Queue one report and read it back byte for byte. */
    memset(report, 0, sizeof(report));
    report[0] = 0x00;
    report[1] = 0x14;
    report[2] = 0x11;
    xcp_usb_mock_push(report, sizeof(report));

    XCP_CHECK_EQ_INT(xcp_usb_mock_pending(), 1);
    XCP_CHECK_EQ_INT(b->read(NULL, h, buf, sizeof(buf), &len, 1), XCP_USB_OK);
    XCP_CHECK_EQ_INT(len, 20);
    XCP_CHECK_MEM(buf, report, 20);
    XCP_CHECK_EQ_INT(xcp_usb_mock_pending(), 0);

    /* Unplug mid-session -> DISCONNECT. */
    xcp_usb_mock_set_connected(0);
    XCP_CHECK_EQ_INT(b->read(NULL, h, buf, sizeof(buf), &len, 1), XCP_USB_DISCONNECT);

    b->close(NULL, h);
}

XCP_TEST(mock_queue_is_fifo_and_bounded)
{
    xcp_usb_backend    *b = xcp_usb_mock_backend();
    xcp_usb_device_info dev;
    uint8_t             buf[XCP_USB_MAX_REPORT];
    uint8_t             a[20];
    uint8_t             c[20];
    uint8_t             got[20];
    size_t              len = 0;
    void               *h;
    int                 i;

    xcp_usb_mock_reset();
    b->init(NULL);
    b->enumerate(NULL, &dev, 1);
    h = b->open(NULL, &dev);

    memset(a, 0, sizeof(a)); a[2] = 0xAA;
    memset(c, 0, sizeof(c)); c[2] = 0xCC;
    xcp_usb_mock_push(a, sizeof(a));
    xcp_usb_mock_push(c, sizeof(c));

    XCP_CHECK_EQ_INT(b->read(NULL, h, buf, sizeof(buf), &len, 0), XCP_USB_OK);
    memcpy(got, buf, 20);
    XCP_CHECK_EQ_INT(got[2], 0xAA);
    XCP_CHECK_EQ_INT(b->read(NULL, h, buf, sizeof(buf), &len, 0), XCP_USB_OK);
    XCP_CHECK_EQ_INT(buf[2], 0xCC);

    /* A report larger than the caller's buffer is dropped, not truncated. */
    xcp_usb_mock_push(a, sizeof(a));
    XCP_CHECK_EQ_INT(b->read(NULL, h, buf, 8, &len, 0), XCP_USB_ERROR);
    XCP_CHECK_EQ_INT(xcp_usb_mock_pending(), 0);

    /* Pushing rubbish is ignored rather than corrupting the queue. */
    xcp_usb_mock_push(NULL, 0);
    XCP_CHECK_EQ_INT(xcp_usb_mock_pending(), 0);

    /* Bounded: pushing far past the depth must not wrap onto live entries. */
    for (i = 0; i < 5000; i++) {
        xcp_usb_mock_push(a, sizeof(a));
    }
    XCP_CHECK(xcp_usb_mock_pending() <= 256);

    b->close(NULL, h);
}

XCP_TEST(usb_timeout_codes_are_not_disconnects)
{
    XCP_CHECK(xcp_usb_status_is_timeout(-7));
    XCP_CHECK(xcp_usb_status_is_timeout((int32_t)0x80240007u));

    /* These are real removal codes; treating them as timeouts would leave a
     * stale virtual pad behind. */
    XCP_CHECK_FALSE(xcp_usb_status_is_timeout((int32_t)0x802400FFu));
    XCP_CHECK_FALSE(xcp_usb_status_is_timeout((int32_t)0x80240004u));
    XCP_CHECK_FALSE(xcp_usb_status_is_timeout((int32_t)0x80240006u));
    XCP_CHECK_FALSE(xcp_usb_status_is_timeout(0));
}

XCP_TEST(mock_start_handshake_is_optional_but_reachable)
{
    xcp_usb_backend    *b = xcp_usb_mock_backend();
    xcp_usb_device_info dev;
    void               *h;

    xcp_usb_mock_reset();
    b->init(NULL);
    b->enumerate(NULL, &dev, 1);
    h = b->open(NULL, &dev);

    XCP_CHECK(b->start != NULL);
    XCP_CHECK_EQ_INT(b->start(NULL, h, XCP_CTRL_XBOXONE), XCP_USB_OK);
    b->close(NULL, h);
}

void xcp_register_usb_mock_tests(void)
{
    XCP_RUN(mock_enumerates_and_classifies_a_xbox_pad);
    XCP_RUN(mock_reports_disconnect_and_no_data);
    XCP_RUN(mock_queue_is_fifo_and_bounded);
    XCP_RUN(usb_timeout_codes_are_not_disconnects);
    XCP_RUN(mock_start_handshake_is_optional_but_reachable);
}
