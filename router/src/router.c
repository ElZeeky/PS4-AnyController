/*
 * router/src/router.c - player-1 assignment and the DS4/Xbox sharing policy.
 *
 * Design notes
 * ------------
 * The reference plugin gave the Xbox pad to player 2 by returning a *virtual*
 * handle for a second user, leaving player 1's real DS4 handle alone.
 *
 * For player 1 we do the opposite, and simpler, thing: we let scePadOpen hand
 * out the game's real DS4 handle and remember which user it belongs to; then we
 * augment the data returned by scePadRead/scePadReadState on that handle. We
 * never invent handle numbers, so games that call
 * scePadGetControllerInformation, scePadGetHandle or scePadSetVibration keep
 * working against a genuine pad, and there is no new failure mode where a game
 * refuses to open "our" handle.
 *
 * Everything is driven from the four router entry points; nothing here calls
 * into a platform directly.
 */
#include "xcp/router.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------ */

void xcp_pad_data_centre(xcp_pad_data *d)
{
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->left_x  = (uint8_t)XCP_STICK_CENTER;
    d->left_y  = (uint8_t)XCP_STICK_CENTER;
    d->right_x = (uint8_t)XCP_STICK_CENTER;
    d->right_y = (uint8_t)XCP_STICK_CENTER;
}

void xcp_pad_data_copy_managed(xcp_pad_data *dst, const xcp_pad_data *src)
{
    if (!dst || !src) return;
    dst->buttons       = src->buttons;
    dst->left_x        = src->left_x;
    dst->left_y        = src->left_y;
    dst->right_x       = src->right_x;
    dst->right_y       = src->right_y;
    dst->left_trigger  = src->left_trigger;
    dst->right_trigger = src->right_trigger;
    dst->connected     = src->connected;
}

void xcp_router_config_default(xcp_router_config *cfg)
{
    if (!cfg) return;
    cfg->target                     = XCP_P1_FIRST_OPEN;
    cfg->policy                     = XCP_XBOX_WHEN_ACTIVE;
    cfg->activity_deadzone          = 24;
    cfg->forced_user_id             = -1;
    cfg->require_standard_port_type = 1;
}

static void router_log(const xcp_router *r, const char *message)
{
    if (r && r->host && r->host->log) {
        r->host->log(message);
    }
}

/* Distance of an axis from centre, always >= 0. */
static int axis_offset(uint8_t value)
{
    int d = (int)value - (int)XCP_STICK_CENTER;
    return d < 0 ? -d : d;
}

/*
 * Is the Xbox pad being touched right now? Used by XCP_XBOX_WHEN_ACTIVE.
 * Any button, any trigger, or any axis past the configured deadzone counts.
 */
static int xbox_is_active(const xcp_router *r)
{
    const xcp_pad_state *s = &r->xbox;
    int dz = (int)r->cfg.activity_deadzone;

    if (!r->xbox_present || !s->connected) return 0;
    if (s->buttons != 0) return 1;
    if (s->left_trigger > 0 || s->right_trigger > 0) return 1;
    if (axis_offset(s->left_x)  > dz) return 1;
    if (axis_offset(s->left_y)  > dz) return 1;
    if (axis_offset(s->right_x) > dz) return 1;
    if (axis_offset(s->right_y) > dz) return 1;
    return 0;
}

/* Copy an xcp_pad_state into the pad-data payload. */
static void apply_state_to_pad_data(const xcp_pad_state *s, xcp_pad_data *d)
{
    d->buttons       = s->buttons;
    d->left_x        = s->left_x;
    d->left_y        = s->left_y;
    d->right_x       = s->right_x;
    d->right_y       = s->right_y;
    d->left_trigger  = s->left_trigger;
    d->right_trigger = s->right_trigger;
    d->connected     = 1;
}

/* Merge an xcp_pad_state into an xcp_pad_data using the same policy as the
 * state/state merge. */
static void merge_state_into_pad_data(const xcp_pad_state *s, xcp_pad_data *d)
{
    xcp_pad_state merged;

    if (!(d->connected)) {
        xcp_pad_state_centre_sticks(&merged);
        merged.buttons   = 0;
        merged.connected = 0;
    } else {
        merged.buttons       = d->buttons;
        merged.left_x        = d->left_x;
        merged.left_y        = d->left_y;
        merged.right_x       = d->right_x;
        merged.right_y       = d->right_y;
        merged.left_trigger  = d->left_trigger;
        merged.right_trigger = d->right_trigger;
        merged.connected     = d->connected;
    }

    xcp_pad_state_merge(&merged, s);
    apply_state_to_pad_data(&merged, d);
}

/* ---------------------------------------------------------------------------
 * Handle bookkeeping
 * ------------------------------------------------------------------------ */

const xcp_router_handle_entry *xcp_router_find_handle(const xcp_router *r, int32_t handle)
{
    int i;
    if (!r) return NULL;
    for (i = 0; i < XCP_ROUTER_MAX_HANDLES; i++) {
        if (r->handles[i].used && r->handles[i].handle == handle) {
            return &r->handles[i];
        }
    }
    return NULL;
}

static xcp_router_handle_entry *find_handle_mut(xcp_router *r, int32_t handle)
{
    int i;
    for (i = 0; i < XCP_ROUTER_MAX_HANDLES; i++) {
        if (r->handles[i].used && r->handles[i].handle == handle) {
            return &r->handles[i];
        }
    }
    return NULL;
}

static void remember_handle(xcp_router *r, int32_t handle, int32_t user_id,
                            int32_t type, int32_t index)
{
    xcp_router_handle_entry *slot = NULL;
    int i;

    if (handle < 0) return; /* an open that failed - nothing to track */

    slot = find_handle_mut(r, handle);
    if (slot) {
        /* Handle number reused without a close: overwrite the stale entry. */
        slot->user_id = user_id;
        slot->type    = type;
        slot->index   = index;
        return;
    }

    for (i = 0; i < XCP_ROUTER_MAX_HANDLES; i++) {
        if (!r->handles[i].used) {
            slot = &r->handles[i];
            break;
        }
    }
    if (!slot) return; /* table full: we simply do not augment this handle */

    slot->used    = 1;
    slot->handle  = handle;
    slot->user_id = user_id;
    slot->type    = type;
    slot->index   = index;
    r->handle_count++;
}

static void forget_handle(xcp_router *r, int32_t handle)
{
    xcp_router_handle_entry *slot = find_handle_mut(r, handle);

    if (!slot) return;
    memset(slot, 0, sizeof(*slot));
    if (r->handle_count > 0) r->handle_count--;

    /* Drop-in games close and reopen pads between rounds; if the chosen slot
     * goes away we must be ready to pick a new one, otherwise the next open
     * would silently be ignored. */
    if (handle == r->p1_handle) {
        r->p1_handle = -1;
        r->p1_user_id = -1;
    }
}

/* ---------------------------------------------------------------------------
 * Choosing the player-1 slot
 * ------------------------------------------------------------------------ */

static int32_t resolve_foreground_user(xcp_router *r)
{
    int32_t user = -1;

    if (r->cfg.forced_user_id >= 0) return r->cfg.forced_user_id;

    if (r->host && r->host->get_foreground_user) {
        if (r->host->get_foreground_user(&user) != 0) user = -1;
    }
    return user;
}

/* Should the pad belonging to (user_id, type, index) drive player 1? */
static int should_become_p1(xcp_router *r, int32_t user_id, int32_t type, int32_t index)
{
    (void)index;

    /* Never hijack a pad that is not the standard controller for the slot. */
    if (r->cfg.require_standard_port_type &&
        type != 0 /* ORBIS_PAD_PORT_TYPE_STANDARD */) {
        return 0;
    }

    if (r->cfg.target == XCP_P1_ANY_HANDLE) return 1;

    if (r->cfg.target == XCP_P1_FOREGROUND_USER) {
        int32_t fg = r->foreground_user;
        return (fg >= 0 && user_id == fg);
    }

    /* XCP_P1_FIRST_OPEN: the first standard pad opened wins. */
    return 1;
}

static void maybe_claim_p1(xcp_router *r, int32_t handle, int32_t user_id,
                           int32_t type, int32_t index)
{
    char line[192];

    if (r->p1_handle >= 0) return;             /* already decided */
    if (!should_become_p1(r, user_id, type, index)) return;

    r->p1_handle  = handle;
    r->p1_user_id = user_id;

    snprintf(line, sizeof(line),
             "xcp: player 1 slot claimed (handle=%d user=%d type=%d index=%d)",
             (int)handle, (int)user_id, (int)type, (int)index);
    router_log(r, line);
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

void xcp_router_init(xcp_router *r, const xcp_host_api *host, const xcp_router_config *cfg)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->host = host;
    if (cfg) {
        r->cfg = *cfg;
    } else {
        xcp_router_config_default(&r->cfg);
    }
    r->p1_handle       = -1;
    r->p1_user_id      = -1;
    r->foreground_user = resolve_foreground_user(r);
}

void xcp_router_set_xbox(xcp_router *r, const xcp_pad_state *state, int present)
{
    char line[192];

    if (!r) return;

    if (state) {
        r->xbox = *state;
    } else {
        memset(&r->xbox, 0, sizeof(r->xbox));
    }

    if (present != r->xbox_present) {
        snprintf(line, sizeof(line), "xcp: xbox pad %s",
                 present ? "connected" : "disconnected");
        router_log(r, line);
        r->xbox_present = present ? 1 : 0;
        /* We keep p1_handle across a disconnect on purpose: a replug should
         * land straight back in the same slot without the game re-opening. */
    }
}

/* ---------------------------------------------------------------------------
 * Hooked operations
 * ------------------------------------------------------------------------ */

void xcp_router_note_pad_open(xcp_router *r, int32_t handle, int32_t user_id,
                              int32_t type, int32_t index)
{
    if (!r) return;

    /* Refresh the foreground user lazily: it is only known once user service is
     * up, which may be after the router was constructed. */
    if (r->foreground_user < 0) {
        r->foreground_user = resolve_foreground_user(r);
    }

    r->opens_seen++;
    if (handle < 0) return;

    remember_handle(r, handle, user_id, type, index);
    maybe_claim_p1(r, handle, user_id, type, index);
}

void xcp_router_note_pad_close(xcp_router *r, int32_t handle)
{
    if (!r) return;
    forget_handle(r, handle);
}

int32_t xcp_router_pad_open(xcp_router *r, int32_t user_id, int32_t type,
                            int32_t index, const void *param)
{
    int32_t handle;

    if (!r) return -1;
    if (!r->host || !r->host->pad_open) return -1;

    handle = r->host->pad_open(user_id, type, index, param);
    xcp_router_note_pad_open(r, handle, user_id, type, index);
    return handle;
}

int32_t xcp_router_pad_close(xcp_router *r, int32_t handle)
{
    if (!r) return -1;

    xcp_router_note_pad_close(r, handle);

    if (!r->host || !r->host->pad_close) return -1;
    return r->host->pad_close(handle);
}

void xcp_router_apply(xcp_router *r, int32_t handle, xcp_pad_data *out, int32_t num)
{
    int i;

    if (!r || !out || num <= 0) return;

    /* Never touch a handle we do not own. */
    if (handle != r->p1_handle) return;

    /* The Xbox pad is the point of this plugin: if it is here, use it. */
    if (!r->xbox_present || !r->xbox.connected) return;

    switch (r->cfg.policy) {
    case XCP_XBOX_ONLY:
        for (i = 0; i < num; i++) {
            apply_state_to_pad_data(&r->xbox, &out[i]);
        }
        break;

    case XCP_MERGE:
        for (i = 0; i < num; i++) {
            merge_state_into_pad_data(&r->xbox, &out[i]);
        }
        break;

    case XCP_XBOX_WHEN_ACTIVE:
    default:
        if (!xbox_is_active(r)) return; /* idle Xbox: the DS4 keeps the slot */
        for (i = 0; i < num; i++) {
            apply_state_to_pad_data(&r->xbox, &out[i]);
        }
        break;
    }

    r->reads_injected++;
}

/*
 * The shared body of scePadRead and scePadReadState, for hosts that let us do
 * the read for them (the PC mock). Returns the host's return code so callers
 * can pass it straight through to the game.
 *
 * `num` is 1 for scePadReadState.
 */
static int32_t read_common(xcp_router *r, int32_t handle, xcp_pad_data *out,
                           int32_t num, int is_state)
{
    int32_t ret;

    if (!r || !r->host) return -1;

    if (is_state) {
        if (!r->host->pad_read_state) return -1;
        ret = r->host->pad_read_state(handle, out);
    } else {
        if (!r->host->pad_read) return -1;
        ret = r->host->pad_read(handle, out, num);
    }

    r->reads_seen++;

    /* Never touch a failed read. */
    if (ret < 0) return ret;

    xcp_router_apply(r, handle, out, num);
    return ret;
}

int32_t xcp_router_pad_read(xcp_router *r, int32_t handle, xcp_pad_data *out, int32_t num)
{
    if (num <= 0) return -1;
    return read_common(r, handle, out, num, 0);
}

int32_t xcp_router_pad_read_state(xcp_router *r, int32_t handle, xcp_pad_data *out)
{
    return read_common(r, handle, out, 1, 1);
}

int32_t xcp_router_get_controller_information(xcp_router *r, int32_t handle, void *out_info)
{
    if (!r || !r->host || !r->host->get_controller_information) return -1;

    /*
     * We do NOT fake this. The handle is a real DS4 handle, so the real answer
     * is the correct answer. If we ever move to virtual handles this becomes
     * the place to synthesise a response.
     */
    (void)handle;
    return r->host->get_controller_information(handle, out_info);
}

/* ---------------------------------------------------------------------------
 * Introspection
 * ------------------------------------------------------------------------ */

int xcp_router_handle_is_p1(const xcp_router *r, int32_t handle)
{
    if (!r) return 0;
    return r->p1_handle >= 0 && handle == r->p1_handle;
}

void xcp_router_describe(const xcp_router *r, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!r) {
        snprintf(out, out_size, "router=null");
        return;
    }

    snprintf(out, out_size,
             "handles=%d p1_handle=%d p1_user=%d fg_user=%d xbox=%s "
             "policy=%d target=%d reads=%lu injected=%lu opens=%lu",
             r->handle_count,
             (int)r->p1_handle,
             (int)r->p1_user_id,
             (int)r->foreground_user,
             r->xbox_present ? "up" : "down",
             (int)r->cfg.policy,
             (int)r->cfg.target,
             r->reads_seen,
             r->reads_injected,
             r->opens_seen);
}
