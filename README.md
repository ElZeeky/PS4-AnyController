# PS4-AnyController

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Payload binary: GPL-3.0](https://img.shields.io/badge/payload_binary-GPL--3.0-orange.svg)](NOTICE)
[![Firmware: 9.00, 11.00 and 13.52](https://img.shields.io/badge/firmware-9.00_%7C_11.00_%7C_13.52_seen_working-informational.svg)](#requirements)
[![Platform: PS4 · GoldHEN](https://img.shields.io/badge/platform-PS4_%7C_GoldHEN-003791.svg)](#requirements)
[![Discord](https://img.shields.io/badge/Discord-join-5865F2.svg)](https://discord.gg/4TtyCpdgFq)

Use a **wired controller** (see [Compatibility](#compatibility) for which ones are confirmed) as **PLAYER 1** on a jailbroken **PS4** — in every screen:
home, settings, sign-in and games. A real DualShock 4 can stay connected and usable.

It runs as a **GoldHEN payload** (`AnyController.elf`) and creates a **virtual DualShock 4**
through Sony's own virtual-device API, so the system itself sees your controller
everywhere, not just inside a game. Confirmed working on a real firmware 11.00 console;
the current build also creates the pad through the direct API, without the older callsite
patch, on 11.00. Tester logs show the same direct API path creating and feeding the pad on
firmware 9.00 (a GameSir T4 receiver in Switch Pro mode) and on firmware 13.52 (a GameSir
Nova Lite receiver in Xbox 360 mode).

> Based on [crucifix86/xbox_controller_plugin](https://github.com/crucifix86/xbox_controller_plugin)
> (MIT). That project gives the pad player **2**; this is about player **1**. See [NOTICE](NOTICE).

---

## Features

- **System-wide** — works on the home screen, in settings, at sign-in, in notifications and
  in games, not only in-game.
- **A real virtual DS4** — the console treats it like a DualShock 4 plugged in over USB;
  nothing is faked at the game level, and your real DS4 keeps working.
- **You pick the user** — the device is created *unassigned*, so the console's own
  "Who's using this controller?" prompt decides the profile.
- **Safe by design** — it only ever reports what the controller sends; if `sceUsbd` or the
  virtual-device API is unavailable it says so and stops, touching nothing.
- **No permanence** — the optional firmware-specific patch lives only in the payload
  process's RAM; nothing is written outside `/data/`.
- **Unknown HID gamepads too** — if the report descriptor declares buttons, sticks and a
  hat, the fallback decodes it by field offsets; no clone-specific table is needed.
- **Testable off-console** — a PC inspector exercises the same decoder (105 tests / 639 checks).

---

## Requirements

- A PS4 on firmware **11.00** (confirmed, including the direct API path), or **9.00** or
  **13.52** (direct API path seen working in tester logs), with **GoldHEN v2.4b18.10 or
  later**.
- A **wired controller**. The decoder understands the Xbox 360, Xbox One/Series,
  DualSense/DualSense Edge, Switch PDP and Switch Pro (`057E:2009`, report `0x30`) layouts,
  including clones that use them. Unknown HID gamepads are also tried from their report
  descriptor when the interface exposes a standard gamepad collection. Decoding a layout is
  not the same as a confirmed console run; see [Compatibility](#compatibility).
- On the **Manba One V2**, going from Switch mode back to wired PC leaves its face bits
  rotated until the pad is power-cycled; the payload compensates for that one transition.
  A clean PC connection, or a pad with another USB vendor id (for example the GameSir
  T4's own Xbox mode), keeps the normal layout.
- **Known not to work yet:** the DualSense (on every firmware tried so far the USB read aborts
  straight after the claim; see [Compatibility](#compatibility)), the Xbox 360 *wireless*
  receiver, and PS3 pads. Wheels, keyboards and mice are not supported, and the virtual pad is
  input-only: no rumble, gyro or haptics.
- A DS4 (or equivalent) to boot the console and start GoldHEN; it is optional afterwards.
- A way to copy one file over FTP (e.g. FileZilla).

---

## Compatibility

Only the rows marked **confirmed** have been observed driving the PS4. Everything else is
either partial, unsupported, or waiting for the indicated hardware run. PC decoder tests are
not promoted to console confirmation.

The full per-result table - firmware, GoldHEN, controller mode, connection and evidence - is
in [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md). Summary:

- **Confirmed working:** Manba One V2, Xbox 360 mode, 2.4 GHz receiver, PS4 11.00,
  GoldHEN v2.4b18.10+ (exact build not logged).
- **Partially working:** Manba One V2, Switch Pro mode, same console; pad created and fed but
  physical face-button labels are still unconfirmed.
- **Unsupported:** DualSense/Edge output features (adaptive triggers, haptics, LEDs, mic,
  battery, touchpad coordinates); DS4-mode pads are left to the console's native driver.
- **Untested:** DualSense and Edge basic input, Xbox One/Series GIP, 8BitDo, GameSir T4/
  "GameSir 2", Generic HID/Logitech F310, Machenike G1 and firmware 13.52.

Two tool limitations, not controller support claims: Linux `hidraw` does not expose Xbox
XInput/GIP input, so use the payload log there; and `--play` cannot replay a Generic HID
capture because the capture does not carry the report descriptor needed to rebuild its map.

---

## Install

1. Grab `AnyController.elf` from a Release (or build it — see [Building](#building)).
2. Copy it to `/data/payloads/` on the console.
3. GoldHEN → Utilities → **Payloader LaunchPad** → run it, then press a button on your
   controller to answer the console's "Who's using this controller?" prompt.
4. Optional: add it to AutoRun so it starts on boot — see
   [`docs/DEPLOY-PS4-PAYLOAD.md`](docs/DEPLOY-PS4-PAYLOAD.md).

`./build.sh package` assembles a ready-to-drag **`release/`** folder (payload + `INSTALL.txt` +
Windows and Linux inspectors).
Full walkthrough: [`docs/DEPLOY-PS4-PAYLOAD.md`](docs/DEPLOY-PS4-PAYLOAD.md).

### Good to know

- **Touchpad click** — the DualSense's own click passes straight through; on a controller
  without one, the virtual DS4's click is fired by an **L3+R3 chord** (program a back button
  to an `L3+R3` macro). The Xbox Series **Share** button also works (as the DS4 Share).
- **Stopping it** — the payload has no in-console stop; **reboot** to stop it for good.
  Turning the controller off removes the virtual pad but leaves the payload running.

### DualSense and DualSense Edge

The wired standard DualSense (`054C:0CE6`) and DualSense Edge (`054C:0DF2`) use USB
input report `0x01` (64 bytes). Basic input is **implemented and PC-tested, but not yet
confirmed on a console**: both sticks, both analog triggers, face buttons, shoulders,
D-pad, L3/R3, PS, Create/Options and the touchpad click. Edge pads use the same basic
profile; the Edge's Fn and paddle bits are left unmapped unless you remap them to
standard buttons in the controller's own profile. The physical touchpad click passes
through as a held button.

Not supported: adaptive-trigger effects, haptics, lightbar/player LEDs, mic mute,
battery state and touchpad coordinates. The virtual pad API used here is input-only
(`OPEN-13`), so there is no path for game output to reach the physical controller.
DualSense basic input still needs one real 9.60/console validation run; the exact
capture sweep is in `PROGRESS.md` section 5l.

### Third-party controllers

The payload uses existing wire formats rather than guessed ids: Xbox 360
(`FF/5D/01`), Xbox One/Series GIP (`FF/47/D0`), Switch Pro (`057E:2009`) and
descriptor-gated Generic HID for D-Input pads. 8BitDo pads are expected to work through
their Xbox, Switch or D-Input modes, but that is a class/id expectation, not a console
confirmation. A GameSir T4 Cyclone Pro in Xbox mode should use Xbox 360 or GIP if it
exposes the standard interface; the exact "GameSir 2" model still needs to be
identified. If a pad exposes a vendor-specific HID report instead, send an
`AnyController-Inspector --capture` dump before a profile is added.

---

## How it works

GoldHEN payloads run inside an existing system process, where Sony's own virtual-device API
(`scePadVirtualDevice*`, the one Remote Play uses) is available. The payload reads your
controller's reports over `sceUsbd` and feeds them into a **virtual DualShock 4**, so the
system treats it exactly like a DS4 plugged in over USB.

The current build first raises the payload's credentials and calls
`scePadVirtualDeviceAddDevice` directly; AnyPad PS4 showed this works on 13.52 without a
callsite patch, and this payload has now created and fed a pad the same way on 11.00. If
the direct path does not produce a klog event on 11.00, the payload falls back to the
guarded in-memory `libScePad` patch that was confirmed there. The device is created
**unassigned**, so the choice of user stays on the console.

The build products use the `xcp_` prefix. The full engineering log — what is verified versus
assumed, and what is still open — is in [PROGRESS.md](PROGRESS.md).

---

## Building

Everything goes through `build.sh`. It finds a portable Zig automatically and falls back to
`clang`/`ld.lld` if present.

```bash
scripts/fetch_vendor.sh      # clone the reference repos at pinned revisions
./build.sh test              # run the 98 unit tests
./build.sh cli               # build the PC inspector for this machine (host/)
./build.sh cli-linux         # cross-compile the static Linux inspector
./build.sh payload           # cross-compile the payload -> build/ps4/AnyController.elf
./build.sh package           # assemble release/ (payload + both inspectors)
```

`./build.sh payload` needs **Zig**, **Python** and the **OpenOrbis** toolchain
(`.tools/oo/OpenOrbis/PS4Toolchain`, release v0.5.4). It clones the public `ps4-payload-sdk`
for its CRT *sources* on first use — that CRT is GPL-3.0, so the payload binary is GPL-3.0
even though the sources here are MIT (see [NOTICE](NOTICE)).

`scripts/fetch_vendor.sh` clones the reference sources; the two toolchains are not scripted,
so download them yourself:

- [OpenOrbis v0.5.4 `toolchain-llvm-18.tar.gz`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz)
  — extract to `.tools/oo/OpenOrbis/PS4Toolchain`
- [Zig 0.17.0 win64](https://ziglang.org/download/0.17.0/zig-x86_64-windows-0.17.0.zip)
  — extract to `.tools/zig`

---

## PC inspector

`AnyController-Inspector` captures a controller and decodes everything without a console. No
hidapi, no libusb, no Zadig, no driver swap — Windows uses SetupAPI, Linux uses
`/dev/hidraw` directly.

Double-click it (or run it with no arguments) and it scans for a controller, captures 30 seconds
of raw reports, writes `AnyController-capture-XXXX-YYYY.txt` next to the executable, then
decodes it automatically and prints the buttons and stick ranges it saw. Press every button and
move both sticks while it runs. That file is what to send when reporting a new pad.

```bash
./build/AnyController-Inspector                # capture + analyze automatically
./build/AnyController-Inspector --capture 45   # capture only, explicit duration
./build/AnyController-Inspector --list         # what is plugged in and what we make of it
./build/AnyController-Inspector --selftest     # synthetic reports through the whole pipeline
./build/AnyController-Inspector --mock         # the player-1 policy decisions, no hardware
./build/AnyController-Inspector --play FILE    # decode a capture or a payload log
./build/AnyController-Inspector --live 20      # Windows only: read the real pad, print coverage
```

On Linux, use `build/AnyController-Inspector-linux-x86_64`, or the packaged
`setup-linux.sh` / `run-linux.sh` pair. `setup-linux.sh` installs one udev rule so
`/dev/hidraw*` is readable without sudo; `run-linux.sh` starts the tool from its own folder.
Xbox 360/One/Series in XInput/GIP mode usually do not expose `/dev/hidraw`, so use the payload
log `/data/AnyController.log` with `--play` for those. A Switch Pro / `057E:2009` capture is
decoded from its full 64-byte `0x30` report; `samples/switch-pro-reports.txt` is a known-good
playback fixture. A Generic HID/D-Input capture cannot be replayed by `--play`, because the
capture does not carry the report descriptor needed to rebuild its field map; the tool says
so explicitly instead of guessing. If a capture contains no input reports, it is removed and
the tool reports whether the interface was readable but silent (including the XInput/GIP and
Linux `hidraw` cases).

`--live` is a real cross-check: Windows exposes the pad's semantic state through XInput,
whose button mask is byte-identical to the Xbox 360 wire mask, so the tool packs it into a
real report and runs it through the same decoder the console runs. See
`samples/xbox360-reports.txt` for a worked example of the byte layout, and
`samples/dualsense-reports.txt`, `samples/xbox-series-reports.txt` and
`samples/switch-pro-reports.txt` for the newer profiles.
Capture files carry their VID/PID in the header, so `--play` picks the right protocol
even when two pads share the same report id or length.

Captures contain only controller state, but may include a `# path:` line with a local device
path. Delete it before sharing. Send firmware logs and `.bin` dumps privately, never in a public
issue; see [`packaging/DUMPER.txt`](packaging/DUMPER.txt).

---

## Layout

| Path | Contents |
|---|---|
| `core/` | pure C, no PS4 headers: HID report → platform-neutral pad state |
| `usb/` | open/read/close: `usb_sceusbd` (PS4), `usb_mock` (tests), `usb_common` |
| `router/` | the player-1 decision (`router.c`), every platform call injected |
| `mocks/` | a fake scePad/sceUserService, plus report-packing helpers |
| `host/` | the PC-side tool (`pc_tool.c`, `hid_capture.c`, shipped as `AnyController-Inspector`) |
| `ps4/` | the payload (`payload/`), Orbis glue (`src/`), `build_payload.sh`, the libScePad patch, plus the `hello/` and `dumper/` diagnostic payloads |
| `packaging/` | `INSTALL.txt`, `DUMPER.txt`, `INSPECTOR.txt`, and the Linux inspector helpers used to assemble `release/` |
| `tests/` | 98 tests, 592 checks, no test-framework dependency |
| `samples/` | hex report dumps for `--play` |
| `docs/` | deployment guides and `docs/logs/` (real console run logs) |

---

## Safety

- The payload only ever reports what the controller sends; it never presses anything by
  itself.
- If `sceUsbd` or the virtual-device API is unavailable, it says so and stops; it never
  touches your existing controllers.
- With the controller absent, no virtual device is created at all — the console is untouched.
- Nothing is written outside `/data/`, and nothing is permanent — the libScePad patch lives
  only in the payload process's RAM.

Deploying is deliberately a manual step; nothing here touches your console on its own.

---

## Community

Want to help the project — testing on other firmware or controllers, bug reports, or code?
Join the [Discord](https://discord.gg/4TtyCpdgFq).

---

## Credits & licence

The HID → OrbisPadData mapping derives from
[crucifix86/xbox_controller_plugin](https://github.com/crucifix86/xbox_controller_plugin)
(MIT). Full credits and third-party licences: [NOTICE](NOTICE).

The sources in this repository are **MIT** ([LICENSE](LICENSE)). The compiled payload binary
contains the GPL-3.0 `ps4-payload-sdk` CRT and is therefore **GPL-3.0**.
