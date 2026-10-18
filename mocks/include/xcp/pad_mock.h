/*
 * xcp/pad_mock.h - a fake PS4 for PC testing.
 *
 * Implements the xcp_host_api vtable on top of an in-memory model of the
 * scePad / sceUserService world:
 *   - a set of logged-in users with one designated foreground user
 *   - pads that can be opened per (user_id, type, index) and return handles
 *   - per-handle pad data, so tests can pretend a DS4 is being wiggled
 *
 * This is what lets the player-1 assignment logic be tested end to end on a PC
 * with "two logged-in users, DS4 as user 1, nothing else" - exactly the
 * scenario described in the plan.
 */
#ifndef XCP_PAD_MOCK_H
#define XCP_PAD_MOCK_H

#include "xcp/pad_data.h"
#include "xcp/router.h"

#define XCP_MOCK_MAX_USERS 4
#define XCP_MOCK_MAX_PADS  16

/* Mirrors ORBIS_PAD_PORT_TYPE_STANDARD / _SPECIAL without including orbis. */
#define XCP_MOCK_PORT_TYPE_STANDARD 0
#define XCP_MOCK_PORT_TYPE_SPECIAL  2

typedef struct xcp_mock_pad {
    int          used;
    int32_t      handle;
    int32_t      user_id;
    int32_t      type;
    int32_t      index;
    int          connected;
    xcp_pad_data data;
} xcp_mock_pad;

typedef struct xcp_mock_host {
    xcp_host_api api; /* pass &mock.api to xcp_router_init */

    int32_t users[XCP_MOCK_MAX_USERS];
    int     user_count;
    int32_t foreground_user; /* -1 when nobody is logged in */

    xcp_mock_pad pads[XCP_MOCK_MAX_PADS];
    int32_t      next_handle;

    /* Counters so tests can assert on how the router called us. */
    int open_calls;
    int close_calls;
    int read_calls;
    int read_state_calls;
    int info_calls;

    /* Fault injection. */
    int fail_next_open;        /* force the next scePadOpen to fail        */
    int fail_next_read;        /* force the next scePadRead to fail        */
    int read_state_returns_minus_one; /* mimic "pad not ready"              */
    int open_returns_zero;     /* some apps treat handle 0 as valid        */

    /* Captured log lines (router_log output). */
    char log[32][192];
    int  log_count;
} xcp_mock_host;

/* Reset to a clean, powered-on console with nobody logged in. */
void xcp_mock_host_init(xcp_mock_host *m);

/* Log a user in (idempotent). The first logged-in user becomes foreground. */
void xcp_mock_host_login(xcp_mock_host *m, int32_t user_id);

/* Change which logged-in user is foreground. */
void xcp_mock_host_set_foreground(xcp_mock_host *m, int32_t user_id);

/* Look up a pad by handle; NULL when it is not open. */
xcp_mock_pad *xcp_mock_host_pad(xcp_mock_host *m, int32_t handle);

/* Index of the first pad opened by `user_id`, or -1. */
int xcp_mock_host_pad_index_for_user(xcp_mock_host *m, int32_t user_id);

/* Fill in a pad's data; returns 0 on success. */
int xcp_mock_host_set_pad_data(xcp_mock_host *m, int32_t handle, const xcp_pad_data *data);
int xcp_mock_host_set_pad_state(xcp_mock_host *m, int32_t handle, const xcp_pad_state *state);

/* Convenience: set the data of the first pad belonging to `user_id`. */
int xcp_mock_host_set_user_state(xcp_mock_host *m, int32_t user_id, const xcp_pad_state *state);

/* Simulate the game closing everything. */
void xcp_mock_host_close_all(xcp_mock_host *m);

#endif /* XCP_PAD_MOCK_H */
