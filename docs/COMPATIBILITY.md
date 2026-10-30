# Controller Compatibility

<<<<<<< Updated upstream
**Last evidence update:** 2026-10-23
=======
Evidence date: **2026-10-27**. Only rows marked **confirmed** were observed driving a PS4.
PC decoder tests, class signatures and wire-format compatibility are not promoted to console
confirmation. Rows marked **tester log** come from console logs sent by other people
(`DUMPLOGS/tests from other people/`): they show what the payload did on that console, not that
the controller is fully correct.
>>>>>>> Stashed changes

This document tracks controller compatibility with AnyController, separating results verified on a real PS4 from PC-only tests, implementation limitations, and untested configurations.

**Important:** Only results explicitly marked as confirmed were observed working on a PS4. Passing unit tests, decoding USB reports on a PC, or recognizing a controller's protocol does not establish PS4 compatibility.

## Compatibility Summary

| Status | Meaning |
|---|---|
| **Confirmed working** | Tested on a PS4 with the firmware, GoldHEN version, connection, and controller mode documented below. The tested inputs work as expected. |
| **Partially working** | Tested on a PS4 and input works, but a known issue remains, such as incorrect button mapping. |
| **Untested** | No valid PS4 test has confirmed the configuration. PC evidence, if available, is listed separately. |
| **Unsupported** | The specified feature is not supported by the current implementation. This does not necessarily mean the entire controller is unusable. |
| **Unsupported by design** | The configuration is intentionally left to the PS4's native controller driver. |

## PS4 Test Results

These are the controller configurations that have been tested directly on a PS4.

| Status | Controller and mode | Connection | PS4 firmware | GoldHEN | Evidence and limitations |
|---|---|---|---|---|---|
<<<<<<< Updated upstream
| **Confirmed working** | Manba One V2, Xbox 360/XInput (`045E:028E`) | 2.4 GHz USB receiver | 11.00 | v2.4b18.10+ (exact build not logged) | Evidence 5e/5f: virtual pad created and received input. Player 1 worked on the home screen, in menus, and in games. |
| **Partially working** | Manba One V2, Switch Pro (`057E:2009`, report `0x30`) | 2.4 GHz USB receiver | 11.00 | v2.4b18.10+ (exact build not logged) | Evidence 5h/5i and subsequent PS4 testing: virtual pad created and controller input works. The face-button mapping is incorrect: controller Y → PS Cross (×), A → Circle (○), B → Triangle (△), and X → Square (□). |
=======
| **Confirmed working** | Manba One V2, Xbox 360/XInput (`045E:028E`) | 2.4 GHz USB receiver | 11.00 | v2.4b18.10+ (exact build not logged) | 5e/5f: virtual pad created and fed; player 1 on home, menus and in games |
| **Partially working** | Manba One V2, Switch Pro (`057E:2009`, report `0x30`) | 2.4 GHz USB receiver | 11.00 | v2.4b18.10+ (exact build not logged) | 5h/5i: pad created, 400 reports, Y-axis fixed, no output errors; face-button labels still unconfirmed because the capture has no face presses |
| **Unsupported** | DualSense / DualSense Edge output features: adaptive triggers, haptics, LEDs, mic mute, battery state, touchpad coordinates | Wired USB | Any (not tested) | Not applicable | The virtual-pad API is input-only (`OPEN-13`); no path exists for game output to reach the physical pad |
| **Unsupported by design** | DualShock 4 or a DS4-mode clone | Wired USB | Any | Not applicable | Left to the console's native driver; the payload does not claim it |
| **Failing (tester logs)** | DualSense (`054C:0CE6`) basic input | Wired USB | 9.00 and 10.01 | Not logged | Opens and claims interface 3, then every interrupt read aborts at once (`0x802400FF`) and the source is reopened in a loop. Same on both firmwares, with or without a DS4 attached. Cause unknown (`OPEN-25`); the build now tries a recovery ladder and logs each step |
| **Failing (tester log)** | Logitech Dual Action (`046D:C216`), generic HID | Wired USB | 9.00 | v2.4b18.10 | Descriptor parsed (12 buttons) and the interface opened, but no report ever arrived; about every 15 s the transfer aborts (`0x802400FF`) and the device re-enumerates. Cause unknown; the same recovery ladder applies |
| **Partially working (tester log)** | GameSir T4 Cyclone Pro, receiver in Switch Pro mode (`057E:2009`) | 2.4 GHz USB receiver | 9.00 | v2.4b18.10 | The direct AddDevice path created and fed the pad on 9.00. Face buttons were crossed (A became Triangle, B became Circle) because the Switch Pro bit constants were wrong; fixed in code, not yet re-tested on a console |
| **Working with a bug (tester log)** | GameSir T4 Cyclone Pro, receiver in its own Xbox mode (`3537:1015`, `FF/5D/01`) | 2.4 GHz USB receiver | 9.00 | v2.4b18.10 | Opened and fed as Xbox 360, but the Manba post-Switch face workaround was applied after a Switch session. It is now limited to Microsoft vendor ids; not yet re-tested |
| **Unsupported** | Xbox 360 wireless receiver (`045E:0719`) | Wireless | Any | Not applicable | Different framed protocol; it used to be opened as GIP and then read nothing, and is now reported as unsupported |
| **Untested** | DualSense Edge (`054C:0DF2`) basic input | Wired USB | Not run | No run | Same profile as DualSense; Edge Fn/paddles unmapped; no hardware (`OPEN-26`) |
| **Untested** | Xbox One/Series GIP (`FF/47/D0`), including Series X pads | Wired USB | 13.52 log invalid; 11.00 not run | No run | The 13.52 log contained zero USB devices, so it proves nothing (`OPEN-24`) |
| **Untested** | 8BitDo Xbox / Switch / D-Input modes | Wired USB | Not run | No run | ID and descriptor-gate tests only; no console hardware |
| **Untested (PC evidence only)** | GameSir Nova Lite 2.4 GHz receiver, Xbox 360 mode (`3537:1040`) | 2.4 GHz USB receiver | n/a (PC enumeration) | No run | `FF/5D/01` interface classified on PC; console not run |
| **Untested** | GameSir T4 Cyclone Pro, Xbox mode | Wired USB | Not run | No run | Expected to use standard Xbox 360 or GIP; no capture yet (`OPEN-27`) |
| **Untested** | GameSir "2" | Unknown | Not run | No run | Identity and capture are both missing (`OPEN-27`) |
| **Untested (PC evidence only)** | GameSir Nova Lite, Switch mode (`057E:2009`) | Wired USB | n/a (PC capture) | No run | 2000/2000 reports decoded by `--play`; console not run |
| **Untested** | Generic HID / Logitech F310 D-Input | Wired USB | Not run | No run | Descriptor-gated parser has tests only; no descriptor or console run (`OPEN-22`) |
| **Untested** | Machenike G1, Xbox 360 mode | Wired USB | 13.52 old log | No run | Old build/log; current payload has not been run (`OPEN-24`) |
>>>>>>> Stashed changes

**Confirmed firmware coverage is currently limited to PS4 firmware 11.00.** These results do not establish compatibility with other firmware versions.

## Untested Controllers and Configurations

The following configurations have not been confirmed working on a PS4. PC-side results are included only as diagnostic evidence, not as proof of console compatibility.

| Controller and mode | Connection | Available evidence | PS4 test status |
|---|---|---|---|
| DualSense (`054C:0CE6`), basic input | Wired USB | Synthetic decoder tests only. A firmware 9.60 capture was requested, but no capture was received from the tester (`OPEN-25`). | Untested on PS4 firmware 11.00 and 13.52. |
| DualSense Edge (`054C:0DF2`), basic input | Wired USB | Shares the basic DualSense profile. Fn buttons and rear paddles are unmapped. No hardware available for testing (`OPEN-26`). | Untested. |
| Xbox One/Series GIP (`FF/47/D0`), including Xbox Series X controllers | Wired USB | The firmware 13.52 log contained zero USB devices and is invalid as compatibility evidence. Firmware 11.00 has not been tested (`OPEN-24`). | Untested. |
| 8BitDo, Xbox / Switch / D-Input modes | Wired USB | Device ID and descriptor-gate tests only. No console hardware test. | Untested. |
| GameSir Nova Lite, Xbox 360 mode (`3537:1040`) | 2.4 GHz USB receiver | PC enumeration classified the `FF/5D/01` interface. No PS4 test. | Untested; PC evidence only. |
| GameSir T4 Cyclone Pro, Xbox mode | Wired USB | No capture available. The expected protocol has not yet been verified (`OPEN-27`). | Untested. |
| GameSir "2" | Unknown | Exact controller identity and USB capture are missing (`OPEN-27`). | Untested. |
| GameSir Nova Lite, Switch mode (`057E:2009`) | Wired USB | PC capture: 2,000 of 2,000 reports decoded with `--play`. No PS4 test. | Untested; PC evidence only. |
| Generic HID / Logitech F310, D-Input mode | Wired USB | Descriptor-gated parser tests only. No captured descriptor or console test (`OPEN-22`). | Untested. |
| Machenike G1, Xbox 360 mode | Wired USB | An older log exists from firmware 13.52, but it used an earlier build. The current payload has not been tested (`OPEN-24`). | Untested with the current build. |

### Firmware Testing Notes

- **11.00:** Tested with GoldHEN v2.4b18.10+ using the Manba One V2 in Xbox 360/XInput and Switch Pro modes. Xbox 360/XInput mode is confirmed working; Switch Pro mode works with incorrect face-button mapping.
- **9.60:** A DualSense capture has been requested. No valid tester capture has been received.
- **13.52:** Some testing has been attempted, but the Xbox GIP log contained zero USB devices and cannot establish compatibility. The older Machenike G1 log does not verify the current build.
- **Other firmware versions:** Untested. Compatibility should not be assumed until verified on a real console.

## Unsupported Features and Configurations

These limitations describe the current implementation, not necessarily the compatibility of the entire controller.

### DualSense and DualSense Edge Output Features

**Status: Unsupported by the current implementation.**

The virtual-pad API is input-only (`OPEN-13`). There is currently no output path that forwards game-generated output commands to the physical controller.

The following features are therefore not supported by AnyController:

- Adaptive trigger control.
- Advanced haptics and vibration output.
- Lightbar and player indicator LED control.
- Microphone mute control.
- Battery status reporting.
- Touchpad coordinates and gesture input.

Basic input support for the DualSense or DualSense Edge does not imply support for these features.

### DualShock 4 and DS4 Mode

**Status: Unsupported by design as an AnyController target.**

DualShock 4 controllers and controllers operating in DS4 mode are left to the PS4's native controller driver. AnyController does not claim to provide compatibility for this configuration.

## Diagnostic Tool Limitations

These are limitations of the capture and inspection tools. They should not be interpreted as controller compatibility results.

### Linux `hidraw` and Xbox XInput/GIP

On Linux, the kernel driver owns many Xbox XInput/GIP controllers. As a result, `/dev/hidraw` may not expose their input stream.

For these cases, use `/data/AnyController.log` captured on the PS4 and replay it with `--play`. Empty captures are deleted, and the tool explains this case.

### Generic HID Playback

The `--play` inspector cannot decode a Generic HID/D-Input capture when the capture does not include the USB report descriptor needed to reconstruct the field map.

The payload parses the descriptor on the console. The inspector prints a note when it cannot reconstruct the mapping instead of guessing.

### DualSense Output

DualSense output features remain unsupported because the virtual-pad API is input-only. See `OPEN-13` in `PROGRESS.md`.

## Evidence Rules

Compatibility claims must be based on reproducible evidence.

1. **PS4 confirmation requires a real console test.** Record the PS4 firmware, GoldHEN version, controller model, connection method, operating mode, and observed behavior.
2. **A decoder test is not a compatibility test.** Unit tests, PC captures, and successful report playback can validate parsing, but they cannot prove that the payload works with that configuration on a PS4.
3. **Invalid logs do not count as evidence.** For example, a capture containing zero USB devices cannot establish that a controller is incompatible or compatible.
4. **Partial results must remain partial.** If the virtual pad works but certain buttons are mapped incorrectly, document the working input and the exact mapping issue.
5. **Do not infer hardware details.** Vendor IDs, product IDs, report formats, and packet sizes must come from a capture, descriptor, or other verifiable evidence.
6. **Keep evidence tied to the tested build.** Results from older payloads or logs do not automatically verify the current release.
7. **Update this document when new evidence arrives.** Promote a configuration to confirmed only after the relevant PS4 test succeeds and the tested inputs behave as expected. Keep known mapping issues documented.

## Reporting a Compatibility Test

When submitting a test result, provide:

- Controller model and USB vendor/product ID, if available.
- Connection method and controller operating mode.
- PS4 firmware version.
- GoldHEN version and payload build.
- Whether the controller was detected.
- Whether the virtual pad was created.
- Which buttons, axes, and D-pad directions worked.
- Any incorrect mappings, missing inputs, errors, or diagnostic logs.

A successful test of one controller mode does not automatically confirm the same controller in another mode or connection method.

Community testing is essential to expanding verified compatibility across controller models and PS4 firmware versions.
