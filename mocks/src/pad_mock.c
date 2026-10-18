/*
 * mocks/src/pad_mock.c - in-memory model of the scePad / sceUserService world.
 */
#include "xcp/pad_mock.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Capture the router's log lines so tests can assert on what was reported.
 * ------------------------------------------------------------------------ */

static void mock_log(const char *message)
{
    /* The vtable has no context pointer, so we route through a single global
     * that xcp_mock_host_init() points at. Tests are single threaded. */
    extern xcp_mock_host *g_mock_host_for_log;
    xcp_mock_host *m = g_mock_host_for_log;

    if (!m || !message) return;
    if (m->log_count < (int)(sizeof(m->log) / sizeof(m->log[0]))) {
        snprintf(m->log[m->log_count], sizeof(m->log[0]), "%s", message);
        m->log_count++;
    }
}

xcp_mock_host *g_mock_host_for_log = NULL;

/* ---------------------------------------------------------------------------
 * Constructor
 * ------------------------------------------------------------------------ */

/* --- the scePad family of calls ----------------------------------------- */

static int32_t mock_pad_open(int32_t user_id, int32_t type, int32_t index, const void *param)
{
    xcp_mock_host *m = g_mock_host_for_log;
    xcp_mock_pad  *pad = NULL;
    int i;

    (void)param;
    if (!m) return -1;

    m->open_calls++;

    if (m->fail_next_open) {
        m->fail_next_open = 0;
        return -1;
    }

    /* A real scePadOpen fails if a pad is already open for that slot. */
    for (i = 0; i < XCP_MOCK_MAX_PADS; i++) {
        if (m->pads[i].used && m->pads[i].user_id == user_id &&
            m->pads[i].type == type && m->pads[i].index == index) {
            return -1;
        }
    }

    for (i = 0; i < XCP_MOCK_MAX_PADS; i++) {
        if (!m->pads[i].used) {
            pad = &m->pads[i];
            break;
        }
    }
    if (!pad) return -1;

    memset(pad, 0, sizeof(*pad));
    pad->used      = 1;
    pad->handle    = m->open_returns_zero ? 0 : m->next_handle++;
    pad->user_id   = user_id;
    pad->type      = type;
    pad->index     = index;
    pad->connected = 1;
    xcp_pad_data_centre(&pad->data);
    pad->data.connected = 1;

    return pad->handle;
}

static xcp_mock_pad *pad_for_handle(xcp_mock_host *m, int32_t handle)
{
    int i;
    if (!m) return NULL;
    for (i = 0; i < XCP_MOCK_MAX_PADS; i++) {
        if (m->pads[i].used && m->pads[i].handle == handle) return &m->pads[i];
    }
    return NULL;
}

static int32_t mock_pad_close(int32_t handle)
{
    xcp_mock_host *m = g_mock_host_for_log;
    xcp_mock_pad  *pad;

    if (!m) return -1;
    m->close_calls++;

    pad = pad_for_handle(m, handle);
    if (!pad) return -1;

    memset(pad, 0, sizeof(*pad));
    return 0;
}

static int32_t mock_pad_read(int32_t handle, xcp_pad_data *out, int32_t num)
{
    xcp_mock_host *m = g_mock_host_for_log;
    xcp_mock_pad  *pad;
    int i;

    if (!m) return -1;
    m->read_calls++;

    if (m->fail_next_read) {
        m->fail_next_read = 0;
        return -1;
    }

    pad = pad_for_handle(m, handle);
    if (!pad) return -1;
    if (!out) return -1;

    for (i = 0; i < num; i++) {
        out[i] = pad->data;
    }
    return num;
}

static int32_t mock_pad_read_state(int32_t handle, xcp_pad_data *out)
{
    xcp_mock_host *m = g_mock_host_for_log;
    xcp_mock_pad  *pad;

    if (!m) return -1;
    m->read_state_calls++;

    if (m->fail_next_read) {
        m->fail_next_read = 0;
        return -1;
    }
    if (m->read_state_returns_minus_one) return -1;

    pad = pad_for_handle(m, handle);
    if (!pad) return -1;
    if (!out) return -1;

    *out = pad->data;
    return 0;
}

/* A stand-in for OrbisPadInformation; only `connected` is inspected by tests. */
typedef struct mock_pad_information {
    float    touchpad_density;
    uint16_t touch_resolution_x;
    uint16_t touch_resolution_y;
    uint8_t  stick_deadzone_l;
    uint8_t  stick_deadzone_r;
    uint8_t  connection_type;
    uint8_t  count;
    int32_t  connected;
    int32_t  device_class;
} mock_pad_information;

static int32_t mock_get_controller_information(int32_t handle, void *out_info)
{
    xcp_mock_host       *m = g_mock_host_for_log;
    xcp_mock_pad        *pad;
    mock_pad_information *info = (mock_pad_information *)out_info;

    if (!m) return -1;
    m->info_calls++;

    pad = pad_for_handle(m, handle);
    if (!pad) return -1;

    if (info) {
        memset(info, 0, sizeof(*info));
        info->touchpad_density   = 1.0f;
        info->touch_resolution_x = 1920;
        info->touch_resolution_y = 943;
        info->connected          = pad->connected;
        info->device_class       = 0; /* ORBIS_PAD_DEVICE_CLASS_PAD */
    }
    return 0;
}

/* --- the sceUserService surface ----------------------------------------- */

static int32_t mock_get_foreground_user(int32_t *out_user_id)
{
    xcp_mock_host *m = g_mock_host_for_log;

    if (!m || !out_user_id) return -1;
    if (m->foreground_user < 0) return -1;

    *out_user_id = m->foreground_user;
    return 0;
}

/* --- public helpers ------------------------------------------------------ */

void xcp_mock_host_init(xcp_mock_host *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));

    m->api.pad_open                   = mock_pad_open;
    m->api.pad_close                  = mock_pad_close;
    m->api.pad_read                   = mock_pad_read;
    m->api.pad_read_state             = mock_pad_read_state;
    m->api.get_controller_information = mock_get_controller_information;
    m->api.get_foreground_user        = mock_get_foreground_user;
    m->api.log                        = mock_log;

    m->foreground_user = -1;
    m->next_handle     = 1;

    /* The log hook has no context pointer of its own. */
    g_mock_host_for_log = m;
}

void xcp_mock_host_login(xcp_mock_host *m, int32_t user_id)
{
    int i;
    if (!m) return;

    for (i = 0; i < m->user_count; i++) {
        if (m->users[i] == user_id) return;
    }
    if (m->user_count >= XCP_MOCK_MAX_USERS) return;

    m->users[m->user_count++] = user_id;
    if (m->foreground_user < 0) m->foreground_user = user_id;
}

void xcp_mock_host_set_foreground(xcp_mock_host *m, int32_t user_id)
{
    if (!m) return;
    m->foreground_user = user_id;
}

xcp_mock_pad *xcp_mock_host_pad(xcp_mock_host *m, int32_t handle)
{
    return pad_for_handle(m, handle);
}

int xcp_mock_host_pad_index_for_user(xcp_mock_host *m, int32_t user_id)
{
    int i;
    if (!m) return -1;
    for (i = 0; i < XCP_MOCK_MAX_PADS; i++) {
        if (m->pads[i].used && m->pads[i].user_id == user_id) return i;
    }
    return -1;
}

int xcp_mock_host_set_pad_data(xcp_mock_host *m, int32_t handle, const xcp_pad_data *data)
{
    xcp_mock_pad *pad = pad_for_handle(m, handle);
    if (!pad || !data) return -1;
    pad->data = *data;
    return 0;
}

int xcp_mock_host_set_pad_state(xcp_mock_host *m, int32_t handle, const xcp_pad_state *state)
{
    xcp_mock_pad *pad = pad_for_handle(m, handle);
    if (!pad || !state) return -1;

    pad->data.buttons       = state->buttons;
    pad->data.left_x        = state->left_x;
    pad->data.left_y        = state->left_y;
    pad->data.right_x       = state->right_x;
    pad->data.right_y       = state->right_y;
    pad->data.left_trigger  = state->left_trigger;
    pad->data.right_trigger = state->right_trigger;
    pad->data.connected     = state->connected;
    return 0;
}

int xcp_mock_host_set_user_state(xcp_mock_host *m, int32_t user_id, const xcp_pad_state *state)
{
    int idx = xcp_mock_host_pad_index_for_user(m, user_id);
    if (idx < 0) return -1;
    return xcp_mock_host_set_pad_state(m, m->pads[idx].handle, state);
}

void xcp_mock_host_close_all(xcp_mock_host *m)
{
    int i;
    if (!m) return;
    for (i = 0; i < XCP_MOCK_MAX_PADS; i++) {
        memset(&m->pads[i], 0, sizeof(m->pads[i]));
    }
}
