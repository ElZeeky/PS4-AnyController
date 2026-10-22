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
    XCP_CTRL_SWITCH,
    XCP_CTRL_XBOX_SERIES,
    XCP_CTRL_DUALSENSE
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
 * Xbox One (and Xbox-mode third-party pads) - "GIP" input report.
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

/*
 * Xbox Series X|S speaks the same 0x20 GIP input report. The only difference
 * we decode is the Share button, which is not in the 18-byte prefix: it sits
 * XCP_XBOX_SERIES_SHARE_FROM_END bytes before the end of the whole USB
 * transfer (GIP header included). That is byte 18 of the 44-byte firmware 5.1
 * report and byte 22 of the 48-byte 5.5 report (the same len-relative rule
 * Linux's xpad driver uses). It gets its own controller type so the 18-byte
 * Xbox One path cannot invent a Share press from trailing bytes on
 * third-party GIP pads.
 */
#define XCP_XBOX_SERIES_SHARE_FROM_END 26u
#define XCP_XBOX_SERIES_SHARE_BIT      0x01u
#define XCP_XBOX_SERIES_MIN_SIZE       44u /* smallest full USB transfer (fw 5.1) */

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
 * Sony DualSense (PS5 pad) / DualSense Edge - USB input report 0x01.
 *
 * The wired USB report is 64 bytes. Only the first 11 are needed here; the
 * layout is the same one Linux hid-playstation.c and SDL_hidapi_ps5.c use:
 *   [0]  report id, 0x01
 *   [1]  left  stick X      [2] left  stick Y      (0..255, Y grows down)
 *   [3]  right stick X      [4] right stick Y
 *   [5]  L2 trigger         [6] R2 trigger         (0..255)
 *   [7]  sequence counter
 *   [8]  buttons0: low nibble = hat, high nibble = face buttons
 *   [9]  buttons1: shoulders, triggers, create/options, L3/R3
 *   [10] buttons2: PS, touchpad, mute (Edge extras live here too)
 * ---------------------------------------------------------------------- */
#define XCP_DUALSENSE_REPORT_ID   0x01u
#define XCP_DUALSENSE_REPORT_SIZE 64u
#define XCP_DUALSENSE_MIN_SIZE    11u

typedef struct __attribute__((packed)) {
    uint8_t report_id;      /* [0]  0x01 */
    uint8_t left_stick_x;   /* [1] */
    uint8_t left_stick_y;   /* [2] */
    uint8_t right_stick_x;  /* [3] */
    uint8_t right_stick_y;  /* [4] */
    uint8_t left_trigger;   /* [5]  0..255 */
    uint8_t right_trigger;  /* [6]  0..255 */
    uint8_t counter;        /* [7] */
    uint8_t buttons0;       /* [8]  hat + face buttons */
    uint8_t buttons1;       /* [9] */
    uint8_t buttons2;       /* [10] */
} xcp_dualsense_report;

/* xcp_dualsense_report.buttons0 */
#define XCP_DS_SQUARE   0x10u
#define XCP_DS_CROSS    0x20u
#define XCP_DS_CIRCLE   0x40u
#define XCP_DS_TRIANGLE 0x80u

/* xcp_dualsense_report.buttons1 */
#define XCP_DS_L1        0x01u
#define XCP_DS_R1        0x02u
#define XCP_DS_L2        0x04u
#define XCP_DS_R2        0x08u
#define XCP_DS_CREATE    0x10u
#define XCP_DS_OPTIONS   0x20u
#define XCP_DS_L3        0x40u
#define XCP_DS_R3        0x80u

/* xcp_dualsense_report.buttons2 */
#define XCP_DS_PS        0x01u
#define XCP_DS_TOUCHPAD  0x02u
#define XCP_DS_MUTE      0x04u
#define XCP_DS_EDGE_FN_L      0x10u
#define XCP_DS_EDGE_FN_R      0x20u
#define XCP_DS_EDGE_PADDLE_L  0x40u
#define XCP_DS_EDGE_PADDLE_R  0x80u

/* -------------------------------------------------------------------------
 * Known USB identifiers, used to pick a protocol when enumerating.
 * ---------------------------------------------------------------------- */
#define XCP_VID_MICROSOFT 0x045Eu
#define XCP_PID_XBOX360   0x028Eu /* wired Xbox 360 pad */
#define XCP_PID_360_WLESS 0x0719u /* Xbox 360 wireless receiver (unsupported) */

#define XCP_VID_PDP       0x0E6Fu

#define XCP_VID_SONY          0x054Cu
#define XCP_PID_DUALSENSE     0x0CE6u
#define XCP_PID_DUALSENSE_EDGE 0x0DF2u

/* Is `pid` one of the known Xbox One / Series product ids? */
int xcp_is_xboxone_pid(uint16_t pid);

/* Is `pid` a Microsoft Xbox Series X|S product id (Share button quirk)? */
int xcp_is_xbox_series_pid(uint16_t pid);

/* Is `pid` a Sony DualSense or DualSense Edge product id? */
int xcp_is_dualsense_pid(uint16_t pid);

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
