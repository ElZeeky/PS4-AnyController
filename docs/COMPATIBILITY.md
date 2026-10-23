# Compatibility

Evidence date: **2026-10-23**. Only rows marked **confirmed** were observed driving a PS4.
PC decoder tests, class signatures and wire-format compatibility are not promoted to console
confirmation.

| Result | Controller and mode | Connection | PS4 firmware | GoldHEN | Evidence / limitation |
|---|---|---|---|---|---|
| **Confirmed working** | Manba One V2, Xbox 360/XInput (`045E:028E`) | 2.4 GHz USB receiver | 11.00 | v2.4b18.10+ (exact build not logged) | 5e/5f: virtual pad created and fed; player 1 on home, menus and in games |
| **Partially working** | Manba One V2, Switch Pro (`057E:2009`, report `0x30`) | 2.4 GHz USB receiver | 11.00 | v2.4b18.10+ (exact build not logged) | 5h/5i: pad created, 400 reports, Y-axis fixed, no output errors; face-button labels still unconfirmed because the capture has no face presses |
| **Unsupported** | DualSense / DualSense Edge output features: adaptive triggers, haptics, LEDs, mic mute, battery state, touchpad coordinates | Wired USB | Any (not tested) | Not applicable | The virtual-pad API is input-only (`OPEN-13`); no path exists for game output to reach the physical pad |
| **Unsupported by design** | DualShock 4 or a DS4-mode clone | Wired USB | Any | Not applicable | Left to the console's native driver; the payload does not claim it |
| **Untested** | DualSense (`054C:0CE6`) basic input | Wired USB | 9.60 capture requested; console 11.00/13.52 not run | No run | Synthetic decoder tests only; no capture from the tester (`OPEN-25`) |
| **Untested** | DualSense Edge (`054C:0DF2`) basic input | Wired USB | Not run | No run | Same profile as DualSense; Edge Fn/paddles unmapped; no hardware (`OPEN-26`) |
| **Untested** | Xbox One/Series GIP (`FF/47/D0`), including Series X pads | Wired USB | 13.52 log invalid; 11.00 not run | No run | The 13.52 log contained zero USB devices, so it proves nothing (`OPEN-24`) |
| **Untested** | 8BitDo Xbox / Switch / D-Input modes | Wired USB | Not run | No run | ID and descriptor-gate tests only; no console hardware |
| **Untested (PC evidence only)** | GameSir Nova Lite 2.4 GHz receiver, Xbox 360 mode (`3537:1040`) | 2.4 GHz USB receiver | n/a (PC enumeration) | No run | `FF/5D/01` interface classified on PC; console not run |
| **Untested** | GameSir T4 Cyclone Pro, Xbox mode | Wired USB | Not run | No run | Expected to use standard Xbox 360 or GIP; no capture yet (`OPEN-27`) |
| **Untested** | GameSir "2" | Unknown | Not run | No run | Identity and capture are both missing (`OPEN-27`) |
| **Untested (PC evidence only)** | GameSir Nova Lite, Switch mode (`057E:2009`) | Wired USB | n/a (PC capture) | No run | 2000/2000 reports decoded by `--play`; console not run |
| **Untested** | Generic HID / Logitech F310 D-Input | Wired USB | Not run | No run | Descriptor-gated parser has tests only; no descriptor or console run (`OPEN-22`) |
| **Untested** | Machenike G1, Xbox 360 mode | Wired USB | 13.52 old log | No run | Old build/log; current payload has not been run (`OPEN-24`) |

## Tool limitations

These are capture-tool limits, not controller support claims.

* **Linux `hidraw` and Xbox XInput/GIP.** The kernel driver owns those pads, so `/dev/hidraw`
  usually exposes no input stream. Use `/data/AnyController.log` from the console with
  `--play` instead. Empty captures are deleted and the tool explains this case.
* **Generic HID playback.** `--play` cannot decode a Generic HID/D-Input capture because the
  capture does not carry the report descriptor needed to rebuild the field map. The payload
  does parse the descriptor on the console. The inspector prints a note instead of guessing.
* **DualSense output.** Input-only virtual pad; see `OPEN-13` in `PROGRESS.md`.

## Evidence rules

* A row becomes confirmed only after a console run on the stated firmware/GoldHEN with the
  controller in the stated mode and connection.
* PC capture playback and unit tests can prove a decoder, not a console configuration.
* Vendor IDs and packet sizes are not added from guesses; a capture or descriptor is required.
