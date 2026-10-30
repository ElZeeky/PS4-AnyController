# P5 release report

Date: **2026-10-30**  
Payload version string: **1.2.0-beta.2** (set `XCP_PAYLOAD_VERSION` in `ps4/payload/main.c`
to this value before building; the source still says `1.2.0-dev`)  
Scope: fixes driven by the tester logs (GameSir T4, GameSir Nova Lite, Logitech Dual Action,
DualSense, Xbox Series X, Machenike G1) and a full read-through of the payload, USB backend,
core decoder and tests.  
Status: **pre-release.** The decoder changes are verified on PC. The payload changes have
only been syntax-checked, and the patched payload has **not** been built with the project
toolchain or run on a console.

## Implemented changes

### Core decoder (PC-tested)

- **Switch Pro face buttons.** `XCP_SWPRO_*` used the PDP/HORI HID order (Y B A X). The real
  Pro Controller report 0x30, byte 3, is Y=0x01 X=0x02 B=0x04 A=0x08. X, B and A were
  crossed, which is what the GameSir T4 tester saw ("A does not work, B is the confirm
  button"). The decoder is positional: Nintendo A -> Circle, B -> Cross, X -> Triangle,
  Y -> Square. The existing tests compared against the constant names, so they agreed with
  the bug; the new test pins raw bytes.
- **Generic HID parser.** Global item tag 3 is Physical Minimum, not Logical Minimum, and was
  overwriting the logical range. Effects: a hat with logical range 1..8 read one step off,
  and a signed axis with a signed Physical Minimum collapsed to the centre.
- **Analog triggers declared as Simulation Controls** (Brake 0xC5 = L2, Accelerator 0xC4 =
  R2), as used by pads in Android mode, are now read.
- **Digital L2/R2** on a pad with no analog trigger now also set the analog trigger value.
- **Xbox 360 wireless receiver (`045E:0719`)** is now reported as unsupported. It used to
  be classified as GIP, opened, and then read nothing.
- **Xbox One/Series Guide button.** The Guide button has its own GIP message (0x07); it was
  dropped before decoding. `xcp_gip_guide_state()` reads it and the payload ORs `XCP_BTN_PS`
  into the state. The PS bit is still the unverified upstream value (`OPEN-2`).
- `xcp_pad_state_equal()` compares field by field instead of `memcmp` over padding.

### PS4 payload (syntax-checked only)

- **Virtual-pad creation is capped.** `AddDevice` can create a device even when the kernel
  log never names it, and an unnamed device is never removed. The payload now stops after 3
  attempts (a replug starts the count again), notifies the user, and probes the klog reader
  with its own marker before creating anything. Lines the payload printed about an event are
  no longer re-read as new events. The klog wait uses real elapsed time.
- **GIP start-up** sends to the descriptor's OUT endpoint instead of a fixed `0x02`, and a
  failed start-up is logged.
- **Manba post-Switch face workaround** now applies only to Microsoft vendor ids. The T4's
  own Xbox mode (`3537:1015`) was receiving it after a Switch session.
- **Diagnostics.** Read errors are logged per open (a process-wide counter hid everything
  after the first eight). A source that opens but never delivers data is retried with a
  growing pause and a notification after 3 attempts. A pad whose reports arrive but never
  decode logs the shape of one report. While no usable controller is on the bus, the USB
  device list is printed every 10 s (the start-up dump often runs before the bus settles
  and says "0 devices").
- **Recovery ladder for DualSense and Generic HID** (experimental). A pad that opens but
  never delivers a report is reopened with a different variant each time: standard; also
  claim the interfaces the console normally keeps; SET_IDLE plus clear-halt after the claim;
  claim without detaching. The variant that delivers a report is remembered. Every step is
  logged. The DualSense also logs the result of a read-only `GET_REPORT` (feature 0x05) as a
  diagnostic.

### Documentation

- `README.md`: headline no longer says "any wired controller"; firmware badge and text now
  list 9.00, 11.00 and 13.52 as seen working in logs; known-not-working list added.
- `docs/COMPATIBILITY.md`: new rows from the tester logs (see below); the "8BitDo Ultimate
  2C" log is recorded as a duplicate of the Nova Lite log.

## Verification

- Unit tests: **105 tests / 639 checks / 0 failed** (was 98 / 592). Built with gcc and
  `-Wall -Wextra -Wshadow -Wconversion`, no warnings. This run was done outside the
  project's Zig toolchain; re-run `./build.sh test` before publishing.
- Seven new tests were written first. The six that exercise the bugs failed against the
  previous code with the predicted values (Switch Pro `0x04` decoded as Circle instead of
  Cross, `0x08` as Triangle instead of Circle; hat logical minimum 0 instead of 1; signed
  axis minimum 129 instead of -127; Brake/Accelerator ignored; digital L2 left the analog
  value at 0; wireless receiver typed as Xbox One) and pass now. The seventh covers the
  Guide message.
- **Real-capture regression** (old decoder against new decoder, same input):

  | Capture | Reports | Changed |
  |---|---:|---:|
  | Xbox 360 reports from the console logs (notification-loop log, Machenike, Nova Lite 13.52, T4 Xbox mode) | 792 | 0 |
  | GameSir T4 receiver, Switch Pro, PS4 9.00 console log | 1572 | 93 |
  | GameSir Nova Lite, Switch Pro, PC `hidraw` capture | 2000 | 367 |

  In both Switch Pro captures only reports with raw bits 0x02, 0x04 or 0x08 change
  (0x02: Cross -> Triangle, 0x04: Circle -> Cross, 0x08: Triangle -> Circle); bit 0x01 does
  not move. The Machenike PC capture decodes 0 of 1958 reports with both versions; that is
  the documented XInput capture limitation, not a regression.
- PS4 sources: `gcc -fsyntax-only` against the OpenOrbis headers, no new warnings. They have
  **not** been compiled by `ps4/build_payload.sh` and have **not** been run.
- The patch applies cleanly (`patch -p1 --binary`) to a clean copy of the repository and the
  resulting files are identical to the tested ones.
- Not done: payload build with the project toolchain, Inspector rebuild, any console run.

## Console evidence reviewed

These logs come from builds made **before** the changes above. They show what the previous
payload did.

| Log | Firmware | Finding |
|---|---|---|
| GameSir T4 receiver, Switch Pro mode (`057E:2009`) | 9.00 | Direct `AddDevice` created and fed the pad in 5 sessions. A/B/X crossed (explained above). |
| GameSir T4 receiver, own Xbox mode (`3537:1015`) | 9.00 | Opened and fed; the Manba workaround was applied after a Switch session. |
| GameSir Nova Lite receiver, Xbox 360 (`3537:1040`) | 13.52 | Direct path works: DeviceId `0x7030D`, first `InsertData` = 0. Sticks, D-pad, LB/RB seen. Face buttons and triggers not pressed. |
| DualSense (`054C:0CE6`) | 9.00 and 10.01 | Opens and claims interface 3, then every read aborts at once (`0x802400FF`) and the source is reopened in a loop. Same with a DS4 attached and without. |
| Logitech Dual Action (`046D:C216`) | 9.00 | Descriptor parsed (12 buttons), no report ever arrives, the device re-enumerates about every 15 s. |
| Xbox Series X | 13.52 | Zero USB devices visible; no conclusion possible. |
| GameSir T4 Xbox mode, direct | 9.00 | Zero USB devices visible; no conclusion possible. |
| Machenike G1 (old build) | 13.52 | Opened as Xbox 360; the 11.00 callsite patch was refused. Predates the direct path. |
| "8BitDo Ultimate 2C" | 13.52 | Byte-identical to the Nova Lite log; **not** 8BitDo evidence. |

## Artifacts

Fill this table from the real build; none of these were produced for this report.

| Artifact | Size (bytes) | SHA-256 |
|---|---:|---|
| `release/AnyController.elf` | to fill | to fill |
| `release/AnyController-dumper.elf` | to fill | to fill |
| `release/AnyController-Inspector.exe` (Windows) | to fill | to fill |
| `release/AnyController-Inspector-linux-x86_64` | to fill | to fill |

Compute them with `sha256sum release/*` and `stat -c %s release/*` after `./build.sh package`.
Required local inputs are the same as before: the OpenOrbis toolchain, the payload SDK/CRT
and the GoldHEN SDK. The plugin is not part of the release.

## Known limitations

- **Confirmed on a console by the author:** Manba One V2, Xbox 360 mode, 2.4 GHz receiver,
  PS4 11.00.
- **Working in tester logs:** GameSir Nova Lite, Xbox 360 mode, PS4 13.52 (no face buttons
  pressed). **Partially working:** GameSir T4 receiver, Switch Pro mode, PS4 9.00 (face
  buttons fixed in code, not re-tested).
- **DualSense and Logitech Dual Action fail on every firmware tried.** The root cause is
  not identified. The recovery ladder is a set of informed attempts, not a known fix.
  Strategy 2 calls `sceUsbdClearHalt` assuming the signature of `libusb_clear_halt`, in the
  same way `detach` already assumes its signature; it only runs after earlier opens failed.
- Xbox Series X and the T4 in Xbox mode were not seen by the console at all in their logs.
- The GIP start-up (power-on, LED, security) follows `xpad` and has never run against real
  hardware. The Guide-to-PS mapping depends on the unverified PS bit.
- The payload keeps the elevated credentials (`authid 0x3800000000010003`) for its whole
  lifetime; they are only restored on failure paths.
- A virtual pad created without klog confirming it stays until reboot. The attempt cap
  limits how many can pile up but does not remove one that already exists.
- Firmware 13.52 has the direct path only; the callsite-patch fallback is firmware 11.00
  only.
- Not supported: PS3 pads, the Xbox 360 wireless receiver, steering wheels, keyboards and
  mice, adaptive triggers, haptics, LEDs, gyro and battery state (`OPEN-13`).
- `router/`, `ps4/src/hooks.c` and `host/` were not reviewed in depth.

## Required community tests

1. **DualSense, PS4 9.00 and 10.01, this build.** Send `/data/AnyController.log`. Look for
   `open strategy`, `DualSense probe: GET_REPORT`, `no report with strategy` and
   `strategy N ... delivered a report`.
2. **Logitech Dual Action / any generic HID pad that goes silent.** Same log, same lines.
3. **GameSir T4 receiver in Switch Pro mode, PS4 9.00.** Press A, B, X and Y one at a time.
   Expected: A -> Circle, B -> Cross, X -> Triangle, Y -> Square.
4. **The same T4 switched to its Xbox mode.** Expected: A -> Cross, B -> Circle (no rotation).
5. **Xbox One/Series on a console.** Log whether the GIP start-up succeeds and whether the
   Guide button reaches the console as PS.
6. **8BitDo Ultimate 2C.** A real log; the one submitted was the Nova Lite log.
7. **Xbox Series X and T4 Xbox mode, direct.** A log from this build, which prints the USB
   bus every 10 s while nothing usable is found.
8. **GameSir Nova Lite, PS4 13.52.** Face buttons and both triggers.

Full evidence table: `COMPATIBILITY.md` in this release folder (repository:
`docs/COMPATIBILITY.md`).
