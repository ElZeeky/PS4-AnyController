# Deploying the virtual-pad *payload* (system-wide, menus included)

**Nothing in this project ever touches your console.** No network access, no FTP, no
payload. Everything below is yours to run.

This guide deploys the payload, which creates a **virtual DualShock 4** that the whole
system sees, so the pad works on the home screen, in the settings menus, at sign-in and in
games.

---

## 0. Why a payload and not a plugin

GoldHEN loads `.prx` plugins into the **game's** process. The home screen and the
sign-in screen belong to system processes, which never load plugins, so a plugin
cannot reach them - no configuration changes that. A GoldHEN **payload** runs inside an
existing system process, where Sony's own virtual-device API (`scePadVirtualDevice*`,
the API Remote Play uses) is available. A device created with it is a real MBus device:
the system treats it exactly like a DualShock 4 plugged in over USB.

The DS4 path is unchanged and still yours: it boots the console, runs the browser
exploit and starts GoldHEN. After that the virtual pad can take over every screen.

---

## 1. Requirements

* **GoldHEN v2.4b18.10 or later.** Older versions do not run ELF payloads reliably and
  have no auto-start. Check it in GoldHEN's own menu (`Settings`/`Utilities` shows the
  version string); if it is older, update GoldHEN first.
* The virtual-device API is known to work on firmware 10.01 and 11.02, and **11.00 is now
  confirmed working too** (this project, with the libScePad patch described below).
* The Xbox pad's receiver plugged into the console's USB port. The pad's own dongle is
  the input source; the console does not have to understand it as a pad, because we read
  it directly over `sceUsbd`.
* Zig, which this project already uses, and git. **No Linux, no `make`, no Docker and
  no separately installed LLVM** - see below.

---

## 2. Build it

```bash
./build.sh payload            # full ELF
./build.sh payload --check    # compile the sources only; no CRT, no SDK clone
```

Both run anywhere this project already builds. A payload is not a `.prx`, so it needs a
CRT (the `_start` that calls `main()` and the runtime that resolves the Sony imports in
the hijacked process), and the only public one is in `ps4-payload-sdk`. **Its build system
is not needed**, though: the script clones that repository for its seven CRT *source*
files and compiles them here with the Zig this project already uses, then links with
Zig's `ld.lld` and the SDK's own linker script. First run clones it into
`.tools/ps4-payload-sdk`; point `PS4_PAYLOAD_SDK` elsewhere if you prefer.

The Sony libraries come from the OpenOrbis stubs already in the tree - the SDK's own are
empty placeholders. **No libc is linked in**: `printf`, `memset`, `snprintf` and friends
stay as imports and are resolved at load time.

The build ends by checking its own output - it fails if the ELF is not a PIE or has lost
`_start`/`main` - and leaves **`build/ps4/AnyController.elf`** behind.

Licensing, worth knowing before shipping: that binary contains the SDK's GPL-3.0 CRT, so
the **binary** is GPL-3.0 while this repository's sources stay MIT. See [NOTICE](../NOTICE).

If you would rather use the SDK's official build (`make`, an installed LLVM), it produces
the same kind of artifact; nothing in this repository depends on it.

---

## 3. Put it on the console

GoldHEN's FTP server normally listens on port **2121**, user/password `ps4`/`ps4`.

```bash
curl -u ps4:ps4 -T build/ps4/AnyController.elf \
  ftp://192.168.1.123:2121/data/payloads/AnyController.elf
```

Then, on the console:

* **GoldHEN → Utilities → Payloader LaunchPad**, pick `AnyController.elf` to start it
  once, or press **Square** on it to add it to the AutoRun queue; or
* from a computer, send it to GoldHEN's **PayLoader on port 9090** with any payload
  sender; or
* add it to AutoRun by hand - `/data/GoldHEN/payloads.ini`:

  ```ini
  [AutoRun]
  /user/data/payloads/AnyController.elf = 1
  ```

AutoRun is what makes it start by itself after every reboot, which is what you want once
it works. Keep the other entries in that file untouched.

For the first run, **start it by hand from the LaunchPad** so you can see it fail.

---

## 4. Before the first run

* Boot the console with the DS4, run the exploit, let GoldHEN load.
* Enable klog in GoldHEN's menu (on-screen and/or over the network, depending on the
  GoldHEN build) **before** starting the payload: the payload's only output is the
  kernel log.
* Plug the Xbox pad's receiver into a USB port. It does not matter when; the payload
  rescans once a second while it has no input source.

---

## 5. Reading the log

**Easiest: the payload writes its own file, `/data/AnyController.log`.** Open GoldHEN's
FTP (FileZilla, port 2121) and download `/data/AnyController.log`. That works even with
klog off, and it is the file to send back.

> If `/data/AnyController.log` is **not there** after a run, the payload's code never
> executed: the ELF did not load or its imports did not resolve in the hijacked process.
> That is the finding - say so, because no klog line can appear either.

The same lines also go to the kernel log. GoldHEN's klog server is on **port 3232**
(`ncat <ps4-ip> 3232`, or PowerShell / PuTTY in raw mode). Connect *after* starting the
payload: klog has a single reader, and a client holding it can stop the payload reading
its own handle event. If `printf` output never shows there, GoldHEN's **TTY redirect**
option is off - the file above is unaffected.

These are the lines that matter, in the order they should appear (this is the real FW 11.00
shape, taken from a working run):

```
xcp: =============================================
xcp: virtual-pad payload starting
xcp: sizeof(OrbisPadData) = 120
xcp: sceUsbd init ok
xcp: usb: 1 device(s) visible
xcp:   usb[0] 045E:028E 045E:028E (Xbox 360)      <- the receiver
xcp: opened 045E:028E as Xbox 360
xcp: input source ready: 045E:028E (Xbox 360)
xcp: sceKernelLoadStartModule(/system/common/lib/libSceMbus.sprx) = 0x...
xcp: scePadInit() = 0x00000000
xcp: scePadSetProcessPrivilege(1) = 0x00000000
xcp: vda patch: applied (cave stub + retargeted call)
xcp: marker add
xcp: AddDevice(size=32 user=1 type=3) = 0x00000000     <- 0 because of the patch
xcp: klog pad-added: <118>#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED ...
xcp:   parsed DeviceId = 0x5030D
xcp: device adopted: DeviceId=0x5030D handle=0x0005030D
xcp: bind skipped: press a button on the controller to accept the assignment prompt
xcp: first InsertData(handle=0x0005030D) = 0x00000000
```

**Two things to know about this shape:**

* `vda patch: applied` is mandatory. The patch retargets one internal call inside
  libScePad's `scePadVirtualDeviceAddDevice` so it returns `0`; without it `AddDevice`
  answers `0x803B0006` and no device is really created. The patch is guarded - if the call
  site is not the FW 11.00 one it prints `vda patch: ... not patching` and stops rather
  than corrupt a system library.
* `AddDevice = 0x00000000` (with the patch) means success. If you see `0x803B0006`
  instead, the patch did not apply - send the `vda patch:` line above it.
* `bind skipped` means the console's **"Who's using this controller?"** prompt is answered
  by **pressing a button on the controller**, not automatically.

| Symptom | Meaning |
|---|---|
| nothing at all | the payload did not start; check the path and GoldHEN's version |
| `libSceMbus did not load` | the payload stopped before touching libScePad, on purpose - calling into it first can kill the process it runs in |
| `open(/dev/klog) failed` | another reader has the log (GoldHEN's log server does while a client is connected). Close the log client and start the payload again |
| `scePadInit failed` | the pad service is not available to this process |
| `no device event within 700 ms` | the console did not announce a new device. The payload stops rather than leave a device it cannot address |
| `usb: nothing at all` | `sceUsbd` sees no devices at all, so it is not the receiver that is the problem |
| the receiver is not listed | note its real `VID:PID` and send it: the decoder has to be told about a new id |
| `found ... but could not open it` | the receiver was seen but `sceUsbdOpen` refused, or no interface could be claimed (look at the `iface`/`claim` lines just above it) |
| `no claimable interrupt IN endpoint` | the descriptors showed no IN interrupt endpoint on any interface, so it fell back to interface 0 - send those `iface`/`ep` lines, they are the answer |
| `reading interrupt IN on EP 0x.. of interface ..` | what it settled on. If reports then never arrive, this line is the one to change |
| `input source dropped (disconnect)` | the receiver was unplugged or a transfer failed; it is reopened on the next scan |

The `usb:` block is the point of the first run: it says what the console sees.
The `xcpcapture` lines are the pad's own bytes; paste them into a file and run them
through the same decoder the console ran:

```bash
./build/xcp_inspector --play capture.txt
```

A clean capture exits `0`; a rejected line makes it exit `2`.

---

## 6. What to expect on screen

A few seconds after `device adopted`, the console behaves as if a DS4 had just been
plugged in: **"Who's using this controller?"** appears and you pick the user **on the
PS4**. Then the virtual pad is that user's controller - menus, sign-in, notifications,
games, PS button included.

The console decides which player number it is. With the DS4 still connected you may end
up with two controllers on the same user; turn the DS4 off (`PS` → `Sound/Devices` →
`Turn Off Device`) and the virtual pad is what is left.

Motion, the light bar and the touch pad **surface** of the virtual pad are neutral/absent:
only buttons, sticks and triggers are fed. The **touchpad click** *is* available, emitted
from a **chord: L3 and R3 held together at the same time**. Program a back button (e.g.
M3) to an `L3+R3` macro on the controller so one press fires it; the payload latches that
click for a moment so a game sees it even though the macro only pulses it. The DS4 can
still be used as before.

---

## 7. If it misbehaves

1. **Stop the payload.** The payload itself has **no stop button** - it is a loop with no
   exit, and its libScePad patch lives only in that process's RAM. **Rebooting the console
   is the reliable way to stop it**, and it clears the patch and the virtual device. If
   your GoldHEN build lets the loader kill a running payload, that works too. **Turning the
   controller off** makes the payload delete the virtual pad (so the console goes back to
   normal), but the payload code keeps running. See section 9.
2. **Take the receiver out** - same as turning the pad off: the virtual pad is removed.
3. **Reboot** clears everything. Nothing is written anywhere outside
   `/data/payloads/` and `/data/GoldHEN/payloads.ini`.

Things that are true by construction:

* The payload **never presses anything by itself**. Every button it reports comes from
  the receiver's reports, decoded by the same code the PC tests exercise.
* If `sceUsbd` or the virtual-device API is unavailable, it says so and stops; it never
  touches your existing controllers.
* It only holds `/dev/klog` for the ~1 second it needs to learn the device handle, so
  your klog server works normally the rest of the time.
* The virtual pad is created for **user 1 unassigned**, so the choice of user stays on
  the console and sign-in keeps working without a physical pad.

---

## 8. What to send back

1. The file `/data/AnyController.log` (or every `xcp:` line from klog, from
   `virtual-pad payload starting` on), or the fact that the file does not exist.
2. The `usb:` block in particular - that is what decides the next step.
3. The `xcpcapture` lines.
4. Which GoldHEN version, which firmware, and in which menus the virtual pad worked.

---

## 9. Stopping the payload, and the touchpad chord

**Stopping it.** The payload has **no built-in stop**: it is a `for (;;)` loop with no exit
condition, and the libScePad patch it applies exists only in that process's RAM. In order
of preference:

1. **Reboot the console.** Definitive. Clears the process, the RAM patch and the virtual
   device.
2. **Turn the controller off** (or unplug the receiver). The payload sees the input source
   go away, removes the virtual pad (`DeleteDevice`) and the console behaves normally again
   - but the payload code keeps running, ready if the pad comes back.
3. Killing the hosting process is **not recommended**: it is a system process, and dying
   without `DeleteDevice` can leave the virtual pad orphaned until a reboot.

If a clean in-console stop is wanted later, the small options are a sentinel file
(`/data/xcp_stop`), a button combo (e.g. hold `Guide + Options`), a network command, or an
idle timeout - all of which would `DeleteDevice` and then leave the loop. None is built in
today.

**Touchpad chord.** The virtual DS4's touchpad **click** is produced when **L3 and R3 are
held together**. The Xbox report has no touchpad of its own, and the controller's M1..M4
buttons are firmware macros (they only ever emit existing buttons), so a chord is what
drives it. A short L3+R3 macro pulse is enough - the payload latches the click for ~250 ms
so a game registers it, and hides the loose L3/R3 while latched. A game that itself uses
L3+R3 will see the touchpad instead, which is the one trade-off to know.

