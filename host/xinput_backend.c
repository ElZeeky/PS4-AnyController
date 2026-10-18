/*
 * host/xinput_backend.c - read the real controller on Windows, no setup needed.
 *
 * Why XInput and not raw HID: the Manba One V2 in wired Xbox mode reports as
 * VID_045E/PID_028E, which Windows binds to the Xbox driver. The pad is then
 * owned by XInput and is not readable as raw reports without replacing its
 * driver (Zadig/WinUSB). XInput therefore gives us live, real, no-setup access
 * to the pad's semantic state.
 *
 * Crucially, the XInput wButtons mask uses the SAME bit layout as the packed
 * 16-bit button field in the Xbox 360 wire report, so we can pack the XInput
 * state into a real 20-byte report and push it through our own decoder. That
 * cross-checks our mapping against the OS's interpretation of the same
 * hardware. It does not prove the on-the-wire byte order; the console capture
 * does that (see --play).
 *
 * xinput*.dll is loaded dynamically so nothing has to be linked and the tool
 * still builds and runs where the DLL is absent.
 */
#include "xinput_backend.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>

/* --- Minimal XInput ABI, declared here so no import library is needed ----- */

typedef struct xinput_gamepad {
    uint16_t wButtons;
    uint8_t  bLeftTrigger;
    uint8_t  bRightTrigger;
    int16_t  sThumbLX;
    int16_t  sThumbLY;
    int16_t  sThumbRX;
    int16_t  sThumbRY;
} xinput_gamepad;

typedef struct xinput_state {
    uint32_t      dwPacketNumber;
    xinput_gamepad Gamepad;
} xinput_state;

typedef DWORD (*xinput_get_state_fn)(DWORD index, xinput_state *state);
typedef DWORD (*xinput_set_state_fn)(DWORD index, void *vibration);

/* XInput button bits, identical to the Xbox 360 report's packed mask. */
#define XI_DPAD_UP        0x0001u
#define XI_DPAD_DOWN      0x0002u
#define XI_DPAD_LEFT      0x0004u
#define XI_DPAD_RIGHT     0x0008u
#define XI_START          0x0010u
#define XI_BACK           0x0020u
#define XI_LEFT_THUMB     0x0040u
#define XI_RIGHT_THUMB    0x0080u
#define XI_LEFT_SHOULDER  0x0100u
#define XI_RIGHT_SHOULDER 0x0200u
#define XI_A              0x1000u
#define XI_B              0x2000u
#define XI_X              0x4000u
#define XI_Y              0x8000u

static HMODULE              g_xinput;
static xinput_get_state_fn  g_get_state;
static xinput_set_state_fn  g_set_state;

int xcp_xinput_available(void);

int xcp_xinput_available(void)
{
    static const char *candidates[] = {
        "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"
    };
    unsigned i;

    if (g_get_state) return 1;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        g_xinput = LoadLibraryA(candidates[i]);
        if (!g_xinput) continue;

        g_get_state = (xinput_get_state_fn)(void *)GetProcAddress(g_xinput, "XInputGetState");
        g_set_state = (xinput_set_state_fn)(void *)GetProcAddress(g_xinput, "XInputSetState");
        if (g_get_state) return 1;

        FreeLibrary(g_xinput);
        g_xinput = NULL;
    }
    return 0;
}

int xcp_xinput_find_connected_slot(void)
{
    xinput_state state;
    DWORD         i;

    if (!xcp_xinput_available()) return -1;

    for (i = 0; i < 4; i++) {
        memset(&state, 0, sizeof(state));
        if (g_get_state(i, &state) == 0 /* ERROR_SUCCESS */) {
            return (int)i;
        }
    }
    return -1;
}

int xcp_xinput_read(int slot, xcp_xinput_reading *out)
{
    xinput_state state;

    if (!out) return -1;
    if (!xcp_xinput_available()) return -1;

    memset(&state, 0, sizeof(state));
    if (g_get_state((DWORD)slot, &state) != 0) return -1;

    memset(out, 0, sizeof(*out));
    out->packet       = state.dwPacketNumber;
    out->buttons      = state.Gamepad.wButtons;
    out->left_trigger = state.Gamepad.bLeftTrigger;
    out->right_trigger= state.Gamepad.bRightTrigger;
    out->left_x       = state.Gamepad.sThumbLX;
    out->left_y       = state.Gamepad.sThumbLY;
    out->right_x      = state.Gamepad.sThumbRX;
    out->right_y      = state.Gamepad.sThumbRY;
    return 0;
}

int xcp_xinput_set_rumble(int slot, uint8_t left, uint8_t right)
{
    if (!xcp_xinput_available() || !g_set_state) return -1;
    (void)slot;
    (void)left;
    (void)right;
    return -1; /* not wired up yet: nothing needs it before rumble support */
}

void xcp_sleep_ms(int ms)
{
    if (ms > 0) Sleep((DWORD)ms);
}

#else /* !_WIN32 */

int xcp_xinput_available(void) { return 0; }
int xcp_xinput_find_connected_slot(void) { return -1; }
int xcp_xinput_read(int slot, xcp_xinput_reading *out) { (void)slot; (void)out; return -1; }
int xcp_xinput_set_rumble(int slot, uint8_t left, uint8_t right)
{ (void)slot; (void)left; (void)right; return -1; }
void xcp_sleep_ms(int ms) { (void)ms; }

#endif
