# PROGRESS — PS4-AnyController (xcp): Xbox pad → PS4 Player 1 (payload + plugin)

**Read this first at the start of every work session.** It is the shared memory between
sessions. Update it after every meaningful step.

> **Note (plugin):** the `.prx` plugin described below turned out untested and
> non-functional, so it is **not part of the published repository** — its sources stay
> local only (see `.gitignore`) and the project ships the payload only.

---

## 1. Goal

Two deliverables, one for each half of what the user wants.

**1. The plugin** (`.prx`, phases PC-* and PS4-1/PS4-2): a GoldHEN plugin for a
jailbroken **PS4 fw 11.00** so that an Xbox-style controller (**Manba One V2**, on a
2.4 GHz receiver) works as **PLAYER 1** inside games.

**2. The payload** (phase PS4-3, the user's actual priority): the pad must work
**everywhere - home screen, settings, sign-in, notifications and games** - not only
inside a game. A `.prx` cannot do that: GoldHEN loads plugins into the *game* process
only, and the system screens belong to processes that never load a plugin. The payload
creates a **virtual DualShock 4** with Sony's `scePadVirtualDevice*` API (the one Remote
Play uses), which the whole system sees as a genuine pad.

The DS4 is only needed to boot the console and start GoldHEN; after that the virtual pad
is what the user plays with. The receiver is plugged into the **console**, and the
payload reads it there over `sceUsbd` - no PC in the loop.

Reference implementation whose mapping we preserve: `crucifix86/xbox_controller_plugin`
(MIT). That one assigns the Xbox pad to **player 2**.

---

## 2. Current phase

**PC-1, PC-2, PC-3 and PS4-1 are DONE.** The plugin builds cleanly and the artifact has
been inspected.

**PS4-3 (the payload) is DONE AND CONFIRMED WORKING ON THE CONSOLE.** On a fw 11.00
GoldHEN console the Manba One V2 now drives the system **as player 1 in every screen** —
home, menus and games — through a virtual DualShock 4 the payload creates. This was
reached in stages, each one proved by a run the user sent back:

1. AddDevice returned `0x803B0006` universally. Fixed with a **code-cave patch inside
   libScePad's `scePadVirtualDeviceAddDevice`** (see 4.10).
2. The virtual pad then fought a **read/disconnect loop** that created and deleted the
   device dozens of times a second. Fixed with **read timeout + hysteresis** (see 4.13).
3. The pad now appears and behaves. The **touchpad click** was added via an L3+R3 chord,
   because the Manba's M1..M4 turned out to be firmware macros that emit no bits of their
   own (see 4.12).

Confirmed by runs `docs/logs/xcp_payload4.log` .. `xcp_payload7.log` (see 5e). The plugin
remains a per-game fallback and its first console run (PS4-2) is still pending.

**PC-4 / P4 (inspector validation) is done for PC playback; Linux `hidraw` capture is still
not hardware-tested.** The Linux binary uses `/dev/hidraw` + sysfs, captures and analyzes
automatically, and ships with `setup-linux.sh` / `run-linux.sh`. The Windows inspector now
auto-analyzes the capture too, and `--play` understands the payload's `opened VID:PID` line.
P4 added a Switch Pro playback fixture, a long-line check for the 256-token parser, and
empty-capture cleanup on Windows. The release evidence table is
[`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md); see 5m.

**2026-10-23 — direct VDA path, Switch Pro and current-build warning (DIRECT PATH NOW
CONSOLE-TESTED ON FW 11.00).** The payload now tries the AnyPad-style route first: raise the
payload's credentials, verify the authenticated authid/caps, call
`scePadVirtualDeviceAddDevice` with type `1` and the AnyPad sentinel, and use a **5 s** klog
wait. If no event arrives **and** the firmware is 11.00, it applies the guarded callsite
patch and retries type `3` without the sentinel. The klog watcher connects to TCP
`127.0.0.1:3232` first and falls back to `/dev/klog`. The decoder now understands Switch Pro
`057E:2009` (report `0x30`, 64-byte captures) and Linux capture records the real
first-report length.

**Every payload log in `DUMPLOGS/tests from other people/` predates this build and must
not be used to judge the direct path or Switch Pro.** The newest payload links to **313,704
bytes**, including the endpoint-parser fix (4.15), the first-report/Switch-Y fixes from
the second console log (4.16), and interface-based detection plus Generic HID (4.17). The
direct path has created and fed virtual pads on FW
11.00 in Xbox mode (5f) and Switch mode (5h); the on-screen assignment prompt and gamepad
usability after assignment are still unconfirmed, and 13.52 is still a test target. The
11.00 callsite patch remains the fallback. Switch face buttons still need a capture with
those buttons actually pressed (5h).

**There is no in-console way to stop the payload once it is running**; see the new
section 10, which is the honest answer to the user's question.

| Phase | State |
|---|---|
| PC-1 three-layer refactor | done, tests green |
| PC-2 core unit tests | done — synthetic vectors throughout |
| PC-2 PC CLI | done — `--list`, `--selftest`, `--mock`, `--play`, `--live`; no-args capture now analyzes too |
| PC-3 mock scePad/user-service + edge cases | done — 24 router tests |
| PC-4 native Linux inspector | **implemented, cross-compiled; hardware test pending** — `/dev/hidraw`, `setup-linux.sh`, `run-linux.sh` |
| P4 inspector validation and release | **done for PC playback; Linux `hidraw` still needs real hardware** — Switch Pro fixture + real capture playback, long-line token check, empty-capture cleanup, compatibility table and release report |
| PS4-1 build the `.prx` | **done** — `build/ps4/xcp_controller.prx`, exports and imports verified |
| PS4-1 deployment guide | done — [`docs/DEPLOY-PS4.md`](docs/DEPLOY-PS4.md) |
| PS4-2 iterate from the user's klog | **blocked on the user running it once** (payload already works, so optional) |
| PS4-3 payload: virtual DS4, system-wide | **DONE AND WORKING on fw 11.00**; direct-path build links at **313,704 bytes** and awaits 13.52/on-screen confirmation |
| PS4-3 interface detection + Generic HID | **implemented, PC tests green; console test pending** — class signatures, detach/claim, audio/HID extras, descriptor-driven fallback (4.17) |
| PS4-3 direct VDA path (AnyPad-style credentials + type 1) | **console-verified on fw 11.00** — created + `first InsertData = 0` (5f); 13.52 and on-screen assignment still pending; type 3 + 11.00 patch remains fallback |
| Switch Pro `057E:2009` decoder | **console-verified** (5h): created pad, 400 reports, Y fix confirmed, no output errors/disconnect; face bits captured in 5i, physical order still to confirm |
| DualSense `054C:0CE6` / Edge `054C:0DF2` | **implemented, PC tests green; 9.60 capture + console run pending** — USB report `0x01`, basic input and physical touchpad pass-through (5l); output features are not possible via the input-only virtual pad |
| Third-party Xbox/GIP/Switch/HID pads | **class/descriptor based and PC-tested** — 8BitDo modes and standard GameSir interfaces reuse existing profiles; T4 Cyclone Pro Xbox and "GameSir 2" still need captures (5l) |
| PS4-3 libScePad VDA patch | **done and confirmed** — 4.10 |
| PS4-3 deployment guide | done — [`docs/DEPLOY-PS4-PAYLOAD.md`](docs/DEPLOY-PS4-PAYLOAD.md) |
| PS4-3 touchpad-click chord (L3+R3) | **done and confirmed** — 4.12 |
| Automated tests | `./build.sh test` → **98 tests / 592 checks / 0 failed** |

---

## 3. Environment (this machine)

| Thing | State |
|---|---|
| OS | Windows 10/11, driven through **Git Bash (MSYS)** |
| C compiler | none preinstalled. Portable **Zig 0.17.0** at `.tools/zig/zig.exe`; `build.sh` finds it automatically |
| `make` | **not available** — `./build.sh` is the single entry point |
| PS4 toolchain | OpenOrbis release **v0.5.4** extracted to `.tools/oo/OpenOrbis/PS4Toolchain`. Has `bin/windows/create-fself.exe`, 447 stub libs (`libScePad.so`, `libSceUsbd.so`, `libSceUserService.so`, `libkernel.so`, `libSceLibcInternal.so`, …), `link.x`, `include/` |
| GoldHEN SDK | `vendor/GoldHEN_Plugins_SDK`; needs `crtprx.o` + `libGoldHEN_Hook.a` built before linking a plugin |
| PS4 access | **never** touched by the agent. Deployment is a manual step by the user |
| Attached controller | **the Manba One V2 is currently plugged in and enumerating** (see 4.8) |

Pinned upstream revisions: [`third_party/REFS`](third_party/REFS).
Re-clone with `scripts/fetch_vendor.sh`. Everything under `vendor/` and `.tools/` is
gitignored.

### 3.1 Toolchain provenance

`vendor/` is fetched by script, but the two downloaded toolchains are not — only a
version number was recorded, which made a fresh clone unreproducible. Both URLs below
were checked against the artifacts already on this machine and returned byte-identical
sizes, so they are the exact sources these builds came from:

| Artifact | Source | Size |
|---|---|---|
| OpenOrbis `toolchain-llvm-18.tar.gz` | `https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz` | 158,688,666 |
| Zig 0.17.0 win64 | `https://ziglang.org/download/0.17.0/zig-x86_64-windows-0.17.0.zip` | 100,268,137 |

Drop the OpenOrbis tarball's contents at `.tools/oo/OpenOrbis/PS4Toolchain` (its `bin/windows`
on `PATH` plus `OO_PS4_TOOLCHAIN` exported is what `create-fself` needs) and unzip Zig to
`.tools/zig/`. Both paths are what `build.sh` looks for, so nothing needs configuring.

---

## 4. Verified facts (read from real headers/sources/behaviour — NOT assumed)

### 4.1 Which protocol the Manba One V2 speaks — **RESOLVED (was OPEN-3)**

Measured on this PC with `./build/xcp_inspector --list`:

```
hid   045E:028E  HID-compliant game controller   HID\VID_045E&PID_028E&IG_00\...
hid   045E:028E  USB input device                USB\VID_045E&PID_028E&IG_00\...
```

* **VID `0x045E`, PID `0x028E`** — Microsoft, the *wired Xbox 360 controller* id.
* The `IG_00` suffix is the XInput interface group, so the pad is bound to the Xbox
  driver and appears through XInput, **not** as a readable HID gamepad.

Consequences:
* The live code path on both PC and PS4 is the **Xbox 360** one
  (`XCP_CTRL_XBOX360`), i.e. fixed 20-byte reports, input on **EP 0x81**, rumble on
  **EP 0x01**, and **no GIP init handshake needed**. That removes the whole class of
  Xbox One bring-up failures.
* The GIP / Xbox One decoder stays in the tree for other pads but is not the one that
  matters for this hardware.
* An earlier web hint suggested the Manba One needed the Linux `xone` driver (GIP);
  that is *not* what this unit does in wired Xbox mode on this machine.
* **Confirmed 2026-10-11, and this is the case that matters:** with **no cable**, over
  its own 2.4 GHz receiver, the ids are identical - `045E:028E` with the `IG_00` XInput
  interface group - and `--live 3` found it in XInput slot 0 with the decoder agreeing on
  all 750 polls (exit 0). So the dongle advertises the *wired* Xbox 360 pad rather than
  Microsoft's wireless receiver (`0x0719`), which is what makes the 20-byte EP 0x81 path
  the right one, with no GIP handshake.
* It is a **composite** device: Windows lists it twice
  (`USB\VID_045E&PID_028E&IG_00` and `HID\VID_045E&PID_028E&IG_00`). See OPEN-10.

### 4.2 The two `pad.h` variants — **RESOLVED (was OPEN-1)**

| Source | Motion fields in `OrbisPadData` |
|---|---|
| OpenOrbis git HEAD | `orientation`, `acceleration`, `angularVelocity` |
| OpenOrbis **release v0.5.4** (what people actually install) | `quat`, `vel`, `acell` |

So the reference plugin is **not** broken: it compiles against the **release** headers,
which is what our build uses.

**Design consequence, and it matters:** the plugin must compile against either header set.
It does, because `xcp_pad_data` carries *only* `buttons`, `left_x/y`, `right_x/y`,
`left_trigger`, `right_trigger`, `connected` — every field that is spelled identically in
both variants. Motion, touch and the trailing unknown bytes are never named and never
rewritten; the platform layer always calls the real `scePadRead` first and copies back
only the managed fields.

### 4.3 Real Orbis button bit values
`include/orbis/_types/pad.h` — L3 0x0002, R3 0x0004, OPTIONS 0x0008, UP 0x0010,
RIGHT 0x0020, DOWN 0x0040, LEFT 0x0080, L2 0x0100, R2 0x0200, L1 0x0400, R1 0x0800,
TRIANGLE 0x1000, CIRCLE 0x2000, CROSS 0x4000, SQUARE 0x8000, TOUCH_PAD 0x100000.

There is **no SHARE and no PS button** in the enum. Upstream invents
`DS4_BUTTON_SHARE 0x0001` and `DS4_BUTTON_PS 0x010000`; those are **unverified guesses**
and stay flagged in `core/include/xcp/pad_state.h` (OPEN-2).

### 4.4 Real sceUsbd API
`include/orbis/Usbd.h` — as used. :warning: `sceUsbdDetachKernelDriver` is declared
**`(void)` and takes no parameters**; upstream calls it with `(handle, 0)`, which in C is
harmless but does nothing. We will not rely on it.

### 4.5 GoldHEN hook macros are real
`Utilities.h`: `HOOK_EXTERN`, `HOOK_INIT`, `HOOK(name)`, `HOOK32(name)`,
`HOOK_CONTINUE(name, type, ...)`, `UNHOOK(name)`, `klog` == `printf`.
`Detour.h`: `Detour_Construct/Detour_DetourFunction/Detour_Destroy/Detour_Stub`.
`Patcher.h`: `Patcher_*`.

### 4.6 The Xbox 360 mapping, decoded and pinned by tests
`xcp_stick_from_signed` inherits upstream's one-unit asymmetry around the centre
(`0 → 128`, `-1 → 127`, `+1 → 128`); the deadzone folds both back to 128. Full
deflection is exact (`-32768 → 0`, `+32767 → 255`). Deliberately reproduced, and pinned
by `tests/test_translate.c` so nobody "fixes" it by accident.

### 4.7 The XInput `wButtons` mask is byte-identical to the Xbox 360 wire mask
Both use 0x0001 d-pad up … 0x8000 Y. That is what makes `--live` a real cross-check: the
OS's semantic state can be packed into a 20-byte report and pushed through the very same
decoder the PS4 will run.

### 4.8 The DS4/Xbox merge rule
Upstream applies the deadzone per axis, so a `255 - y` Y-inversion lands the centre on 127
rather than 128. Reproduced. `xcp_pad_state_merge` uses OR for buttons, max for triggers,
and "the axis furthest from centre wins" for sticks, so a resting DS4's jitter cannot drag
an actively deflected Xbox axis back to the middle.

### 4.9 The virtual-device API (read from Sony's stubs + two working projects)

Everything here is what the payload is built on. The **API facts** come from the
OpenOrbis sysroot in this repo; the **call order and the log format** come from
Control4Free and SplashDown, both GPL-3.0, read for behaviour only - see NOTICE.

* `libScePad.so` really exports all four calls: `scePadVirtualDeviceAddDevice`,
  `…DeleteDevice`, `…InsertData`, `…GetRemoteSetting`, and `orbis/Pad.h` declares them
  (with an empty parameter list, so the payload declares its own prototypes).
* `libSceMbus.so` exists as a stub with the full `sceMbus*` surface, and `dlopen`/
  `dlsym` are in `libkernel.so` and `<dlfcn.h>`.
* The target is **a payload, not a plugin**: GoldHEN's plugin loader documents `[default]`
  and per-title sections, i.e. *titles*. The home/sign-in screens are system processes
  that never load a plugin, which is exactly why Control4Free is a payload too.
* **Order matters**: `dlopen(libSceMbus)` → `scePadInit()` →
  `scePadSetProcessPrivilege(1)` → `AddDevice`. libScePad imports from libSceMbus and
  those imports are unresolved in a payload, so calling into libScePad first can kill the
  process; and the privilege call fails with SCE_PAD_ERROR_NOT_INITIALIZED before
  `scePadInit`, which is a silent failure.
* **`AddDevice` returns a status, not a handle** (0x803b0006 even on success). The handle
  is the MBus DeviceId the login manager logs a moment later, and reading `/dev/klog` is
  the only way for a payload to learn it:
  `#LOGIN MGR# … SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x7030d][type:1][subType:2]`.
  subType 2 is the Remote Play pad; matching anything looser risks addressing a real pad
  that appeared in the same window.
* `/dev/klog` has a **single reader**. GoldHEN's log server only holds it while a client
  is connected, so the payload opens it around AddDevice and closes it immediately after.
* The sample `InsertData` takes is 120 bytes - the same `OrbisPadData` layout the plugin
  already uses. It needs a **rising timestamp and counter**; a constant one is what
  de-duplicating readers see as "no new data". Device type is **3**; created for **user 1**
  the device arrives unassigned, so the PS4 shows its own "Who's using this controller?"
  screen and the choice of user stays on the console.
* The ELF is not the same artifact as a `.prx`: it is a dynamically linked ELF loaded
  into a running system process by GoldHEN's PayLoader (port 9090) or AutoRun
  (`/data/GoldHEN/payloads.ini`, `/user/data/payloads/<name>.elf = 1`), and it needs a CRT
  whose `_start` calls `main()` and resolves the Sony imports by name. That CRT only
  exists in `ps4-payload-sdk` (**GPL-3.0**), which is a build-time dependency of the
  payload target and nothing else.
* Requirements: **GoldHEN v2.4b18.10 or later**. The virtual-device call is confirmed on
  firmware 10.01 and 11.02; on **11.00 it is now confirmed too** (this repo, 5e).

### 4.10 The libScePad code-cave patch — **CONFIRMED ON CONSOLE**

The whole payload hung on one fact: on this console
`scePadVirtualDeviceAddDevice` returned **`0x803B0006`** ("assignment screen pending")
instead of `0`, so `AddDevice` never really added anything. The call is not broken in the
kernel — it is the call *inside* the exported function that comes back non-zero and the
function returns it verbatim. So we retarget that single call so the service still runs
(the device IS created) but its status is discarded and `0` returned.

FW **11.00** offsets inside `libScePad.sprx`, all measured against a live dump and then
re-checked on the console:

| Thing | Offset |
|---|---|
| `scePadVirtualDeviceAddDevice` | `0x5B40` |
| the IPC `call` inside it | `0x5C00` (original bytes `E8 33 A5 FF FF`) |
| dispatch target (PLT thunk `ff 25 ..`) | `0x138` |
| cave: 15 dead NOP bytes after the func's `ret`, before the next prologue | `0x2F21` |

Mechanism (`ps4/payload/xcp_pad_patch.c`, `xcp_pad_patch_vda`): the call site becomes
`call <cave>`; the cave is a 10-byte stub `push rax; call <0x138>; pop rax;
xor eax,eax; ret`. Notes that matter and were got wrong before the fix:

* The inner `call` sits at **cave+1** (because of `push rax`), so its rel32 is measured
  from **cave+6**, not cave+5. Getting that off by one lands the call mid-instruction and
  faults. Both displacements are therefore **computed at run time** from the addresses we
  actually have (`emit_call`), never hard-coded.
* The `push/pop rax` exists only for **16-byte stack alignment**: vda makes the original
  call with `rsp % 16 == 8` on entry to the callee, and a second `call` from the cave would
  otherwise be 8 mod 16 off and fault on the first SSE access.
* Safety: before writing a byte we compare the call site to the known FW 11.00 bytes and
  every cave byte to `0x90`, and we `sceKernelMprotect` the two pages RWX then back to RX.
  On any mismatch we **refuse to patch** (a wrong patch here is a crash in a system
  process).
* The patch lives in this process's mapping of libScePad only, so it is not persistent and
  cannot leak into another boot; the created device is what persists in the system.

Confirmed: `vda patch: applied`, `AddDevice(...) = 0x00000000`,
`klog pad-added: SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x5030d]...`,
`device adopted: DeviceId=0x5030D handle=0x0005030D`.

### 4.11 The assignment prompt and the bind that we do NOT call

`AddDevice(type 3, user 1)` creates the device **unassigned**, so the PS4 raises its own
"Who's using this controller?" screen. Two ways were considered to clear it:

* `sceMbusBindDeviceWithUserId(deviceId, userId)` — signature confirmed from disassembly
  and from the login manager's own log line
  (`sceMbusBindDeviceWithUserId( 0x00000000000d0301, 0x10000000 )`), primary user id
  `0x10000000`. **In practice the call did not return** in our process (the log stopped
  right after `device adopted`), so it is **disabled** behind `XCP_VDA_TRY_BIND 0` in
  `main.c`. It may block or fault when made from a payload's hijacked process.
* **What we do instead:** we simply **press a button on the fed controller**, which answers
  the prompt. This is the current, working flow and needs no extra API.

`xcp_vda_bind_device`/`xcp_vda_disconnect_device` and `XCP_VDA_USER_PRIMARY 0x10000000`
exist in `xcp_vda.c`/`xcp_vda.h`, ready if a threaded bind is ever wanted.

### 4.12 M1..M4 and the touchpad click — **measured, not assumed**

The DS4 has one control the Xbox 360 report cannot express: the **touchpad click**
(`ORBIS_PAD_BUTTON_TOUCH_PAD = 0x100000`). The Manba One V2 has **M1..M4** back buttons,
so the obvious idea was to read those.

A dedicated capture (`docs/logs/xcp_payload5.log`, change-only logging, 400 reports) with
every button pressed one at a time proves otherwise:

* **No byte beyond the standard 360 layout ever changed** (bytes 14..19 stayed `00`), and
  **no unused bit** appeared (byte 3's spare `0x08` never set).
* The Manba One V2's M1..M4 are **firmware macros** ("can be programmed to different key
  combinations", per Manba's own manual; single-button mapping only for
  A/B/X/Y/LB/LT/RB/RT). They therefore emit **only existing buttons**, never a new HID
  usage. A PC would see exactly the same thing the console already sees, so a PC capture
  would add nothing.
* The controller's wired modes are **XInput / DirectInput** (`BACK+START` 5 s); the
  Manba One series is documented as **not supporting PS/Xbox consoles**, which is the
  whole reason this project virtualises a DS4 in the first place.

**Decision:** the touchpad click is emitted from a **chord, L3+R3 held together**
(`core/src/translate.c`, both `decode_xbox360` and `decode_xboxone`). The user programs a
back button (e.g. M3) to an **"L3+R3" macro**; pressing both sticks does the same. L3 and
R3 also still pass through on their own.

**Second finding, from the first chord run (`xcp_payload6.log`):** the macro does **not**
hold the pair — it emits a short **burst** (`L3`, then `L3+R3`, then `R3`, then neither),
so the raw chord is true for about one report (~8 ms) and a game never sees the click.
Fix (`ps4/payload/main.c`): **latch** the click on the first chord and hold it
(`XCP_TOUCHPAD_HOLD_US` = 250 ms) until neither stick click has been seen for that long,
and **mask L3/R3 out of the sample while latched** so the macro does not also fire them.
The timeout is evaluated **every loop iteration**, not only on a new report, so the click
cannot stick if the pad goes quiet right after one. The latch is also reset when the input
source opens or drops, so no state leaks between sessions.

Confirmed working in `xcp_payload7.log` (29 `touchpad click down` / 29 `up`, balanced).
Known trade-offs, documented rather than hidden: (a) a game that itself uses L3+R3 will
get the touchpad instead; (b) the macro emits one loose `L3` report (~8 ms) before the
chord, so a game may see a one-frame L3.

### 4.13 `sceUsbd` read: real error codes, and the churn fix

The sceUsbd interrupt transfer on EP 0x81 comes back with Orbis status codes, not libusb
numbers. Seen on the console as the pad is removed:

| Code | Meaning here |
|---|---|
| `0x802400FF` | transfer aborted / device going away |
| `0x80240004` | device gone (early stage) |
| `0x80240006` | device gone |
| `0x80240007` | **no data inside the timeout**; an idle pad can return it repeatedly. It is not a removal |

The earlier code treated **any** non-zero, non-`-7` result as a disconnect, and used a
**2 ms** read timeout. Together those turned ordinary gaps into "disconnects", and the
virtual pad was deleted and recreated many times a second (device ids climbing
`0x7030d → 0x8030d → … → 0x11030d` in `xcp_payload3.log`). Fixes:

* Read timeout raised to **16 ms** (`XCP_READ_TIMEOUT_MS`), longer than the pad's own
  report period, so a normal gap is a timeout, not an error.
* **Hysteresis:** a single failed transfer is not a removal. The source is only declared
  gone after **1 s with no decoded data** (`XCP_INPUT_DEAD_US`), so a hiccup no longer
  collapses the pad.
* The real error codes are now **logged** (`read: ep 0x81 ret=0x...`) so the constant is a
  fact, not a guess (closes the OPEN-8 guesswork for the disconnect path).
* Every non-capture run logs its own header and the first `InsertData(...)` result, so a
  single log line proves the virtual pad accepts data at all.

**Correction (4.18).** `0x80240007` was still falling through the "disconnect" branch in
the field build. `Falloconstantnotification.log` shows **3,445** `input source ready` and
**3,445** `input source really gone` lines, with **3,445** `opened 045E:028E` and only two
virtual-pad removals: the source was being torn down and reopened every ~1 s even though
the pad never left. `0x80240007` is now returned as `XCP_USB_NO_DATA`, exactly like a
timeout; only `0x802400FF/04/06` (and a real close/IO failure) counts as removal.

### 4.14 Direct VDA path, TCP klog and Switch Pro (current build; direct path console-verified on 11.00)

AnyPad PS4 shows on firmware 13.52 that a payload can create a virtual pad **without**
the 11.00 callsite patch. The current build implements that route natively:

* `xcp_vda_creds_raise()` saves the process credentials, sets authid
  `0x3800000000010003` and all 16 capabilities to `0xFF`; `xcp_vda_creds_restore()` puts
  the originals back. The result is read back and a zero authid is refused.
* `xcp_vda_add_device(user, 1, 1)` calls `scePadVirtualDeviceAddDevice` with type `1`
  and `0xDEADBEEF` in the unused parameter slots. `scePadInit` is retried with the
  original credentials if the raised call is refused.
* `main.c` waits **5000 ms** for a klog `DEVICE_ADDED` event. On firmware 11.00 only,
  no event triggers the known type `3` + callsite-patch fallback; other firmware logs
  that the direct path produced no event and does not patch blindly.
* `xcp_klog_watch` tries TCP `127.0.0.1:3232` first, then `/dev/klog`; drain sets
  `saw_marker`. The matcher requires `type:1` / `subType:2` and rejects a DeviceId
  whose low byte is zero, which is how AnyPad separates a virtual pad from a
  physical pad re-registering on a console port.
* Switch Pro `057E:2009` is decoded from report `0x30`: face buttons at `@3`, shared
  buttons at `@4`, D-pad/L/ZL at `@5`, 12-bit sticks at `@6..11` scaled with `>> 4`.
  The Manba One manual's Switch-mode table maps its physical A/B/X/Y to Switch
  B/A/Y/X, i.e. by **position**, which is what the standard positional decode does;
  a crossed result therefore needs a raw capture before any global swap is made.
  `sceusbd` sends the `0x80 0x02/03/04` handshake, output report `0x01` subcommands
  (`0x03` full mode, `0x40` IMU, `0x48` vibration, `0x30` player LED) and a 500 ms
  `0x10` keep-alive on the interrupt OUT endpoint.
* Linux capture records the real first-report length instead of a fixed placeholder;
  an empty capture is removed and treated as an error.

The direct path now has an 11.00 console log (5f): credentials, `AddDevice` type `1`,
klog adoption and `first InsertData = 0`, with no callsite patch needed. The Switch Pro
handshake and keep-alive still have no console log; that capture came from the PC
inspector, not the PS4.

### 4.15 Endpoint-array stride fixed (from the 5f log)

The 5f log exposed a real parsing bug:

```
iface 0: class FF sub 5D proto 01, 2 endpoint(s)
  ep 0x81 interrupt IN maxpacket 32 interval 4
  endpoint 1: bLength 0 is not a 7 byte descriptor, skipped
```

That `bLength 0` was not the device; it was our own walk reading `bRefresh` of the first
endpoint. The sysroot forward-declares `struct libusb_endpoint_descriptor`, so the code
had been advancing by a packed 7-byte USB descriptor stride. The runtime array is made of
**aligned libusb structs** (32 bytes on x86_64). The first endpoint happened to work; the
second was always skipped. That silently discarded the interrupt **OUT** endpoint, which
Xbox rumble and the Switch Pro handshake need.

The fix defines the full libusb ABI layout and indexes `alt->endpoint[j]` instead of
`raw + j * 7`. New payload: **298,160 bytes**. The next console log should show both
`ep 0x81 ... IN` and `ep 0x01 ... OUT`; that is the confirmation to look for.

### 4.16 First-report skip, Switch Pro Y and per-source capture (from 5g)

The second console log exposed three independent things:

* **Xbox startup cursor movement.** The first report after opening an Xbox 360 interface
  is synthetic: `00 80 FF 7F 00 80 FF 7F`, which the signed decoder maps to full up-left.
  Feeding it made the assignment cursor move once. `main.c` now drops the first successful
  report after opening an **Xbox** interface (`ignoring the first report (driver init
  state)`), then inserts the next real sample. A controller that is already holding a
  button repeats it in the next report, so nothing is lost. Switch is excluded: its first
  `0x30` packet can already carry a held face button (see 5h).
* **Switch Pro Y was inverted on console.** `xcp_config_default_switch_pro()` now sets
  `invert_left_y` and `invert_right_y`; the legacy Switch PDP path keeps its old
  no-inversion config. The 12-bit stick test now pins the inverted result, and a second
  test pins the default-config behaviour.
* **The 400-report capture budget is per input source.** Switching from Xbox to Switch
  in 5g consumed the whole budget on Xbox, so no raw Switch report was recorded.
  `xcp_capture_reset()` runs on every new input source; the next log will capture the
  Switch session too.

At that point the payload was **298,568 bytes**, `./build.sh test` was **75 tests / 402
checks / 0 failed**. Face-button mapping for the Switch dongle is still unresolved: 5g
contains no raw Switch input, and the user reports crossed face buttons. Do not guess;
capture it.

### 4.17 Interface detection, extra claims and Generic HID (2026-10-23)

The AnyPad comparison showed that a VID/PID table alone loses pads whose ids we cannot
list. Detection now uses the interface descriptor as a second signal:

* **Class signatures.** `FF/5D/01` is XInput (Xbox 360), `FF/47/D0` is GIP (Xbox
  One/Series), and HID class `03` with subclass/protocol `0/0` is a **candidate** Generic
  HID device. A known VID/PID still wins; Sony's pads are never reclassified as Generic HID.
* **A chosen interface, not "interface 0".** `sceusbd_enumerate()` reads the config
  descriptor, picks the interface with an interrupt IN endpoint and stores its number,
  class, subclass and protocol in `xcp_usb_device_info`. `open()` claims that interface.
* **Detach and extra interfaces.** Before claiming, the backend detaches the kernel driver
  through the real `sceUsbdDetachKernelDriver` symbol. On Xbox/GIP pads it also claims the
  audio and extra HID interfaces so the console does not take them; Sony devices are left
  alone. All claimed interfaces are released on close.
* **No pad before real input.** The payload creates the virtual pad only after the first
  report decodes to a state. A device that enumerates but never sends a valid report can no
  longer produce an empty, permanently "neutral" pad.
* **Generic HID fallback.** `core/src/hid_generic.c` parses the report descriptor: buttons
  (Usage Page 0x09), X/Y/Z/Rx/Ry/Rz axes and the hat switch. It accepts an unknown device
  only when the descriptor declares a Gamepad/Joystick/Multi-axis application collection,
  or has a hat plus axes plus at least four buttons; a mouse is rejected. Default button
  order is Android (15+ buttons) or DirectInput; `xcp_hid_set_button()` allows an override.
  Reports are decoded by bit offset, so no clone-specific packet table is needed.
* **Named profiles still win.** `xcp_usb_find()` scans past a Generic HID candidate before
  returning it, so an unknown HID keyboard cannot hide a known Xbox/Switch pad enumerated
  later.

`./build.sh test` is **98 tests / 592 checks / 0 failed**. The payload is **313,704 bytes**,
the plugin **127,184 bytes**. Still console-unverified: the detach ABI and extra claims,
the first real Generic HID pad, and the Switch face-button order.

### 4.18 Post-Switch Xbox face layout and the reconnect-notification loop (2026-10-23)

Two user-reported issues from the Manba One V2, both PC/console reproducible:

* **Face buttons after PC -> Switch -> PC.** The first PC session is correct, but after
  using Switch mode the next wired PC session reports `X`, `B`, `Y`, `A` on the wrong
  PS4 diamond. A power cycle fixes it, so this is pad state, not a
  different USB profile. `xcp_config_for_source(type, previous_type, cfg)` now enables
  `manba_post_switch_layout` only for `XBOX360` after `SWITCH`/`SWITCH_PRO`; that branch
  maps raw `A->Triangle`, `B->Cross`, `X->Square`, `Y->Circle`. A plain PC reconnect,
  a fresh boot and every other controller keep the normal map. This is a full
  permutation, not `swap_ab`/`swap_xy`.
* **Endless `XCP: Controller detected`.** The root cause was the `0x80240007` handling
  above. With the source no longer disappearing on an idle timeout, the notification
  cannot fire on every poll. The payload no longer emits the redundant
  `XCP: controller detected` toast at all; connection state remains in the log, and the
  one-time `virtual pad ready` / real `controller lost` toasts are unchanged. The plugin
  now uses the same **1 s** disconnect hysteresis and only shows its connection toast once
  per plugin load.

The timeout classification lives in `xcp_usb_status_is_timeout()` so the payload and
plugin cannot drift apart. On an idle timeout, the PS4 backend asks
`sceUsbdCheckConnected()` once a second: `0` means still connected (keep the pad),
a negative status means the idle pad was unplugged (release it). This is the one call
that still needs console confirmation, but it closes the "stale virtual pad after an
idle unplug" hole without reopening the churn.

`./build.sh test` is **98 tests / 592 checks / 0 failed**. The payload is **313,704
bytes**, the plugin **127,184 bytes**. Console test still needed: the exact transition
timing and the physical result of the remap.

---

## 5. Verified on PC (tests and tools actually run)

* `./build.sh test` → **98 tests, 592 checks, 0 failures**, warning-clean under
  `-Wall -Wextra -Wshadow -Wconversion`. (The counts below record earlier points in the
  project; the suite has grown since - Generic HID, interface classification and the
  DualSense/third-party cases are the latest additions.)
* `./build.sh cli` → warning-clean; `build/AnyController-Inspector` runs.
* `./build.sh cli-linux` → static `x86_64-linux-musl` ELF; compiled cleanly, no Linux
  host available here to run it yet.
* `--selftest` → 12/12 checks OK, including "unplugging the Xbox pad hands player 1 back
  to the DS4".
* `--play samples/xbox360-reports.txt` → 29/29 reports decode correctly, exit 0.
* `--play samples/xbox360-reports-bad.txt` → 3/3 rejected, exit 2 (intended).
* `--list` → works with no dependencies; found the real controller (see 4.1).
* `--live` against the **real controller**, with someone actually pressing it. Every button
  was exercised and the coverage table's "not seen" column came back **empty**: dpad
  up/down/left/right, start, back, L3, R3, LB, RB, A, B, X, Y. All four sticks swept the
  full `-32768..32767` on each axis, L2 and R2 both peaked at 255, and **the decoder agreed
  with XInput on every sampled state**. An earlier `--live 6` resting-state run was 1250
  polls with 0 mismatches but proved nothing about pressing; this one does. Closes OPEN-6.
* The comparison in that run is against `oracle_buttons()` in `host/xcp_inspector.c`, a table
  written out separately from `core/src/translate.c`, so a disagreement would have meant one
  of the two was wrong. It is a real cross-check, not a tautology — **except** for the
  Back/`XCP_BTN_SHARE` bit, where the oracle copies the same assumption the translator
  makes, so this run cannot close OPEN-2. Only the console can.

### 5b. Verified about the PS4 artifact (checked, not assumed)

* `./build.sh prx` completes and writes `build/ps4/xcp_controller.prx` (~91 KB).
* `xcp_controller.elf` is a valid `ELF 64-bit LSB pie executable, x86-64`.
* `xcp_controller.oelf` carries the **FreeBSD** OSABI, i.e. it was accepted as an Orbis
  module.
* `tools/elf_symbols.js` confirms the symbols GoldHEN's loader looks for are exported:
  `plugin_load`, `plugin_unload`, `g_pluginName`, `g_pluginDesc`, `g_pluginAuth`,
  `g_pluginVersion`, `module_start`, `_init` (at 0x20).
* The same tool confirms we really import what we claim to call: `scePadOpen/Close/Read/
  ReadState/GetControllerInformation`, eleven `sceUsbd*` entry points,
  `sceUserServiceGetForegroundUser`, `sceKernelSendNotificationRequest`.
* The compile-time assertions in `ps4/include/orbis_glue.h` passed, which independently
  re-checks every button bit against the real `ORBIS_PAD_BUTTON_*` enum at build time.
* The klog paste path is exercised end to end by the two committed dumps:
  `samples/klog-capture-example.txt` (5 reports, all 20 bytes, `xcp: ` prefix intact)
  decodes 5/0 and exits **0**; `samples/xbox360-reports-bad.txt` rejects 3/3 and exits
  **2**. A 19-byte line is named as rejected along with the sizes the decoder expects,
  which is what a wrapped or truncated paste looks like.

### 5c. Full-code review pass (this session)

Re-read every source file for correctness, not style. Three genuine defects were found and
fixed; the rest of the tree was already sound. After the fixes: `./build.sh clean &&
./build.sh all` exits 0 with **0 warnings**, **59 tests / 295 checks / 0 failed**;
`./build.sh prx` exits 0 (93,056 bytes). `--selftest` OK; `--play` on the three committed
dumps returns 0 / 0 / 2 as documented; all seven plugin exports still present in the ELF.

1. **`ps4/src/usb_sceusbd.c` freed the wrong pointer.** `sceUsbdGetDeviceList` returns
   a heap array; we cached device pointers into our own static `g_scan[]` but then called
   `sceUsbdFreeDeviceList(g_scan)` — freeing an array that was never returned by the API —
   while the real array leaked once per scan (and the scan repeats ~1/s while no pad is
   present). Fixed: keep the real `libusb_device **` and free exactly that before the next
   scan and on shutdown (`free_scan`).
2. **`ps4/src/usb_sceusbd.c` indexed devices inconsistently.** `info.path` was built
   from the position in the *bus* list, but `open()` looked the index up in our *cache*.
   One device skipped because its descriptor read failed would shift every later index, so
   `open()` could claim the wrong device (or go out of range). Fixed: the path now carries
   the cache index (`g_scan_count`), which only advances for devices we actually keep.
3. **`core/src/translate.c` could divide by zero.** `xcp_apply_deadzone` computed
   `127 - deadzone` as the rescale divisor; a caller passing `deadzone = 127` (the field is
   public and unvalidated, default 15) hit `magnitude = 128 > 127` and divided by zero.
   Guarded (`range <= 0` returns centre) and pinned by three new checks in
   `deadzone_snaps_inside_and_rescales_outside`.

The sceUsbd fixes cannot be exercised off-console (that backend only compiles into the
.prx), so they are reasoned-from-headers, not run: the review of `Usbd.h` confirms
`sceUsbdFreeDeviceList(libusb_device **list)` takes the array from `sceUsbdGetDeviceList`,
and the reference plugin (`vendor/xbox_controller_plugin/src/usb_xbox.c:172`) frees that
same array after opening, never a private copy.

### 5d. The payload (this session)

* `./build.sh payload --check` → exit 0, **no warnings**, compiling all ten payload
  sources with `-Wall -Wextra -Wshadow -Wconversion` against the real Orbis headers.
  That is a real check of the code, not of a link we cannot do here.
* The three compile-time asserts in `ps4/payload/xcp_vda.c` passed, so the pad sample's
  layout is now **proved by the compiler** rather than assumed: `leftStick` at offset 4,
  `touch` at offset 52 (which pins the whole motion block, without naming any motion
  field - the reason for the assert) and `sizeof(OrbisPadData) == 120`.
* The kernel-log parser is unit-tested: `tests/test_klog_line.c`, six tests including the
  negative cases that matter (a real pad's event must NOT be accepted).
* `./build.sh all` → 65 tests / 313 checks / 0 failed; `./build.sh prx` still exits 0 and
  the plugin's seven exports are still in the ELF. The `.prx` grew from 93,056 to
  109,888 bytes: that is the `-lSceLibcInternal` section-alignment padding shifting when
  one object was added, not new code (the `.text`/`.data.rel.ro` sections are aligned to
  0x4000, so one extra file can cost up to 16 KB of padding).
* `./build.sh payload` → exit 0, **0 warnings**, and the artifact checks the script runs
  on itself pass: `e_type = 3` (PIE), `_start` and `main` both defined,
  `248,624` bytes. **No Linux, no `make`, no Docker, no installed LLVM**: the SDK is
  cloned only for its seven CRT *sources*, which Zig compiles, and Zig's `ld.lld` links
  with the SDK's own `elf_x86_64.x` against the OpenOrbis stubs. Its sysroot and its
  empty `sce_stubs/*.c` are not used - the real symbol names were always in OpenOrbis.
* The linked ELF has **33 undefined imports: 23 Sony + 10 libc**. That is the intended
  shape, not a defect: the payload's imports are resolved when it is loaded into the
  hijacked process, exactly as the `.prx` plugin's `printf`/`memset` already are. Linking
  a libc *in* is not possible on this path anyway - OpenOrbis's `libc.a` is musl with TLS
  and the SDK's script has no `PT_TLS` segment.
* The USB backend no longer claims interface 0 blindly. `sceusbd_claim_input()` dumps
  the config descriptor (interface class/subclass/protocol and each endpoint's address,
  transfer type, packet size and interval) into the log, claims the first interface that
  actually has an interrupt IN endpoint, and remembers that endpoint for `read()`.
  Both builds still compile with **0 warnings** (`prx` 110,032 bytes; payload
  251,104 bytes with its artifact checks passing). The sysroot does not define
  `struct libusb_endpoint_descriptor` at all, so its layout is stated in the backend -
  and every entry is validated by its `bLength` before any field in it is trusted.
* **Not verified:** every runtime behaviour of the virtual-device API, whether the loader
  resolves those 10 libc imports in the system process it lands in, and whether the new
  interface/endpoint choice matches what the dongle really does. The first console run
  decides all three - and now prints enough to.

### 5e. Verified ON THE CONSOLE (fw 11.00, GoldHEN) - PS4-3

Driven by the user on the real console; each claim below is a line in a log committed
under `docs/logs/`.

* **The USB path is real.** `usb: 1 device(s) visible`, `opened 045E:028E as Xbox 360`,
  `input source ready: 045E:028E (Xbox 360)`. So the console's own `sceUsbd` sees the
  receiver and the decoder agrees (closes OPEN-9/OPEN-10 for this hardware).
* **The patch works** (4.10): `AddDevice = 0x00000000`, the klog shows the device-added
  event, the payload adopts `handle=0x0005030D`.
* **The virtual pad accepts input:** `first InsertData(handle=...) = 0x00000000`.
* **No more churn** (4.13): `xcp_payload7.log` has exactly **one** `AddDevice`, no
  create/delete loop, and only the disconnect burst at the end.
* **Clean teardown:** turning the controller off produced `read: ep 0x81 ret=0x802400FF`
  .. `input source really gone (1003 ms)` .. `DeleteDevice(0x0005030D) = 0x00000000`.
* **Reactive lifecycle works:** with the Manba absent (`xcp_payload-without-controller.log`) **no**
  virtual pad is created and **no** assignment screen appears - the console is untouched
  until a real controller is there to stand in for.
* **The touchpad click works** (4.12): `xcp_payload7.log` records 29 balanced
  `touchpad click down`/`up`.
* Artifact at this point: `build/ps4/xcp_payload.elf`, **273,440 bytes**; `./build.sh test`
  → **66 tests / 319 checks / 0 failed**.

Run logs, in order: `xcp_payload.log`, `xcp_payload1.log`, `xcp_payload2.log`,
`xcp_payload3.log` (the churn), `xcp_payload-without-controller.log` (no pad), `xcp_payload4.log`
(first success), `xcp_payload5.log` (M1..M4 capture), `xcp_payload6.log` (macro is a
burst), `xcp_payload7.log` (touchpad confirmed).

### 5f. Direct VDA path verified on FW 11.00 (2026-10-23)

One run, on the user's 11.00 console, with the 00:35 build (`DUMPLOGS/1.log`; local,
not committed). The 00:41 build differs only in the Switch Pro USB subcommands and the
stricter klog matcher, not in the direct path. The later 4.15 build also fixes the
endpoint-array stride found in this same log.

* **The direct path works on 11.00 too.** `credentials elevated
  (authid=0x3800000000010003)`, `scePadInit() = 0`, then
  `AddDevice(size=32 user=1 type=1) = 0x803B0006`. The non-zero return did **not** mean
  failure: the klog immediately announced
  `... [DeviceId:0x5030d][type:1][subType:2]`.
* **The handle is learned from klog:** `parsed DeviceId = 0x5030D`,
  `device adopted: DeviceId=0x5030D handle=0x0005030D`.
* **The pad accepts input:** `first InsertData(handle=0x0005030D) = 0x00000000`.
  The capture contains 59 Xbox 360 reports, decoded with 0 rejects; D-pad,
  Cross/Circle and both sticks at neutral were seen. This confirms the direct path is
  not just an `AddDevice` success followed by a dead handle.
* **No callsite patch was needed.** The 11.00 fallback never ran in this session.
* **The BCD firmware log is correct:** `firmware 11.00 (raw 0x11008001)`.
* **The log exposed the endpoint-walk bug:** the interface has 2 endpoints, but the
  second was reported as `bLength 0` because the code used a packed 7-byte stride. Fixed
  in 4.15; this run could still read input, but the OUT endpoint was invisible.
* **The session ended when the USB source disappeared:** `read: ep 0x81 ret=0x802400FF`
  (then `0x80240004`, `0x80240006`), followed by `input source really gone (1001 ms)` and
  `DeleteDevice(0x0005030D) = 0x00000000`. The log does not say whether this was a manual
  unplug or a spontaneous disconnect; that is the immediate open question.
* **Still not proven:** that the assignment prompt appeared on screen and that the pad
  worked in the menu/game after assignment. `bind skipped` only means the payload did not
  answer the prompt itself.

### 5g. Second console run: PC mode works, Switch mode drops (2026-10-23)

`DUMPLOGS/2.log` (local, not committed) used a build whose header says `00:35:07`; the
endpoint lines prove it already contained the 4.15 fix, so the Zig object cache had reused
the old timestamp. It ran in two stages.

**PC/Xbox mode - works, with one startup artifact.**

* Xbox 360 opened on `ep 0x01 OUT` / `ep 0x81 IN`; the direct path created the pad,
  adopted `0x5030D`, and fed it successfully.
* The first report after open is synthetic:
  `00 14 00 00 00 00 00 80 FF 7F 00 80 FF 7F ...`. The signed Xbox decoder maps
  `0x8000`/`0x7FFF` to `L(0,0)`, i.e. full up-left, so the assignment cursor moved once
  even though the controller was neutral. Fixed in 4.16 by dropping the first report.
* After that the session streamed normal neutral reports and the user reported the mode
  working on PC. The capture budget (400 changed reports) was fully used here.

**Switch mode - opened, then the dongle vanished.**

* The dongle re-opened as `057E:2009`; both endpoints were present, so the handshake had
  a real OUT endpoint this time.
* The first two OUT transfers failed with `ret=-2145124348` (`0x80240004`, no device) and
  `ret=-2145124346` (`0x80240006`, not found), `transferred=0`.
* No input arrived for 1452 ms; the payload logged `input source really gone` and deleted
  the pad. Later it re-opened Xbox 360 (likely the dongle switching/kicking back), got no
  input for 1008 ms, and deleted that pad too. There was no crash, assertion or reboot.
* Because the Xbox session had consumed all 400 capture slots, `2.log` contains **no raw
  Switch reports**. That is why the face-button mapping could not be checked from it.
* The user separately reported that Switch Pro **Y was inverted** (now fixed, 4.16) and
  that the face buttons did not match a PS4 pad (A/B/X/Y mapping still open). The Manba
  One manual says its Switch mode sends physical A/B/X/Y as Switch B/A/Y/X (positions),
  so the standard positional decode should already be right for a Manba One; the report
  may be a label expectation or a different/clone dongle. Capture, do not guess.
* The `0x80240004/06` pair means the device was gone, not that the payload rejected it.
  The immediate next test must start with the dongle **already in Switch mode** and leave
  it alone; if the output errors repeat, the dongle is resetting and that is the bug to
  chase, not the mapping.

### 5h. Clean Switch-mode run: input and Y fix confirmed (2026-10-23)

`DUMPLOGS/3.log` (local, not committed) is the retest the 5g note asked for. The dongle
was already in Switch mode when the payload started, and it was left alone.

* **Switch Pro works on console.** `057E:2009` opened on `ep 0x81 IN` / `ep 0x01 OUT`
  (both 64-byte packets), the direct path created the pad, klog named
  `DeviceId:0x7030d`, and the payload adopted `0x0007030D`.
* **No output errors and no disconnect.** Unlike 5g, there is no `0x80240004/06` burst and
  no `input source really gone`; the 400-report capture completed. This strongly supports
  the 5g diagnosis: the dongle was re-enumerating because the mode changed, not because
  the handshake was rejected.
* **Y inversion is fixed.** The first `0x30` sample has raw `left_y = 0xFF7`, which now
  decodes to `L(128,0)` (up); with the old config it would have been `L(128,255)`.
* **The first-report skip did not hurt Switch in this run.** It dropped `81 01 ...`, a
  non-`0x30` packet; the first real `0x30` packet was decoded normally. The skip is now
  restricted to Xbox types, so a first `0x30` packet carrying a held face button cannot
  be lost in the next build.
* **Face buttons are absent from this capture.** Across all 400 Switch reports the
  `buttons_right` byte only contains `0x00`, `0x40` (R) and `0x80` (ZR); the `left` byte
  contains L/ZL and D-pad only. There is no `0x01/0x02/0x04/0x08` (Y/B/A/X) anywhere, and
  `buttons_shared` is a constant `0x80`. So this run did not test the face buttons, or
  the pad did not emit them in Switch mode; it cannot settle the mapping.
* **Next test:** start in Switch mode, press **the four face buttons first**, one at a
  time with a clear pause, then the D-pad/shoulders. The per-source capture resets on
  open, so they will be in the first reports rather than after the 400-report budget.

### 5i. Switch-only face-button capture (2026-10-23)

`DUMPLOGS/4.log` (local, not committed) is a clean Switch-only run on the 02:10 build.
The device opened as `057E:2009`, the direct path adopted `DeviceId:0x6030D`, and
`first InsertData(handle=0x0006030D) = 0x00000000` proved this handle accepted data. The
capture contains face-button bits:

```
id 64: buttons_right = 0x04
id 96: buttons_right = 0x08
id 140: buttons_right = 0x01
id 172: buttons_right = 0x02
```

Then the same bits repeat in cycles (`0x01, 0x04, 0x08, 0x02`). No disconnect, no output
errors. The log does not record which physical button produced each bit, and the capture
has no timestamps, so the mapping is not yet settled from the log alone. Under the current
positional decode those bits become Circle, Triangle, Square, Cross respectively; under a
label-mapped decode they become Cross, Square, Triangle, Circle. One explicit per-button
answer (or a one-button run) decides between the two. Build at this point: **298,568
bytes**.

### 5j. P1 compatibility captures: GameSir, Machenike, Xbox Series X, F310 (2026-10-23)

Analyzed the captures under `DUMPLOGS/tests from other people/` with the inspector (no
new profiles were added; every conclusion below is what the bytes actually show).

* **GameSir Nova Lite, `057E:2009` (Switch mode).** The 396 KB capture is `Zikway Pro
  Controller`, 2000 reports, every one 64 bytes with report id `0x30`. That is the
  standard Switch Pro layout, so the existing `XCP_CTRL_SWITCH_PRO` profile handles it;
  no vendor-specific decoder is needed. The capture exercises only `buttons_right`
  bits `0x01/0x02/0x04/0x08` (Y/B/A/X) and `buttons_left` bits `0x40/0x80` (L/ZL);
  both sticks sweep their full ranges. It does **not** contain R/ZR, D-pad,
  Plus/Minus/Home/Capture or L3/R3, so it cannot confirm those physical labels, but
  the decode itself has no rejected reports and the standard mapping is already pinned
  by `switch_pro_full_report`. `057E:2009` is Nintendo's own Switch Pro id, so the
  id-based profile wins over Generic HID.
* **GameSir Nova Lite 2.4 GHz receiver, `3537:1040` / `3537:1041`.** `1040` is named
  `2.4G XBOX 360 For Windows` and exposes the `FF/5D/01` XInput interface. On Linux the
  `xpad` driver owns it, so the `hidraw` captures are empty; on the PS4 the interface
  signature already classifies it as `XCP_CTRL_XBOX360` (the 4-interface log in
  `Falloconstantnotification.log` did exactly that). No new id entry is required.
  `1041` is `Zikway HID gamepad`, but its 9-byte reports alternate
  `01 80 46 00 00 00 00 00 00` / `01 00 00 ...` with usage page 0: it looks like the
  receiver's battery/status stream, not controller input. It stays a
  descriptor-gated Generic HID candidate, and the generic parser rejects it because
  no gamepad collection/axes/buttons are declared. The new
  `gamesir_receiver_interfaces` test locks both classifications in.
* **Machenike G1, FW 13.52.** The log opens `045E:028E` as **Xbox 360**, reads 20-byte
  packets on EP 0x81, and the decoder accepts them; this is the console transport, not
  GIP. The separate Windows HID capture is 15 bytes and is the wrong transport, so it
  must not become a second Xbox 360 packet size. The actual blocker in that log is the
  old build's 11.00-only callsite patch failing on 13.52 (`got E8 A3 A2 FF FF`), followed
  by the expected `0x80240004/06` teardown. The current payload tries the direct
  type-1 path first and does not need that patch; this log predates it, so 13.52 needs
  a fresh run. Do not guess 13.52 offsets from it.
* **Xbox Series X controller, FW 13.52.** The log is only 367 bytes and says
  `usb: 0 device(s) visible`; it contains no enumeration, interface or input data. It
  proves only that this run had no visible USB source (not in USB mode, not connected,
  or the receiver was absent). It cannot be used to judge GIP support or 13.52. A
  complete log with the controller wired and in USB mode is required.
* **Logitech F310.** No capture, log or PDF was present in the supplied files. The
  D-Input model should be describable by the Generic HID parser if its report
  descriptor is the conventional DirectInput gamepad layout, but that is a hypothesis,
  not a measurement. Capture its descriptor plus one button/axis sweep before adding
  anything.

`./build.sh test` is now **98 tests / 592 checks / 0 failed**; 5k covers the P2
parser hardening and 5l covers the P3 DualSense/third-party work.

### 5k. P2 off-console hardening: Generic HID and Switch decode (2026-10-23)

Closed the bugs found while building the off-console test matrix. No physical
controller was available, so everything here is decoder-level only.

* **Short reports are rejected.** `report_fits()` compares the declared bit extent
  against the received length before any field is read. A report that stops in the
  middle of a field now returns `0` and leaves the previous `xcp_pad_state` intact;
  before, a truncated report was decoded as zeros and could centre sticks or release
  buttons. Empty reports and unknown report ids also return `0`.
* **Every L2/R2 bit is recomputed.** When a report carries trigger axes, the L2/R2
  mask bits are always included and the digital bits are set from the current
  threshold. Releasing a trigger therefore clears the bit; the previous code only
  or-ed presses in and could leave L2/R2 stuck. At exactly 30/255 the bit stays off.
* **Malformed descriptors are rejected.** The parser tracks collection depth and
  refuses a stray `End Collection`, an unbalanced `Pop`, a truncated short/long item,
  a zero `Report Size`, and a descriptor that ends with a collection still open.
  Keyboard collections still fail the gamepad/hat+axes+buttons gate.
* **Test matrix added.** `tests/test_hid_generic.c`: signed 8-bit axes, report ids
  that split buttons and axes, a hat with logical minimum 1, trigger press/release
  and the threshold, keyboard rejection, five malformed descriptors, and truncated
  reports (state preservation). `tests/test_translate.c`: Switch Pro at both 49 and
  64 bytes, explicit Y-axis orientation, a full PDP 7-byte report (face, shoulders,
  stick clicks, Home/Capture, D-pad and both sticks), plus Manba PC->PC versus
  Switch->PC decode checks. At the end of P2 the suite was **95 tests / 559 checks /
  0 failed**; 5l adds the P3 cases.
* **Unchanged conclusions.** `xcp_report_looks_valid()` still treats Switch Pro as
  `>= 13` bytes and the inspector still accepts the GameSir `057E:2009` capture
  (2000/2000 decoded) while rejecting the `3537:1041` status stream. These are
  decoder results, not proof that a specific pad works on a PS4.

### 5l. P3 DualSense and third-party input (2026-10-23)

No DualSense capture or 9.60 console run was available, so this section separates
what is already implemented and tested from what still needs the volunteer tester.

* **USB enumeration.** The standard wired DualSense is `054C:0CE6`; the Edge is
  `054C:0DF2`. Both use the same input protocol. On USB the device exposes four
  interfaces; interface 3 is class `03/00/00` with interrupt IN `0x84` (64-byte
  max packet) and interrupt OUT `0x03`. The existing scanner already picks a HID
  interface that has an interrupt IN endpoint, records interface 3 as preferred,
  claims it, and reads EP `0x84`. The scan also skips the audio interfaces because
  they have no interrupt IN. No DualSense-specific descriptor request is needed.
* **Input report.** Wired report `0x01` is 64 bytes. The decoder consumes the first
  11 bytes: sticks `[1..4]`, triggers `[5..6]`, buttons0 `[8]` (low nibble hat,
  high nibble square/cross/circle/triangle), buttons1 `[9]` (shoulders, digital
  L2/R2, create/options, L3/R3) and buttons2 `[10]` (PS, touchpad, mute, Edge
  extras). Sticks already match PS4 direction, so DualSense does not invert Y.
  A digital trigger bit with a zero analog value is promoted to full travel, as
  Linux `hid-playstation` does.
* **Touchpad fix.** The payload used to route every `XCP_BTN_TOUCHPAD` through the
  Manba-style 250 ms macro latch. That is correct for a firmware back-button
  macro, but wrong for the DualSense's real touchpad: release was delayed by up
  to 250 ms. DualSense now passes the touchpad bit straight through; the latch is
  only used for non-DualSense sources.
* **Tests added.** All eight D-pad directions, analog trigger press at 255 and
  release back to zero, the 64-byte prefix at 11 bytes, all face/shoulder/menu/
  touchpad bits, and an Edge report whose mute/Fn/paddle bits must remain
  unmapped. Interface classification now explicitly covers both DualSense PIDs.
* **DualSense Edge is a separate hardware target.** Basic input is identical, so
  it uses the same profile. The Edge's Fn and paddles normally appear in buttons2
  and have no DS4 equivalent; remap them to standard buttons in the pad's own
  profile if needed. Do not treat the shared PID classification as Edge hardware
  validation.
* **Not reproducible through the current virtual pad.** `scePadVirtualDevice*` is
  fed an input sample only; there is no output callback from the virtual pad to
  the USB device. Therefore adaptive-trigger effects, haptics, lightbar/player
  LEDs, mic mute/battery state and touchpad coordinates cannot be mirrored. The
  touchpad **click** is a button and does pass through. Output is tracked as
  OPEN-13; do not claim these features from input tests.
* **Volunteer capture is still blocking.** A 9.60 capture needs: wired DualSense,
  full 64-byte report `0x01`, one sweep of each stick to all four extremes, both
  triggers slowly to 255 and back, every face/shoulder/D-pad/L3/R3/PS/Create/
  Options/Touchpad button, and one Edge-only pass if available. On Windows run
  `AnyController-Inspector --capture 60` (or `--list` first); the capture file is
  named `AnyController-capture-054C-0CE6.txt`. A klog from the payload with
  `xcp: capture on` is also useful. Without it, the byte offsets above are the
  Linux-documented ones, not a measurement from that unit.
* **Third-party controllers.** No new vendor-id table entries were invented. The
  existing wire signatures cover the safe cases: Xbox 360 `FF/5D/01`, GIP
  `FF/47/D0`, Switch Pro `057E:2009`, and unknown HID class `03/00/00` only after
  the report descriptor passes the gamepad gate. Tests lock in 8BitDo Xbox mode
  (`045E:028E`), Switch mode (`057E:2009`), D-Input (`2DC8:6001` -> Generic HID),
  a boot-keyboard HID (`03/01/01` -> NONE), GameSir XInput/GIP ids, and DS4-mode
  clones (`054C:05C4` -> left to the console's own driver). A GameSir T4 Cyclone
  Pro in Xbox mode should therefore land on Xbox 360 or GIP **if it exposes the
  standard interface**; that and the unknown "GameSir 2" model still need a
  capture. Do not add guessed ids or packet sizes for them.

### 5m. P4 PC-inspector validation and release checks (2026-10-23)

* **Switch Pro playback works.** Added `samples/switch-pro-reports.txt` (three 64-byte
  `0x30` reports) and verified `--play` decoded 3/3 with 0 rejected. The real GameSir Nova
  Lite `057E:2009` capture (`DUMPLOGS/tests from other people/`, local only) decodes
  2000/2000 with 0 rejected; these are PC capture results, not console confirmation.
* **Generic HID playback is descriptor-limited.** The `3537:1041` capture (9-byte vendor
  status stream) is still rejected 8/8. `--play` now prints one explicit note that a
  Generic HID/D-Input capture needs its report descriptor to rebuild the field map. The
  console path already parses that descriptor; the PC capture format does not carry it, so
  no guessed replay was added.
* **Long lines are no longer cut at 512 bytes.** The 256-token parser now uses a 4096-byte
  line and work buffer. A synthetic 1420-byte line with 190 label tokens plus a 64-byte
  Switch Pro report (254 tokens) decoded correctly; before this change `fgets` truncated it.
  `--selftest` now checks the same 254-token case on every run.
* **Empty captures.** Windows now mirrors Linux: if zero input reports arrive, the partial
  file is removed and the tool explains that the interface was readable but silent, that
  XInput/GIP pads may be owned by their kernel driver, and that the console payload log is
  the fallback. Linux also says some pads expose only a vendor/control interface on
  `hidraw`. Empty-capture cleanup on Windows is code-reviewed but not hardware-tested here.
* **Linux capture is still not hardware-tested.** The static binary builds, and the
  `/dev/hidraw` path handles permissions, poll errors, zero reports, and first-report length;
  this machine has no Linux distro or attached hidraw controller. A volunteer must run
  `--capture` on a real DualSense/DualShock/Switch Pro and report whether a non-empty file
  appears. Do not mark it passed from the cross-build alone.
* **Compatibility table.** The canonical evidence table, with firmware, GoldHEN, controller
  mode and connection for every result, is in
  [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md). Only the Manba One V2 Xbox 360 run is
  confirmed; the Switch Pro run is partial; DualSense/Edge, GIP, 8BitDo, GameSir, Generic HID
  and 13.52 remain untested.
* **Mobile diagnostic tool.** Feasible on Android with USB host/OTG (raw HID access exists,
  but needs an app and per-device permissions); more restricted on iOS. Not implemented and
  not required for compatibility work. Revisit only if the PC inspector remains the bottleneck.
* **Release hygiene.** `DUMPLOGS/` is ignored as a whole; the release package copies only the
  explicit payload, dumper, inspector binaries and packaging docs. The ignored `ps4/src/`
  plugin files are not sources for the payload; the payload source list uses `usb_sceusbd.c`,
  `orbis_glue.c`, `core/src/*.c` and `ps4/payload/*.c`. The concise release report and
  artifact hashes are in [`RELEASE-REPORT.md`](RELEASE-REPORT.md); `./build.sh package`
  ships it with `COMPATIBILITY.md`.

---

## 6. Assumed / untested on console

**Most of this list is now closed by the PS4-3 console runs (5e).** What remains:

* The **plugin's** console behaviour (hook installation, the `scePadOpen` pattern games
  use) is still unverified - the payload working does not exercise the plugin path.
* Whether loading `libSceUsbd.sprx` in `hooks_init` is early enough for the plugin's own
  imports; one klog line decides it (plugin-only).
* The exact `sceUsbd` **timeout** status code (the disconnect/abort codes are now known,
  4.13; timeouts are inferred from behaviour).
* A **Switch-mode run that actually includes the face buttons**. 5h proved the clean
  Switch session works (pad created, 400 reports, no output errors), but its capture has
  no A/B/X/Y bits at all. Press the four face buttons first, before the 400-report budget
  is spent, then the sticks/triggers/D-pad.
* The **exact Switch face-button mapping** for the Manba dongle: standard positional
  (`A->Circle`, `B->Cross`, `X->Triangle`, `Y->Square`) versus label-mapped
  (`A->Cross`, `B->Circle`, `X->Square`, `Y->Triangle`). Do not change the default until
  a capture or an explicit per-button report settles it.
* The plugin exports `g_pluginName` / `plugin_load` etc. by **example** (the reference
  plugin, and other GoldHEN plugins) rather than by any declaration in the SDK.
* That the reference plugin works at all on 9.00 — we have only its README's word.
* `XCP_BTN_SHARE` / `XCP_BTN_PS` bit values (OPEN-2); the payload run does not settle them
  because no game has been asked to distinguish them.

Closed since this file was last updated (kept here so the change of status is visible):
real PS4 runtime/pad acceptance, the `sceUsbd` open/claim on the composite dongle, the
virtual-device API on fw 11.00, the payload's libc-import resolution, and the on-wire byte
order.

---

## 7. Open problems

| ID | Problem | Status |
|---|---|---|
| OPEN-1 | `OrbisPadData` motion field names differ between OpenOrbis HEAD and the release | **resolved** — design names none of them (4.2) |
| OPEN-2 | SHARE (0x0001) / PS (0x010000) are upstream guesses, absent from the Orbis enum | flag only; the `--live` oracle shares the assumption, so only the console can settle it |
| OPEN-3 | Which USB protocol the Manba One V2 uses | **resolved** — `045E:028E`, Xbox 360 XInput (4.1) |
| OPEN-4 | No `make` on this machine | mitigated: `build.sh` |
| OPEN-5 | `xcp_pad_data` deliberately omits motion/touch, so an Xbox-driven frame carries the DS4's motion data | acceptable; document it |
| OPEN-6 | `--live` has only been sampled at rest, so real button presses are unverified | **resolved** — full button + axis sweep, decoder agrees with XInput on every state (5) |
| OPEN-7 | On-wire byte order unconfirmed | **resolved on console** — the pad's own bytes now drive a pad the system accepts (5e) |
| OPEN-8 | `XCP_LIBUSB_ERROR_TIMEOUT` value assumed | **resolved for the disconnect path** — the real Orbis codes are logged (`0x802400FF/04/06/07`, 4.13); the timeout code itself is still only inferred from behaviour |
| OPEN-9 | ~~What VID:PID the receiver reports~~ | **resolved on PC and console** — `045E:028E`, Xbox 360 XInput (4.1); the console's `usb:` dump agrees (5e) |
| OPEN-10 | Whether `sceUsbdOpen` plus the interface claim succeeds on a **composite** receiver dongle, and which interface/endpoint carries the reports | **resolved on console** — interface 0 / EP 0x81, exactly as dumped (5e) |
| OPEN-11 | The virtual-device API on **fw 11.00** (confirmed on 10.01 and 11.02 only) | **resolved** — works on 11.00 with the code-cave patch (4.10, 5e) |
| OPEN-12 | Whether the in-process loader resolves the payload's 10 undefined **libc** imports (`printf`, `memset`, `snprintf`, `strstr`, `isxdigit`, ...) | **resolved on console** — the payload runs and logs (5e) |
| OPEN-13 | Motion, touch and the light bar of the virtual pad are neutral; rumble/light-bar **output** (GetRemoteSetting → the pad) is not implemented because the USB backend has no write path | design work, not a blocker |
| OPEN-14 | `sceMbusBindDeviceWithUserId` did **not return** from the payload process; the assignment prompt is answered by a button press instead | **works as-is**; a threaded bind is the alternative if ever wanted (4.11) |
| OPEN-15 | The touchpad chord has trade-offs: a game using L3+R3 gets the touchpad, and the macro leaks one loose `L3` report | accepted; the trigger is one small block in `main.c`/`translate.c` if it needs changing (4.12) |
| OPEN-16 | **No way to stop the payload once running** (infinite loop, no exit) | by design for now; options written up in section 10 |
| OPEN-17 | Switch face buttons crossed on the Manba dongle; 4.log has the bits but not the physical order | open — one explicit per-button answer decides positional vs label mapping; do not guess a global swap |
| OPEN-18 | `sceUsbdDetachKernelDriver` is called through a corrected prototype, and audio/extra-HID interfaces are now claimed | implemented (4.17); the detach ABI and the extra claims still need one console run |
| OPEN-19 | Generic HID uses descriptor offsets and a default button order; no real unknown pad has been decoded yet | PC tests green (4.17); console test with an unnamed HID pad still needed |
| OPEN-20 | The post-Switch face fix keys off the previous source type; the notification fix assumes `0x80240007` is a timeout | implemented and PC-tested (4.18); needs one PC->Switch->PC run and one idle console run to confirm |
| OPEN-21 | `sceUsbdCheckConnected()` must return `0` while the pad is present and a negative error after an idle unplug | implemented as the stale-pad guard (4.18); one console run with an idle unplug is required to pin the return values |
| OPEN-22 | Logitech F310 D-Input has no capture; Generic HID support is only a hypothesis for it | need one `--capture` with the report descriptor and a full button/axis sweep (5j) |
| OPEN-23 | GameSir `3537:1041` is a 9-byte vendor status stream; its descriptor was not captured | keep it descriptor-gated and rejected until a descriptor proves otherwise (5j) |
| OPEN-24 | Machenike G1 and Xbox Series X logs are FW 13.52 but predate the current direct VDA path | retest with the current payload before adding 13.52 patch offsets or a new profile (5j) |
| OPEN-25 | DualSense basic input has no capture from the 9.60 tester and no console run | decoder and USB interface path implemented (5l); need the capture/console sweep listed there |
| OPEN-26 | DualSense Edge is classified with the standard DualSense PID but its Fn/paddles/mute are unmapped and unvalidated | separate physical validation target (5l); basic input should match, extras stay unmapped |
| OPEN-27 | GameSir T4 Cyclone Pro in Xbox mode and the exact "GameSir 2" model have no captures | class-based Xbox 360/GIP handling should cover standard interfaces; do not add guessed ids until captured (5l) |

---

## 8. Layout

```
core/     pure C, no PS4 deps   HID report -> xcp_pad_state
usb/      open/read/close       usb_common.c + usb_mock.c + ps4 sceusbd
router/   router.c              player-1 assignment, host API injected as a vtable
mocks/    pad_mock.c            fake scePad/sceUserService (+ report_pack.c helpers)
host/     pc_tool.c, hid_capture.c  --list --selftest --mock --play --live
tests/    98 tests, no framework dependency
ps4/src/  plugin only           hooks.c, main.c (plugin ABI), orbis_glue.c
ps4/payload/ payload only        main.c (system-wide service), xcp_vda.c,
                                 xcp_pad_patch.c (libScePad code-cave), xcp_klog_watch.c
ps4/      build_prx.sh, build_payload.sh
samples/  hex report dumps for --play
```

Key design decision to remember: **player 1 is served by augmenting the game's own real
DS4 handle**, not by inventing a virtual handle. `scePadOpen` is recorded, and
`scePadRead`/`scePadReadState` are augmented only for the chosen handle. Games therefore
always see a genuine pad, and `scePadGetControllerInformation` never has to be faked.

---

## 9. Next steps

### The payload (PS4-3) - the user's actual goal

1. ~~**Build it.**~~ — **done**; `./build.sh payload` needs no Linux (clones the SDK's CRT
   sources on first use, compiles with Zig, links with `elf_x86_64.x`).
   `build/ps4/AnyController.elf` is current (**313,704 bytes**); the direct path has been
   fed once on 11.00, while Switch Pro, 13.52, the endpoint fix and Generic HID still need
   console runs.
2. ~~**First console run.**~~ — **done and working** (5e). The pad appears system-wide.
3. ~~**libScePad AddDevice fix.**~~ — **done**, the code-cave patch (4.10).
4. ~~**Touchpad click.**~~ — **done** via the L3+R3 chord (4.12).
5. **Remaining, all optional / user-driven:**
   * **AutoRun for every boot** (`/data/GoldHEN/payloads.ini` or
     `/user/data/payloads/<name>.elf = 1`), so it starts without the LaunchPad.
   * **Rumble / light-bar passthrough** (OPEN-13): needs a `sceUsbd` **write** path, which
     the backend does not have yet. `xcp_vda_get_remote_setting` would be the source.
   * A **threaded `sceMbusBindDeviceWithUserId`** to clear the assignment prompt
     automatically instead of the button press (OPEN-14).
   * A **clean stop mechanism** if the user wants one (OPEN-16, section 10).
   * A **configurable touchpad trigger** if L3+R3 turns out to clash (OPEN-15).
   * One run with an **unknown HID gamepad** to confirm the `iface`/`detach`/`claim` and
     `generic HID:` descriptor lines (OPEN-18/19), then the Switch face-button run
     (OPEN-17).

### The plugin (PS4-2) - the fallback

Still worth one run, and independent of the above: copy `build/ps4/xcp_controller.prx` to
`/data/GoldHEN/plugins/`, add it under `[default]` in `/data/GoldHEN/plugins.ini`, enable
klog, launch a game with the receiver in, and send back the `xcp:` and `xcpcapture` lines.
That decides OPEN-2 (SHARE/PS bits) and the plugin-specific items in section 6.
**Do not enable both at once**: the payload already gives the console a pad, and a plugin
rewriting a game's pad reads on top of it would double every input.

The policy defaults (`XCP_P1_FIRST_OPEN` + `XCP_XBOX_WHEN_ACTIVE`) are a deliberate choice,
not a measurement. All three policies are implemented and unit-tested, so if the console
run shows the Xbox pad should own player 1 outright, it is a one-line change in
`hooks_init` — and worth asking before changing, because it changes what the DS4 can do.

---

## 10. Stopping / disabling the payload (the user's question)

The payload is a `for (;;)` loop with **no exit condition** (`ps4/payload/main.c`, the
`for (;;)` after the bring-up). Once it is running it stays until the process dies. The
honest options today:

1. **Reboot the console — the definitive stop.** Clears the hijacked process, the
   libScePad patch (which only ever lived in that process's RAM mapping) and the virtual
   device.
2. **Turn the controller off.** The payload notices (`input source really gone`, 4.13) and
   calls `DeleteDevice(handle)`, so the virtual pad disappears and the console behaves
   normally again. **But the process keeps running** in its loop, ready if the pad returns —
   this "disables the effect", it does not stop the code.
3. **Kill the hosting process — not recommended.** It is a system process; killing it can
   destabilise the console, and if it dies without `DeleteDevice` the virtual pad can be
   left orphaned until a reboot.

There is **no built-in stop**. If one is wanted, these are the clean designs (none is
implemented, per the user's request to not change code):

* **Sentinel file.** Each loop, check for `/data/xcp_stop`; if present, `DeleteDevice` then
  leave the loop (return from `main`). Simple, no UI, easy to script over FTP.
* **Button combo on the fed pad.** E.g. hold **Guide + Options** for 3 s; on detection,
  `DeleteDevice` and return. Needs no extra file and is reachable from the couch.
* **Network command.** A small TCP listener that accepts a `stop` word; the payload
  already knows how to talk over the console's network stack indirectly, but this adds a
  socket dependency to weigh against the file sentinel.
* **Idle timeout.** Leave the loop if there is no input for N seconds. Avoids a stuck
  session but stops the pad on purpose too, so it is the least attractive option.

All of them must call `xcp_vda_remove(&dev)` (i.e. `DeleteDevice`) before returning, so the
system does not keep an orphaned virtual pad.

---

## 11. PC-4: Linux inspector and release packaging (this session)

The inspector is now one portable C tool with two capture backends:

* **Windows**: SetupAPI + overlapped `ReadFile` (unchanged).
* **Linux**: `/dev/hidraw` + `poll()`, sysfs enumeration, report-descriptor gamepad
  detection, name fallback, `setup-linux.sh` udev rule, `run-linux.sh` launcher. No
  hidapi/libusb; the shipped binary is static `x86_64-linux-musl`.

Both backends write `AnyController-capture-VID-PID.txt` next to the executable. No-args
now captures **and analyzes**; `--capture` is capture-only; `--play` accepts captures,
payload logs, and the payload's `opened VID:PID` line. `--list` enumerates HID/USB on
both platforms. `--live` remains Windows/XInput-only.

Build/package:

    ./build.sh test          # 98 tests / 592 checks
    ./build.sh cli           # native inspector
    ./build.sh cli-linux     # static Linux inspector
    ./build.sh package       # release/: payload, dumper, both inspectors, docs/scripts

`release/` now contains `AnyController-Inspector.exe`,
`AnyController-Inspector-linux-x86_64`, `INSPECTOR.txt`, `INSPECTOR-LINUX.txt`,
`COMPATIBILITY.md`, `RELEASE-REPORT.md`, `setup-linux.sh` and `run-linux.sh` (scripts
LF-only). The Linux binary was cross-compiled and inspected with `file` but **not run**
here; a volunteer is needed for hardware testing.

Privacy/legal: do not publish firmware logs or `.bin` dumps. Controller captures may
contain a `# path:` line; delete it before sharing. `.bin` dumps contain Sony code and are
only for firmware porting, never for controller compatibility.
