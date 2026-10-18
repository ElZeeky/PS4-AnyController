/*
 * ps4/payload/xcp_pad_patch.c - retarget the IPC call inside
 * scePadVirtualDeviceAddDevice so it returns 0 instead of 0x803B0006.
 *
 * The mechanism, in one picture (all offsets from the function's own address):
 *
 *   scePadVirtualDeviceAddDevice            (libScePad + 0x5B40)
 *     ...
 *     vda+0x00B9  mov  esi, 0xC018482A       ; message id
 *     vda+0x00BE  xor  eax, eax              ; "0 vector args"
 *  -> vda+0x00C0  call <dispatch thunk>      ; ORIGINAL: e8 33 a5 ff ff
 *     vda+0x00C5  mov  rcx, [rbx]            ; canary check, epilogue...
 *
 *   ... is replaced by:
 *
 *     vda+0x00C0  call <cave>                ; e8 1c d3 ff ff
 *
 *   and the cave, a dead 15-byte run of NOPs at libScePad + 0x2F21:
 *
 *     50            push rax                 ; realign rsp (see below)
 *     e8 11 d2 ff ff  call <dispatch thunk>  ; the exchange, unchanged
 *     58            pop  rax
 *     31 c0         xor  eax, eax            ; <-- the whole point
 *     c3            ret                      ; back into vda's epilogue
 *
 *   (The `call` sits at cave+1 because of the `push rax`, so its displacement
 *   is measured from cave+6, not cave+5 - getting that off by one lands the
 *   call one byte into the thunk, on its `and`, and faults.)
 *
 * Two details that are easy to get wrong:
 *
 *  * Stack alignment. The body of vda aligns rsp to 32 and the call at 0x00C0
 *    is made with rsp 16-aligned, exactly as the ABI requires, so the callee
 *    enters with rsp % 16 == 8. Entering the cave through the same call
 *    leaves rsp % 16 == 8 as well, and a second `call` from there would enter
 *    the dispatch function 8 mod 16 off, which faults the moment it touches a
 *    16-byte-aligned SSE slot. The `push rax` / `pop rax` pair restores the
 *    alignment the callee expects and is otherwise a no-op: rax holds 0 here.
 *
 *  * The dispatch target is a PLT-style thunk (`ff 25 ..`), not a body; the
 *    cave calls the thunk, not the body, so a future HLE hook at that GOT slot
 *    still sees the original call shape.
 *
 * The one thing the stub must NOT preserve is eax - discarding the service's
 * answer is the entire purpose.
 *
 * These byte values are FW 11.00's; they are constants because both relocation
 * displacements are between fixed offsets inside one module and do not depend
 * on where that module lands.
 */
#include "xcp_pad_patch.h"
#include "xcp_log.h"

#include <orbis/libkernel.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Offsets inside libScePad.sprx, firmware 11.00. Verified against a live dump. */
#define XCP_PAD_VDA_OFF      0x5B40u /* scePadVirtualDeviceAddDevice */
#define XCP_PAD_CALLSITE_OFF 0x5C00u /* the IPC call inside it            */
#define XCP_PAD_CAVE_OFF     0x2F21u /* 15-byte NOP pad, dead bytes       */
#define XCP_PAD_DISPATCH_OFF 0x138u  /* PLT thunk the call targets        */

#define XCP_PAD_PAGE_SIZE 0x4000u
#define XCP_PROT_RWX      0x07
#define XCP_PROT_RX       0x05

/* The instruction we expect to find at vda + 0x00C0: `call <thunk>` with the
 * displacement FW 11.00 happens to use. Only used to prove the module is the
 * one these offsets were measured on; the replacement is built at run time. */
static const uint8_t kOriginalCall[5] = { 0xE8, 0x33, 0xA5, 0xFF, 0xFF };

/* Same declaration the dumper uses; only its address is taken here. */
extern void scePadVirtualDeviceAddDevice(void);

static int g_patched;

static int prot_page(uintptr_t addr, int prot)
{
    uintptr_t page = addr & ~(uintptr_t)(XCP_PAD_PAGE_SIZE - 1);
    int32_t   ret  = sceKernelMprotect((const void *)page, XCP_PAD_PAGE_SIZE, prot);

    if (ret != 0) {
        printf(XCP_TAG "mprotect(%p, %#x, %d) = 0x%08X\n",
               (const void *)page, (unsigned)XCP_PAD_PAGE_SIZE, prot,
               (unsigned)ret);
    }
    return ret;
}

/* A `call rel32` from `from` to `to`; the ABI's last byte is from+5. */
static void emit_call(uint8_t *out, uintptr_t from, uintptr_t to)
{
    int32_t rel = (int32_t)((intptr_t)to - (intptr_t)(from + 5));

    out[0] = 0xE8;
    memcpy(out + 1, &rel, sizeof(rel));
}

int xcp_pad_patch_vda(void)
{
    uintptr_t vda;
    uintptr_t base;
    uintptr_t dispatch;
    uint8_t  *callsite;
    uint8_t  *cave;
    uint8_t   stub[10];
    uint8_t   patch[5];
    size_t    i;

    if (g_patched) return 0;

    vda = (uintptr_t)(void *)scePadVirtualDeviceAddDevice;
    if (vda < 0x10000u) {
        printf(XCP_TAG "vda patch: scePadVirtualDeviceAddDevice did not resolve "
                       "(%p)\n", (void *)vda);
        return -1;
    }

    base     = vda - XCP_PAD_VDA_OFF;
    callsite = (uint8_t *)(base + XCP_PAD_CALLSITE_OFF);
    cave     = (uint8_t *)(base + XCP_PAD_CAVE_OFF);
    dispatch = base + XCP_PAD_DISPATCH_OFF;

    printf(XCP_TAG "vda patch: vda=%p base=%p callsite=%p cave=%p\n",
           (void *)vda, (void *)base, (void *)callsite, (void *)cave);

    /* Trust nothing. If the module is not the one these offsets were measured
     * on, we must not write a single byte: a wrong patch here is a crash in a
     * system process, and it would happen on someone's console, not ours. */
    if (memcmp(callsite, kOriginalCall, sizeof(kOriginalCall)) != 0) {
        printf(XCP_TAG "vda patch: call site does not match FW 11.00 (got %02X "
                       "%02X %02X %02X %02X); not patching\n",
               callsite[0], callsite[1], callsite[2], callsite[3], callsite[4]);
        return -2;
    }
    for (i = 0; i < sizeof(stub); i++) {
        if (cave[i] != 0x90) {
            printf(XCP_TAG "vda patch: cave bytes are not NOP padding (got "
                           "%02X at +%u); not patching\n", cave[i], (unsigned)i);
            return -3;
        }
    }

    /* Build both instructions now, from the addresses we actually have, so the
     * displacements can never disagree with where the bytes land. */
    stub[0] = 0x50; /* push rax */
    emit_call(&stub[1], (uintptr_t)&cave[1], dispatch);
    stub[6] = 0x58; /* pop  rax */
    stub[7] = 0x31; /* xor  eax, eax */
    stub[8] = 0xC0;
    stub[9] = 0xC3; /* ret */
    emit_call(patch, (uintptr_t)callsite, (uintptr_t)cave);

    if (prot_page((uintptr_t)cave, XCP_PROT_RWX) != 0) return -4;
    if (((uintptr_t)callsite & ~(uintptr_t)(XCP_PAD_PAGE_SIZE - 1)) !=
        ((uintptr_t)cave & ~(uintptr_t)(XCP_PAD_PAGE_SIZE - 1))) {
        if (prot_page((uintptr_t)callsite, XCP_PROT_RWX) != 0) return -5;
    }

    memcpy(cave, stub, sizeof(stub));
    memcpy(callsite, patch, sizeof(patch));

    /* Put the code pages back to r-x; the bytes stay, the W bit does not. */
    (void)prot_page((uintptr_t)cave, XCP_PROT_RX);
    (void)prot_page((uintptr_t)callsite, XCP_PROT_RX);

    g_patched = 1;
    printf(XCP_TAG "vda patch: applied (cave stub + retargeted call)\n");
    return 0;
}
