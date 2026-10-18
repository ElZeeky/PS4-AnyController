/*
 * mocks/src/report_pack.c - see report_pack.h.
 */
#include "xcp/report_pack.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void xcp_pack_xbox360(uint8_t *buf, size_t buf_size, uint16_t buttons,
                      uint8_t left_trigger, uint8_t right_trigger,
                      int16_t left_x, int16_t left_y,
                      int16_t right_x, int16_t right_y)
{
    if (!buf || buf_size < XCP_XBOX360_REPORT_SIZE) return;

    memset(buf, 0, XCP_XBOX360_REPORT_SIZE);
    buf[0] = 0x00;
    buf[1] = (uint8_t)XCP_XBOX360_REPORT_SIZE;
    put_u16(buf + 2, buttons);
    buf[4] = left_trigger;
    buf[5] = right_trigger;
    put_u16(buf + 6,  (uint16_t)left_x);
    put_u16(buf + 8,  (uint16_t)left_y);
    put_u16(buf + 10, (uint16_t)right_x);
    put_u16(buf + 12, (uint16_t)right_y);
}

void xcp_pack_xboxone(uint8_t *buf, size_t buf_size, uint16_t buttons,
                      uint16_t left_trigger, uint16_t right_trigger,
                      int16_t left_x, int16_t left_y,
                      int16_t right_x, int16_t right_y)
{
    if (!buf || buf_size < XCP_XBOXONE_REPORT_SIZE) return;

    memset(buf, 0, XCP_XBOXONE_REPORT_SIZE);
    buf[0] = XCP_XBOXONE_REPORT_INPUT;
    buf[2] = 1;    /* sequence counter */
    buf[3] = 0x0E; /* remaining length as seen on the wire */
    put_u16(buf + 4, buttons);
    put_u16(buf + 6,  left_trigger);
    put_u16(buf + 8,  right_trigger);
    put_u16(buf + 10, (uint16_t)left_x);
    put_u16(buf + 12, (uint16_t)left_y);
    put_u16(buf + 14, (uint16_t)right_x);
    put_u16(buf + 16, (uint16_t)right_y);
}

void xcp_pack_switch(uint8_t *buf, size_t buf_size,
                     uint8_t buttons0, uint8_t buttons1, uint8_t hat,
                     uint8_t left_x, uint8_t left_y,
                     uint8_t right_x, uint8_t right_y)
{
    if (!buf || buf_size < XCP_SWITCH_REPORT_SIZE) return;

    buf[0] = buttons0;
    buf[1] = buttons1;
    buf[2] = hat;
    buf[3] = left_x;
    buf[4] = left_y;
    buf[5] = right_x;
    buf[6] = right_y;
}
