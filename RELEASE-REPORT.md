# P4 release report

Date: **2026-10-23**  
Payload version string: **1.2.0-dev**  
Scope: PC-inspector validation, compatibility documentation and final release checks.

## Implemented changes

- **DualSense / DualSense Edge basic input** (`054C:0CE6` / `054C:0DF2`): USB report
  `0x01`, sticks, analog triggers, face/shoulder/menu buttons, D-pad and touchpad click.
  Edge extras stay unmapped. Physical touchpad click now passes through without the
  Manba-style 250 ms macro latch.
- **Third-party controller handling**: class signatures for Xbox 360 (`FF/5D/01`) and
  GIP (`FF/47/D0`), Switch Pro `057E:2009` report `0x30`, and descriptor-gated Generic
  HID. No guessed vendor IDs or packet sizes were added.
- **PC inspector validation**: Switch Pro playback fixture and real-capture playback;
  explicit Generic HID playback limitation; 256-token parser now has a 4096-byte line
  buffer and a self-test covering a 254-token, 64-byte report.
- **Empty captures**: Windows and Linux remove a capture containing zero input reports;
  messages explain that the interface may be readable but silent, that XInput/GIP pads
  may be owned by their kernel driver, and that the console payload log is the fallback.
- **Docs**: evidence-based compatibility table with firmware, GoldHEN, mode, connection
  and limitations; updated README, PROGRESS, deployment guide and packaged INSTALL.
- **Repository hygiene**: `DUMPLOGS/` is ignored as a whole and excluded from packaging.

## Verification

- `./build.sh test`: **98 tests / 592 checks / 0 failed**.
- `./build/AnyController-Inspector --selftest`: **OK**, including the 254-token dump line.
- `./build/AnyController-Inspector --play samples/switch-pro-reports.txt`: **3 decoded,
  0 rejected**.
- Real GameSir Nova Lite `057E:2009` capture: **2000 decoded, 0 rejected** (PC only).
- Generic HID `3537:1041` status capture: **0 decoded, 8 rejected**, with the explicit
  descriptor-needed note.
- Empty `3537:1040` capture: **0 decoded, 0 rejected**, reported as empty.
- No controller was attached to this machine (`--list`: 0 recognised), so no physical
  console or controller test was possible. Console results below are from existing logs.
- Linux inspector cross-build succeeded. Real `/dev/hidraw` capture is **not tested**;
  this machine has no Linux distro or attached Linux HID device.

## Artifacts

| Artifact | Size (bytes) | SHA-256 |
|---|---:|---|
| `build/ps4/AnyController.elf` | 313,704 | `a9fc15240ae31f8be9d503591d70da9314d08d29657910a4cf6e15084077fe72` |
| `build/ps4/xcp_controller.prx` | 127,184 | `cd06f9d02af103c8d0d40c3222ec3c3f66bd405484bef8648021fff361b560d1` |
| `build/AnyController-Inspector` (Windows PE) | 1,091,072 | `ea6b25ee02aaa3f496185f1ec7eadb267d999321991bb1097d5c64e8d04d5a34` |
| `build/AnyController-Inspector-linux-x86_64` | 5,388,944 | `7eb218d24f52adcbb8d8c6a4afafc9ba0d04f98aad24150addc646c1af4091cb` |

The payload was rebuilt from the explicit source list in `ps4/build_payload.sh`
(`core/src/*.c`, `usb_common.c`, `usb_sceusbd.c`, `orbis_glue.c`, `ps4/payload/*.c`).
The ignored plugin-only `hooks.c` / `main.c` are not payload sources. Required ignored
inputs are the local OpenOrbis toolchain, payload SDK/CRT and GoldHEN SDK; they are present
for this build. The `.prx` is built for local reference but is not part of the published
release.

## Known limitations

- Only the Manba One V2, Xbox 360 mode, 2.4 GHz receiver, PS4 11.00, GoldHEN
  v2.4b18.10+ (exact build not logged) is **confirmed working**.
- Manba One V2 Switch Pro mode on 11.00 is **partial**: pad created and fed, but the
  physical face-button labels are unconfirmed.
- DualSense/Edge basic input, Xbox One/Series GIP, 8BitDo, GameSir T4/"GameSir 2",
  Generic HID/Logitech F310, Machenike G1 and firmware 13.52 are **untested on console**.
- Adaptive triggers, haptics, lightbar/player LEDs, mic mute, battery state and touchpad
  coordinates cannot be reproduced through the input-only virtual pad (`OPEN-13`).
- Linux `hidraw` capture and Generic HID `--play` are limited as described in
  `docs/COMPATIBILITY.md`.
- The plugin remains untested/non-functional and is not shipped.

## Required community tests

1. **DualSense 9.60 capture and console run**: wired, full 64-byte report `0x01`, full
   stick/trigger/button sweep; then a payload run on the target firmware.
2. **DualSense Edge**: one Edge-only pass; basic input plus Fn/paddle/mute observations.
3. **Xbox One/Series GIP on 11.00 and 13.52**: current payload, wired USB, complete log.
4. **Switch Pro face buttons**: explicit per-button presses before the capture budget is
   used, so the physical label order can be settled.
5. **8BitDo, GameSir T4 Cyclone Pro Xbox mode, GameSir "2", Generic HID/F310, Machenike G1**:
   capture and console run with firmware and GoldHEN version recorded.
6. **Linux**: one real `/dev/hidraw` capture from a DualSense, DualShock or Switch Pro,
   reporting whether the file is non-empty and decodes.

Full evidence table: `COMPATIBILITY.md` in this release folder (repository:
`docs/COMPATIBILITY.md`).
