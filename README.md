# PS4-AnyController

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Payload binary: GPL-3.0](https://img.shields.io/badge/payload_binary-GPL--3.0-orange.svg)](NOTICE)
[![Firmware: 11.00](https://img.shields.io/badge/firmware-11.00-informational.svg)](#requirements)
[![Platform: PS4 · GoldHEN](https://img.shields.io/badge/platform-PS4_%7C_GoldHEN-003791.svg)](#requirements)

Use **any wired third-party controller** as **PLAYER 1** on a jailbroken **PS4** — in every
screen: home, settings, sign-in and games. A real DualShock 4 can stay connected and usable.

It runs as a **GoldHEN payload** (`xcp_payload.elf`) and creates a **virtual DualShock 4**
through Sony's own virtual-device API, so the system itself sees your controller
everywhere, not just inside a game. Confirmed working on a real firmware 11.00 console.

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
- **No permanence** — the firmware-specific patch lives only in the payload process's RAM;
  nothing is written outside `/data/`.
- **Testable off-console** — a PC inspector exercises the same decoder (66 tests / 319 checks).

---

## Requirements

- A PS4 on firmware **11.00** (also reported working on 10.01 / 11.02) with
  **GoldHEN v2.4b18.10 or later**.
- A **wired third-party controller**. The decoder understands the Xbox 360, Xbox One and
  Switch controller report layouts, including clones that use them.
- A DS4 (or equivalent) to boot the console and start GoldHEN; it is optional afterwards.
- A way to copy one file over FTP (e.g. FileZilla) — GoldHEN's server is on port **2121**,
  user/password `ps4`/`ps4`.

---

## Install

1. Grab `xcp_payload.elf` from a Release (or build it — see [Building](#building)).
2. Copy it to `/data/payloads/` on the console.
3. GoldHEN → Utilities → **Payloader LaunchPad** → run it, then press a button on your
   controller to answer the console's "Who's using this controller?" prompt.
4. Optional: add it to AutoRun so it starts on boot — see
   [`docs/DEPLOY-PS4-PAYLOAD.md`](docs/DEPLOY-PS4-PAYLOAD.md).

`./build.sh package` assembles a ready-to-drag **`release/`** folder (payload + `INSTALL.txt`).
Full walkthrough: [`docs/DEPLOY-PS4-PAYLOAD.md`](docs/DEPLOY-PS4-PAYLOAD.md).

### Good to know

- **Touchpad click** — your controller has none, so the virtual DS4's click is fired by an
  **L3+R3 chord** (program a back button to an `L3+R3` macro).
- **Stopping it** — the payload has no in-console stop; **reboot** to stop it for good.
  Turning the controller off removes the virtual pad but leaves the payload running.

---

## How it works

GoldHEN payloads run inside an existing system process, where Sony's own virtual-device API
(`scePadVirtualDevice*`, the one Remote Play uses) is available. The payload reads your
controller's reports over `sceUsbd` and feeds them into a **virtual DualShock 4**, so the
system treats it exactly like a DS4 plugged in over USB.

One firmware-specific wrinkle: on 11.00 `scePadVirtualDeviceAddDevice` returns an error code
even though the device *is* created, so the payload applies a small in-memory patch to that
one call (guarded, and only in its own process). The device is created **unassigned**, so the
choice of user stays on the console.

The build products use the `xcp_` prefix. The full engineering log — what is verified versus
assumed, and what is still open — is in [PROGRESS.md](PROGRESS.md).

---

## Building

Everything goes through `build.sh`. It finds a portable Zig automatically and falls back to
`clang`/`ld.lld` if present.

```bash
scripts/fetch_vendor.sh      # clone the reference repos at pinned revisions
./build.sh test              # run the 66 unit tests
./build.sh cli               # build the PC inspector (host/)
./build.sh payload           # cross-compile the payload -> build/ps4/xcp_payload.elf
./build.sh package           # assemble release/ for the console
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

`host/xcp_inspector` decodes and tests everything without a console. No hidapi, no libusb,
no Zadig, no driver swap — it uses the Windows APIs directly.

```bash
./build/xcp_inspector --list                  # what is plugged in and what we make of it
./build/xcp_inspector --selftest              # synthetic reports through the whole pipeline
./build/xcp_inspector --mock                  # the player-1 policy decisions, no hardware
./build/xcp_inspector --play FILE             # decode a captured report dump
./build/xcp_inspector --live 20               # read the real pad, then print coverage
```

`--live` is a real cross-check: Windows exposes the pad's semantic state through XInput,
whose button mask is byte-identical to the Xbox 360 wire mask, so the tool packs it into a
real report and runs it through the same decoder the console runs. See
`samples/xbox360-reports.txt` for a worked example of the byte layout.

---

## Layout

| Path | Contents |
|---|---|
| `core/` | pure C, no PS4 headers: HID report → platform-neutral pad state |
| `usb/` | open/read/close: `usb_sceusbd` (PS4), `usb_mock` (tests), `usb_common` |
| `router/` | the player-1 decision (`router.c`), every platform call injected |
| `mocks/` | a fake scePad/sceUserService, plus report-packing helpers |
| `host/` | `xcp_inspector`, the PC-side tool |
| `ps4/` | the payload (`payload/`), Orbis glue (`src/`), `build_payload.sh`, the libScePad patch, plus the `hello/` and `dumper/` diagnostic payloads |
| `packaging/` | `INSTALL.txt` used to assemble `release/` |
| `tests/` | 66 tests, 319 checks, no test-framework dependency |
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

## Credits & licence

The HID → OrbisPadData mapping derives from
[crucifix86/xbox_controller_plugin](https://github.com/crucifix86/xbox_controller_plugin)
(MIT). Full credits and third-party licences: [NOTICE](NOTICE).

The sources in this repository are **MIT** ([LICENSE](LICENSE)). The compiled payload binary
contains the GPL-3.0 `ps4-payload-sdk` CRT and is therefore **GPL-3.0**.
