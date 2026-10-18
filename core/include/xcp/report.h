/*
 * xcp/report.h - raw HID report layouts for the supported controllers.
 *
 * Layouts come from the reference implementation crucifix86/xbox_controller_plugin
 * (MIT) and were sanity-checked against the public Xbox 360 HID documentation
 * and the Xbox One "GIP" report layout. Byte offsets are documented per struct;
 * do not reorder fields.
 */
#ifndef XCP_REPORT_H
#define XCP_REPORT_H

#include <stdint.h>

/* Controller families we can decode. */
typedef enum xcp_controller_type {
    XCP_CTRL_NONE = 0,
    XCP_CTRL_XBOX360,
    XCP_CTRL_XBOXONE,
    XCP_CTRL_SWITCH
} xcp_controller_type;

/* Human readable name of a controller type (never NULL). */
const char *xcp_controller_type_name(xcp_controller_type type);

/* -------------------------------------------------------------------------
 * Xbox 360 wired controller - 20 byte input report
 * ---------------------------------------------------------------------- */
#define XCP_XBOX360_REPORT_SIZE 20

typedef struct __attribute__((packed)) {
    uint8_t msg_type;       /* [0]  always 0x00 */
    uint8_t msg_length;     /* [1]  always 0x14 (20) */
    uint8_t buttons_low;    /* [2]  d-pad + start/back/L3/R3 */
    uint8_t buttons_high;   /* [3]  LB/RB/Guide/A/B/X/Y */
    uint8_t left_trigger;   /* [4]  0..255 */
    uint8_t right_trigger;  /* [5]  0..255 */
    int16_t left_stick_x;   /* [6]  LE, -32768..32767 */
    int16_t left_stick_y;   /* [8] */
    int16_t right_stick_x;  /* [10] */
    int16_t right_stick_y;  /* [12] */
    uint8_t reserved[6];    /* [14] */
} xcp_xbox360_report;

/* xcp_xbox360_report.buttons_low bits */
#define XCP_360_DPAD_UP     0x01u
#define XCP_360_DPAD_DOWN   0x02u
#define XCP_360_DPAD_LEFT   0x04u
#define XCP_360_DPAD_RIGHT  0x08u
#define XCP_360_START       0x10u
#define XCP_360_BACK        0x20u
#define XCP_360_L3          0x40u
#define XCP_360_R3          0x80u

/* xcp_xbox360_report.buttons_high bits */
#define XCP_360_LB          0x01u
#define XCP_360_RB          0x02u
#define XCP_360_GUIDE       0x04u
#define XCP_360_A           0x10u
#define XCP_360_B           0x20u
#define XCP_360_X           0x40u
#define XCP_360_Y           0x80u

/* -------------------------------------------------------------------------
 * Xbox One / Series (and Xbox-mode third-party pads) - "GIP" input report.
 *
 * Wire layout of the 0x20 input report:
 *   [0]  0x20 message type
 *   [1]  flags (unused here)
 *   [2]  sequence counter
 *   [3]  remaining length
 *   [4]  buttons, 16-bit little-endian   (see XCP_ONE_* below)
 *   [6]  left trigger,  10-bit (0..1023), little-endian
 *   [8]  right trigger, 10-bit (0..1023), little-endian
 *   [10] left  stick X, int16 LE
 *   [12] left  stick Y, int16 LE
 *   [14] right stick X, int16 LE
 *   [16] right stick Y, int16 LE
 * ---------------------------------------------------------------------- */
#define XCP_XBOXONE_REPORT_SIZE   18
#define XCP_XBOXONE_REPORT_INPUT  0x20u
#define XCP_XBOXONE_REPORT_GUIDE  0x07u

typedef struct __attribute__((packed)) {
    uint8_t  report_type;   /* [0]  0x20 for input */
    uint8_t  flags;         /* [1] */
    uint8_t  counter;       /* [2] */
    uint8_t  length;        /* [3] */
    uint8_t  buttons_low;   /* [4]  low half of the 16-bit button mask */
    uint8_t  buttons_high;  /* [5]  high half  */
    uint16_t left_trigger;  /* [6]  0..1023 */
    uint16_t right_trigger; /* [8]  0..1023 */
    int16_t  left_stick_x;  /* [10] */
    int16_t  left_stick_y;  /* [12] */
    int16_t  right_stick_x; /* [14] */
    int16_t  right_stick_y; /* [16] */
} xcp_xboxone_report;

/* xcp_xboxone_report.buttons_low bits */
#define XCP_ONE_SYNC        0x01u
#define XCP_ONE_MENU        0x04u  /* Start */
#define XCP_ONE_VIEW        0x08u  /* Back / Select */
#define XCP_ONE_A           0x10u
#define XCP_ONE_B           0x20u
#define XCP_ONE_X           0x40u
#define XCP_ONE_Y           0x80u

/* xcp_xboxone_report.buttons_high bits */
#define XCP_ONE_DPAD_UP     0x01u
#define XCP_ONE_DPAD_DOWN   0x02u
#define XCP_ONE_DPAD_LEFT   0x04u
#define XCP_ONE_DPAD_RIGHT  0x08u
#define XCP_ONE_LB          0x10u
#define XCP_ONE_RB          0x20u
#define XCP_ONE_L3          0x40u
#define XCP_ONE_R3          0x80u

#define XCP_ONE_TRIGGER_MAX 1023u

/* -------------------------------------------------------------------------
 * Nintendo Switch "input only" third-party pads (PDP family).
 * ---------------------------------------------------------------------- */
#define XCP_SWITCH_REPORT_SIZE 7

typedef struct __attribute__((packed)) {
    uint8_t buttons0;      /* [0] face, shoulders, ZL/ZR */
    uint8_t buttons1;      /* [1] plus/minus, L3/R3, home */
    uint8_t hat;           /* [2] 0..7 directions, >=8 centred */
    uint8_t left_stick_x;  /* [3] 0..255, centre 128 */
    uint8_t left_stick_y;  /* [4] */
    uint8_t right_stick_x; /* [5] */
    uint8_t right_stick_y; /* [6] */
} xcp_switch_report;

/* xcp_switch_report.buttons0 bits */
#define XCP_SWITCH_Y    0x01u
#define XCP_SWITCH_B    0x02u
#define XCP_SWITCH_A    0x04u
#define XCP_SWITCH_X    0x08u
#define XCP_SWITCH_L    0x10u
#define XCP_SWITCH_R    0x20u
#define XCP_SWITCH_ZL   0x40u
#define XCP_SWITCH_ZR   0x80u

/* xcp_switch_report.buttons1 bits */
#define XCP_SWITCH_MINUS   0x01u
#define XCP_SWITCH_PLUS    0x02u
#define XCP_SWITCH_L3      0x04u
#define XCP_SWITCH_R3      0x08u
#define XCP_SWITCH_HOME    0x10u
#define XCP_SWITCH_CAPTURE 0x20u

/* hat values */
#define XCP_SWITCH_HAT_UP         0u
#define XCP_SWITCH_HAT_UP_RIGHT   1u
#define XCP_SWITCH_HAT_RIGHT      2u
#define XCP_SWITCH_HAT_DOWN_RIGHT 3u
#define XCP_SWITCH_HAT_DOWN       4u
#define XCP_SWITCH_HAT_DOWN_LEFT  5u
#define XCP_SWITCH_HAT_LEFT       6u
#define XCP_SWITCH_HAT_UP_LEFT    7u
#define XCP_SWITCH_HAT_CENTERED   8u

/* -------------------------------------------------------------------------
 * Known USB identifiers, used to pick a protocol when enumerating.
 * ---------------------------------------------------------------------- */
#define XCP_VID_MICROSOFT 0x045Eu
#define XCP_PID_XBOX360   0x028Eu /* wired Xbox 360 pad */
#define XCP_PID_360_WLESS 0x0719u /* Xbox 360 wireless receiver (unsupported) */

#define XCP_VID_PDP       0x0E6Fu

/* Is `pid` one of the known Xbox One / Series product ids? */
int xcp_is_xboxone_pid(uint16_t pid);

/*
 * Is `pid` a known PDP Switch "input only" pad product id?
 * NOTE: this list is from the reference plugin and is unverified (OPEN-3).
 */
int xcp_is_pdp_switch_pid(uint16_t pid);

/*
 * Pick a protocol for a (vid, pid) pair. Returns XCP_CTRL_NONE when unknown.
 * An unknown Microsoft-VID product id maps to XCP_CTRL_XBOXONE because every
 * Xbox One/Series-family pad uses the same GIP format.
 */
xcp_controller_type xcp_controller_type_for_ids(uint16_t vid, uint16_t pid);

#endif /* XCP_REPORT_H */
