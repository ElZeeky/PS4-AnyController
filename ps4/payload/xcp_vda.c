/*
 * ps4/payload/xcp_vda.c - creating the virtual pad, feeding it, deleting it.
 *
 * Everything here is called from main.c's loop and nothing blocks.
 */
#include "xcp_vda.h"
#include "xcp_log.h"

#include "xcp/pad_state.h"

#include <orbis/libkernel.h>

#include <stddef.h>
#include <string.h>

/*
 * The motion block: quat (4 floats, identity for "not moving"), then vel, then
 * acell (3 floats each). It starts right after buttons + both sticks + the
 * analog buttons + padding, i.e. 12 bytes in.
 *
 * We deliberately do NOT name the motion fields anywhere. The OpenOrbis release
 * headers call them quat/vel/acell and the repository HEAD calls them
 * orientation/acceleration/angularVelocity, and the two asserts below pin the
 * layout without depending on either spelling: if a toolchain change moved them,
 * this file stops compiling instead of sending the system a corrupt sample.
 *
 * See PROGRESS.md 4.2 and OPEN-1.
 */
#define XCP_VDA_MOTION_OFFSET 12
#define XCP_VDA_MOTION_FLOATS 10 /* quat 4 + vel 3 + acell 3 */

typedef char xcp_vda_assert_sticks[(offsetof(OrbisPadData, leftStick) == 4) ? 1 : -1];
typedef char xcp_vda_assert_touch[(offsetof(OrbisPadData, touch) ==
                                   XCP_VDA_MOTION_OFFSET + XCP_VDA_MOTION_FLOATS * 4) ? 1 : -1];
typedef char xcp_vda_assert_size[(sizeof(OrbisPadData) == 120) ? 1 : -1];

/* --- library bring-up --------------------------------------------------- */

int xcp_vda_mbus_load(void)
{
    static const char *paths[] = {
        XCP_VDA_MBUS_PATH,
        "/system/common/lib/libSceMbus.sprx",
        "libSceMbus.sprx",
    };
    int32_t handle = 0;
    size_t  i;

    /*
     * sceKernelLoadStartModule, not dlopen. libkernel's dlopen returns NULL for
     * every spelling of this path in a GoldHEN payload (observed on 11.00); the
     * kernel's own module loader, however, is what the CRT itself uses to bring
     * up DT_NEEDED libraries, and it resolves these names.
     */
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]) && handle <= 0; i++) {
        handle = (int32_t)sceKernelLoadStartModule(paths[i], 0, NULL, 0, NULL, NULL);
        printf(XCP_TAG "sceKernelLoadStartModule(%s) = 0x%08X\n",
               paths[i], (unsigned)handle);
    }

    if (handle <= 0) {
        printf(XCP_TAG "libSceMbus did not load; not touching libScePad - its "
                       "imports from it would be unresolved\n");
        return -1;
    }
    printf(XCP_TAG "libSceMbus handle = 0x%08X\n", (unsigned)handle);
    return 0;
}

int xcp_vda_pad_init(void)
{
    int32_t ret;

    ret = scePadInit();
    printf(XCP_TAG "scePadInit() = 0x%08X\n", (unsigned)ret);
    if (ret != 0) {
        printf(XCP_TAG "scePadInit failed; nothing else can work\n");
        return -1;
    }

    /*
     * The order matters and was learned the hard way: calling this before
     * scePadInit returns SCE_PAD_ERROR_NOT_INITIALIZED (0x80920005) and does
     * nothing at all, which is a silent failure.
     */
    ret = scePadSetProcessPrivilege(1);
    printf(XCP_TAG "scePadSetProcessPrivilege(1) = 0x%08X\n", (unsigned)ret);

    return 0;
}

/* --- device lifecycle --------------------------------------------------- */

int32_t xcp_vda_add_device(int32_t user_id)
{
    xcp_vda_add_param param;
    unsigned         i;
    int32_t          ret;

    memset(&param, 0, sizeof(param));
    param.size    = (int32_t)sizeof(param);
    param.user_id = user_id;

    ret = scePadVirtualDeviceAddDevice(&param, XCP_VDA_DEVICE_TYPE);

    printf(XCP_TAG "AddDevice(size=%d user=%d type=%d) = 0x%08X\n",
           (int)param.size, (int)param.user_id, (int)XCP_VDA_DEVICE_TYPE,
           (unsigned)ret);

    /* Whatever came back, the device may exist: the handle is not in here. */
    for (i = 0; i < sizeof(param.pad) / sizeof(param.pad[0]); i++) {
        if (param.pad[i] != 0) {
            printf(XCP_TAG "  note: AddDevice wrote param.pad[%u] = 0x%08X\n",
                   i, (unsigned)param.pad[i]);
        }
    }
    return ret;
}

void xcp_vda_adopt(xcp_vda_device *out, uint64_t device_id)
{
    if (!out) return;

    memset(out, 0, sizeof(*out));
    out->device_id = device_id;
    out->handle    = (int32_t)(device_id & 0xFFFFFFFFu);
    out->owned     = 1;

    printf(XCP_TAG "device adopted: DeviceId=0x%llX handle=0x%08X\n",
           (unsigned long long)device_id, (unsigned)out->handle);
}

void xcp_vda_remove(xcp_vda_device *dev)
{
    int32_t ret;

    if (!dev || !dev->owned || dev->handle < 0) return;

    ret = scePadVirtualDeviceDeleteDevice(dev->handle);
    printf(XCP_TAG "DeleteDevice(0x%08X) = 0x%08X\n",
           (unsigned)dev->handle, (unsigned)ret);

    dev->owned     = 0;
    dev->handle    = -1;
    dev->device_id = 0;
}

/* --- ownership (libSceMbus) --------------------------------------------- */

/*
 * The two MBus entry points the login manager itself uses to give a device an
 * owner. OpenOrbis exports them from libSceMbus.so but has no header for them,
 * so they are declared here with the signatures the system log shows in use:
 *
 *   #LOGIN MGR# sceMbusBindDeviceWithUserId( 0x00000000000d0301, 0x10000000 )
 */
extern int32_t sceMbusBindDeviceWithUserId(uint64_t device_id, uint32_t user_id);
extern int32_t sceMbusDisconnectDevice(uint64_t device_id);

int32_t xcp_vda_bind_device(const xcp_vda_device *dev, uint32_t user_id)
{
    int32_t ret;

    if (!dev || !dev->owned) return -1;

    ret = sceMbusBindDeviceWithUserId(dev->device_id, user_id);
    printf(XCP_TAG "sceMbusBindDeviceWithUserId(0x%llX, 0x%08X) = 0x%08X\n",
           (unsigned long long)dev->device_id, (unsigned)user_id, (unsigned)ret);
    return ret;
}

int32_t xcp_vda_disconnect_device(uint64_t device_id)
{
    int32_t ret = sceMbusDisconnectDevice(device_id);

    printf(XCP_TAG "sceMbusDisconnectDevice(0x%llX) = 0x%08X\n",
           (unsigned long long)device_id, (unsigned)ret);
    return ret;
}

/* --- samples ------------------------------------------------------------ */

void xcp_vda_neutral(OrbisPadData *data)
{
    float *motion;

    if (!data) return;

    memset(data, 0, sizeof(*data));

    /* Identity rotation: the pad is not moving. Everything else in the motion
     * block stays zero, which is what "no accelerometer data" looks like. */
    motion = (float *)(void *)((unsigned char *)data + XCP_VDA_MOTION_OFFSET);
    memset(motion, 0, XCP_VDA_MOTION_FLOATS * sizeof(float));
    motion[3] = 1.0f;

    data->leftStick.x      = XCP_STICK_CENTER;
    data->leftStick.y      = XCP_STICK_CENTER;
    data->rightStick.x     = XCP_STICK_CENTER;
    data->rightStick.y     = XCP_STICK_CENTER;
    data->analogButtons.l2 = 0;
    data->analogButtons.r2 = 0;
    data->connected        = 1;
}

/* Rising, like a real pad's: 4 ms per sample, and a counter that wraps. */
#define XCP_VDA_SAMPLE_PERIOD_US 4000

static uint64_t g_sample_stamp;
static uint8_t  g_sample_count;

int32_t xcp_vda_insert_state(xcp_vda_device *dev, const xcp_pad_state *state)
{
    OrbisPadData sample;

    if (!dev || !dev->owned || dev->handle < 0) return -1;

    xcp_vda_neutral(&sample);

    if (state && state->connected) {
        sample.buttons          = state->buttons;
        sample.leftStick.x      = state->left_x;
        sample.leftStick.y      = state->left_y;
        sample.rightStick.x     = state->right_x;
        sample.rightStick.y     = state->right_y;
        sample.analogButtons.l2 = state->left_trigger;
        sample.analogButtons.r2 = state->right_trigger;
    }
    /* connected stays 1 even with no input source: this IS the console's pad,
     * and a virtual pad that reports itself unplugged disappears from the UI. */

    g_sample_stamp += XCP_VDA_SAMPLE_PERIOD_US;
    g_sample_count++;
    sample.timestamp = g_sample_stamp;
    sample.count     = g_sample_count;

    return scePadVirtualDeviceInsertData(dev->handle, &sample);
}
