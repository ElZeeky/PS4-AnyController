/*
 * host/xinput_backend.h - live controller access on Windows with no setup.
 */
#ifndef XCP_XINPUT_BACKEND_H
#define XCP_XINPUT_BACKEND_H

#include <stdint.h>

/* Semantic state of one pad, as XInput reports it. */
typedef struct xcp_xinput_reading {
    uint32_t packet;         /* bumps on every change the driver reports */
    uint16_t buttons;        /* same bit layout as the Xbox 360 wire mask */
    uint8_t  left_trigger;   /* 0..255 */
    uint8_t  right_trigger;
    int16_t  left_x;         /* -32768..32767 */
    int16_t  left_y;
    int16_t  right_x;
    int16_t  right_y;
} xcp_xinput_reading;

/* 1 when an xinput dll could be loaded (Windows only). */
int xcp_xinput_available(void);

/* Index of the first connected slot, or -1. */
int xcp_xinput_find_connected_slot(void);

/* Read a slot; returns 0 on success, -1 if the slot is empty. */
int xcp_xinput_read(int slot, xcp_xinput_reading *out);

/* Reserved for the rumble work; returns -1 for now. */
int xcp_xinput_set_rumble(int slot, uint8_t left, uint8_t right);

/* Portable millisecond sleep, so the CLI needs no platform #ifdefs. */
void xcp_sleep_ms(int ms);

#endif /* XCP_XINPUT_BACKEND_H */
