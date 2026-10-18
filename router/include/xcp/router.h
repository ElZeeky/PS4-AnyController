/*
 * xcp/router.h - the "pad layer": decides which player slot the Xbox pad drives,
 * and how it shares that slot with the physical DS4.
 *
 * Pure C. Every call into the PS4 (or into the test mock) goes through the
 * xcp_host_api vtable, which is why this logic can be unit tested on a PC
 * against a fake scePad / user-service with two logged-in users.
 */
#ifndef XCP_ROUTER_H
#define XCP_ROUTER_H

#include <stddef.h>
#include <stdint.h>

#include "xcp/pad_data.h"
#include "xcp/pad_state.h"

/* How many pad handles we track at once. Games never get near this. */
#define XCP_ROUTER_MAX_HANDLES 16

/* ---------------------------------------------------------------------------
 * Which slot is "player 1"
 * ------------------------------------------------------------------------ */

typedef enum xcp_p1_target {
    /* The first pad the game opens after the plugin loaded. Default: it needs
     * no user-service cooperation and matches how games actually behave. */
    XCP_P1_FIRST_OPEN = 0,
    /* Every pad opened for the foreground user id. */
    XCP_P1_FOREGROUND_USER,
    /* Every pad the game opens, whatever the user id. Single-player games that
     * open with a system/invalid user id are covered by this. */
    XCP_P1_ANY_HANDLE
} xcp_p1_target;

/* ---------------------------------------------------------------------------
 * How the Xbox pad shares the slot with the DS4
 * ------------------------------------------------------------------------ */

typedef enum xcp_p1_policy {
    /* Xbox wins any frame in which it is actually being touched; the DS4 takes
     * over the moment the Xbox pad is idle. Default: the Xbox drives player 1,
     * and the DS4 still works so the user can never get stuck. */
    XCP_XBOX_WHEN_ACTIVE = 0,
    /* Buttons/axes from both pads are combined; either pad can press. */
    XCP_MERGE,
    /* The Xbox pad owns the slot outright whenever it is connected. */
    XCP_XBOX_ONLY
} xcp_p1_policy;

/* ---------------------------------------------------------------------------
 * Host API, injected by the platform layer
 * ------------------------------------------------------------------------ */

typedef struct xcp_host_api {
    /* Real pad calls. `param` is platform specific and passed through blind. */
    int32_t (*pad_open)(int32_t user_id, int32_t type, int32_t index, const void *param);
    int32_t (*pad_close)(int32_t handle);
    int32_t (*pad_read)(int32_t handle, xcp_pad_data *out, int32_t num);
    int32_t (*pad_read_state)(int32_t handle, xcp_pad_data *out);
    int32_t (*get_controller_information)(int32_t handle, void *out_info);
    /* -1 when the platform cannot tell us (then FIRST_OPEN behaviour is used). */
    int32_t (*get_foreground_user)(int32_t *out_user_id);
    /* NULL-safe logging hook. */
    void    (*log)(const char *message);
} xcp_host_api;

/* ---------------------------------------------------------------------------
 * Router
 * ------------------------------------------------------------------------ */

typedef struct xcp_router_config {
    xcp_p1_target target;
    xcp_p1_policy policy;

    /*
     * How far an axis must leave centre (out of 127) for XCP_XBOX_WHEN_ACTIVE
     * to call the Xbox pad "in use". Keeps stick drift from permanently
     * stealing the slot.
     */
    uint8_t activity_deadzone;

    /* Force the player-1 user id; -1 means "ask the host". */
    int32_t forced_user_id;

    /*
     * When set (default), only pads opened with type
     * ORBIS_PAD_PORT_TYPE_STANDARD (0) are eligible for player 1. Keeps a game
     * that opens a special device (wheel, fightstick) first from stealing the
     * slot. Turn it off if a title opens its main pad as SPECIAL.
     */
    int require_standard_port_type;
} xcp_router_config;

typedef struct xcp_router_handle_entry {
    int32_t handle;
    int32_t user_id;
    int32_t type;
    int32_t index;
    int     used;
} xcp_router_handle_entry;

typedef struct xcp_router {
    const xcp_host_api     *host;
    xcp_router_config       cfg;

    xcp_router_handle_entry handles[XCP_ROUTER_MAX_HANDLES];
    int                     handle_count;

    /* Latest state of the Xbox pad, pushed in by whoever polls USB. */
    xcp_pad_state           xbox;
    int                     xbox_present;   /* a device is open + streaming */

    int32_t                 p1_handle;      /* -1 until a slot is chosen */
    int32_t                 p1_user_id;     /* -1 if unknown */
    int32_t                 foreground_user;/* -1 if unknown */

    /* Counters, handy in klog and in tests. */
    unsigned long           reads_seen;
    unsigned long           reads_injected;
    unsigned long           opens_seen;
} xcp_router;

void xcp_router_config_default(xcp_router_config *cfg);

/* `host` must outlive the router. */
void xcp_router_init(xcp_router *r, const xcp_host_api *host, const xcp_router_config *cfg);

/*
 * Tell the router what the USB side currently sees. Called from the poller.
 * When `present` is 0 the router forgets the pad and stops injecting, but it
 * keeps the chosen player-1 slot so a replug lands in the same slot.
 */
void xcp_router_set_xbox(xcp_router *r, const xcp_pad_state *state, int present);

/* --- The four hooked operations ------------------------------------------ */

int32_t xcp_router_pad_open(xcp_router *r, int32_t user_id, int32_t type,
                            int32_t index, const void *param);
int32_t xcp_router_pad_close(xcp_router *r, int32_t handle);

/*
 * Record an open/close that the platform layer already performed itself.
 *
 * The console hook has to make the real call (the game's handle must be a real
 * pad), so it must not go through the host-calling wrappers above or the pad
 * would be opened twice. It calls these instead.
 */
void xcp_router_note_pad_open(xcp_router *r, int32_t handle, int32_t user_id,
                              int32_t type, int32_t index);
void xcp_router_note_pad_close(xcp_router *r, int32_t handle);
int32_t xcp_router_pad_read(xcp_router *r, int32_t handle, xcp_pad_data *out, int32_t num);
int32_t xcp_router_pad_read_state(xcp_router *r, int32_t handle, xcp_pad_data *out);
int32_t xcp_router_get_controller_information(xcp_router *r, int32_t handle, void *out_info);

/*
 * Decide and overwrite, without calling the host at all.
 *
 * `out` must already contain what the real pad reported, because the platform
 * layer reads the genuine DS4 into its own buffer first and only copies the
 * managed fields back. That ordering is what preserves motion, touch and the
 * trailing bytes the game may also be looking at, so this function exists
 * instead of the read-and-apply helper above being used on the console.
 */
void xcp_router_apply(xcp_router *r, int32_t handle, xcp_pad_data *out, int32_t num);

/* --- Introspection (klog, tests, CLI) ------------------------------------ */

/* 1 if this handle is the slot the Xbox pad drives. */
int xcp_router_handle_is_p1(const xcp_router *r, int32_t handle);

/* One-line summary of the router state. */
void xcp_router_describe(const xcp_router *r, char *out, size_t out_size);

/* Look up a tracked handle; NULL when unknown. */
const xcp_router_handle_entry *xcp_router_find_handle(const xcp_router *r, int32_t handle);

#endif /* XCP_ROUTER_H */
