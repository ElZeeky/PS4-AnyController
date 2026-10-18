# PS4-AnyController (xcp) — Xbox pad as PLAYER 1 on a jailbroken PS4

Lets a wired Xbox-style controller (developed against a **Manba One V2**) drive **player 1**
on a PS4, **in every screen** — home, settings, sign-in and games — while a real DS4 stays
connected and usable. Built for **PS4 firmware 11.00** with GoldHEN.

It is a GoldHEN **payload** (`xcp_payload.elf`): it creates a **virtual DualShock 4**, so
the system itself sees the Xbox pad everywhere, not just inside a game. Confirmed working
on a real 11.00 console (see [PROGRESS.md](PROGRESS.md) §5e).

Based on [crucifix86/xbox_controller_plugin](https://github.com/crucifix86/xbox_controller_plugin)
(MIT) — see [NOTICE](NOTICE). That project gives the Xbox pad player **2**; this is about
player **1**.

---

## Install (payload)

The short version:

1. Build it or grab `xcp_payload.elf` from a Release.
2. Copy it to `/data/payloads/` on the console (FTP port 2121, `ps4`/`ps4`).
3. GoldHEN → Utilities → **Payloader LaunchPad** → run it. Answer the console's
   "Who's using this controller?" prompt by pressing a button on the Xbox pad.
4. Optional: add it to AutoRun so it starts on boot — see
   [`packaging/payloads.ini`](packaging/payloads.ini).

`./build.sh package` assembles a **`release/`** folder with the payload, an `INSTALL.txt`
and an AutoRun example, ready to drag over FTP.

Full guide: [`docs/DEPLOY-PS4-PAYLOAD.md`](docs/DEPLOY-PS4-PAYLOAD.md).

**Touchpad click:** the Xbox pad has none, so the virtual DS4's click is fired by an
**L3+R3 chord** (program a back button to an `L3+R3` macro). Full explanation in
PROGRESS.md §4.12.

**Stopping it:** the payload has no in-console stop; **reboot** to stop it for good.
Turning the pad off removes the virtual pad but leaves the payload running. See
PROGRESS.md §10.

---

## How the payload works, in one paragraph

GoldHEN payloads run inside an existing system process, where Sony's own virtual-device
API (`scePadVirtualDevice*`, the one Remote Play uses) is available. The payload reads the
Xbox pad's bytes over `sceUsbd` and feeds them into a **virtual DualShock 4**, so the
system treats it exactly like a DS4 plugged in over USB. One firmware-specific wrinkle: on
11.00 `scePadVirtualDeviceAddDevice` returns an error code even though the device *is*
created, so the payload applies a small in-memory patch to that one call (guarded, and
only in its own process) so the device is accepted. Details in PROGRESS.md §4.10.

The device is created **unassigned**, so the console shows its own "Who's using this
controller?" screen and you answer it from the controller — the choice of user stays on
the console.

---

## Layout

```
core/        pure C, no PS4 headers:  HID report -> platform-neutral pad state
usb/         open/read/close:         usb_sceusbd (PS4), usb_mock (tests), usb_common
router/      the player-1 decision:   router.c, with every platform call injected
mocks/       a fake scePad/sceUserService, plus report packing helpers
host/        xcp_inspector:           the PC-side tool
ps4/         payload (payload/), Orbis glue (src/), build_payload.sh, the
             libScePad patch (payload/xcp_pad_patch.c), plus the hello/ and
             dumper/ diagnostic payloads
packaging/   INSTALL.txt + payloads.ini used to assemble release/
tests/       66 tests, 319 checks, no test framework dependency
samples/     hex report dumps for --play
docs/        deployment guides; docs/logs/ - real console run logs
```

[PROGRESS.md](PROGRESS.md) is the running log: read it first, it records what is verified
versus assumed and what is still open.

---

## Building

Needs no `make` and no system toolchain. `build.sh` finds a portable Zig, and falls back to
`clang`/`ld.lld` if you have them.

```bash
scripts/fetch_vendor.sh      # clone the reference repos at pinned revisions
./build.sh test              # 66 unit tests
./build.sh cli               # the PC inspector
./build.sh payload           # the PS4 payload -> build/ps4/xcp_payload.elf
./build.sh package           # assemble release/ for the console
```

`./build.sh payload` needs Zig and Python plus OpenOrbis
(`.tools/oo/OpenOrbis/PS4Toolchain`, release v0.5.4) — it clones the public
`ps4-payload-sdk` for its CRT *sources* on first use (that CRT is GPL-3.0; see
[NOTICE](NOTICE)), so the payload binary is GPL-3.0 even though these sources are MIT.

`scripts/fetch_vendor.sh` clones the reference sources; the two downloaded toolchains are
not scripted, so get them yourself:

* [OpenOrbis v0.5.4 `toolchain-llvm-18.tar.gz`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz)
  — 158,688,666 bytes, extract to `.tools/oo/OpenOrbis/PS4Toolchain`
* [Zig 0.17.0 win64](https://ziglang.org/download/0.17.0/zig-x86_64-windows-0.17.0.zip)
  — 100,268,137 bytes, extract to `.tools/zig`

---

## Using the PC tool

No hidapi, no libusb, no Zadig, no driver swap — it uses the Windows APIs directly.

```bash
./build/xcp_inspector --list                  # what is plugged in and what we make of it
./build/xcp_inspector --selftest              # synthetic reports through the whole pipeline
./build/xcp_inspector --mock                  # the player-1 policy decisions, no hardware
./build/xcp_inspector --play FILE             # decode a captured report dump
./build/xcp_inspector --live 20               # read the real pad, then print coverage
```

`--live` is a real cross-check, not a demo: Windows gives us the pad's semantic state
through XInput, whose button mask is byte-identical to the Xbox 360 wire mask, so the tool
packs it into a real 20-byte report and runs it through the same decoder the console runs.

Try `--play samples/xbox360-reports.txt` for a worked example of the byte layout.

---

## Safety

The design makes the boring outcome the default one:

* The payload only ever reports what the controller sends; it never presses anything by
  itself.
* If `sceUsbd` or the virtual-device API is unavailable, it says so and stops; it never
  touches your existing controllers.
* With the pad absent, no virtual device is created at all — the console is untouched.
* Nothing is written outside `/data/`, and nothing is permanent — the libScePad patch
  lives only in the payload process's RAM.

Deploying is deliberately a manual step for you; nothing here touches your console.

---

## Licence

MIT. See [NOTICE](NOTICE) for the upstream projects this builds on and exactly what was
taken from each. The compiled payload binary contains the GPL-3.0 `ps4-payload-sdk` CRT and
is therefore GPL-3.0; the sources in this repository are MIT.
