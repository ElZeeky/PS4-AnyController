/*
 * xcp/report_pack.h - build raw reports from decoded values.
 *
 * Test-support code, not part of the plugin. Two uses:
 *   1. fixtures: write a report with known contents and assert the decoder
 *      reads it back;
 *   2. cross-checking against real hardware on the PC, where the OS gives us a
 *      semantic pad state (XInput) but not the raw wire bytes. Packing that
 *      state into a report and running it through our decoder proves the button
 *      and axis mapping against real input, with only the byte order left to be
 *      confirmed by the capture from the console.
 */
#ifndef XCP_REPORT_PACK_H
#define XCP_REPORT_PACK_H

#include <stddef.h>
#include <stdint.h>

#include "xcp/report.h"

/*
 * Pack an Xbox 360 input report.
 *
 * `buttons` uses the packed 16-bit layout, which is byte-identical to the
 * Windows XInput wButtons mask - so an XInput read can be fed straight in.
 *   0x0001 d-pad up   0x0002 down    0x0004 left    0x0008 right
 *   0x0010 start      0x0020 back    0x0040 L3      0x0080 R3
 *   0x0100 LB         0x0200 RB      0x0400 guide
 *   0x1000 A          0x2000 B       0x4000 X       0x8000 Y
 */
void xcp_pack_xbox360(uint8_t *buf, size_t buf_size, uint16_t buttons,
                      uint8_t left_trigger, uint8_t right_trigger,
                      int16_t left_x, int16_t left_y,
                      int16_t right_x, int16_t right_y);

/* Pack an Xbox One "GIP" input report (10-bit triggers). */
void xcp_pack_xboxone(uint8_t *buf, size_t buf_size, uint16_t buttons,
                      uint16_t left_trigger, uint16_t right_trigger,
                      int16_t left_x, int16_t left_y,
                      int16_t right_x, int16_t right_y);

/* Pack a Switch input-only report (8-bit sticks, hat). */
void xcp_pack_switch(uint8_t *buf, size_t buf_size,
                     uint8_t buttons0, uint8_t buttons1, uint8_t hat,
                     uint8_t left_x, uint8_t left_y,
                     uint8_t right_x, uint8_t right_y);

#endif /* XCP_REPORT_PACK_H */
