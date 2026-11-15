# Audio 2 DJ for modern macOS — experimental

A community userspace driver for the **original Native Instruments Audio 2 DJ**
(USB `17cc:041c`), with four output channels at 48 kHz. This is not the newer
Traktor Audio 2. No NI kernel extension, SIP change or legacy driver is needed.

**Status: v0.1.1 experimental, Intel only.** Tested on one Intel MacBook Pro 13-inch
2020 with macOS 26.7.1 and djay Pro 5.6.9. Apple Silicon and other macOS versions
are untested. This is not an official Native Instruments product.

## Install

1. Download the Intel `.pkg` from [Releases](https://github.com/alberthild/audio2dj/releases).
2. Close DJ/audio applications and turn the interface volume down. Installation
   restarts CoreAudio and interrupts all audio on the Mac.
3. Open the package and follow macOS Installer. Administrator authentication is
   required to install the HAL plugin. The bridge itself runs as your user.
4. Reopen your DJ app and select **Audio 2 DJ Experimental**, 48 kHz.
   Assign master to channels **1–2 (A)** and cue to **3–4 (B)**.
5. Raise the hardware volume carefully and test both outputs before relying on it.

The package is **unsigned and not notarized**; its HAL bundle has a local ad-hoc
signature, not a Developer ID signature. macOS may block downloaded software.
Review/build the source yourself; do not disable SIP or Gatekeeper globally.
The Installer lifecycle scripts have been inspected and the package expanded for
verification, but the package has not been installed end-to-end on a clean Mac.
The equivalent components were installed manually on the test Mac.

A user LaunchAgent starts the bridge at login and waits for playback. Only one
logged-in user may control the physical interface at a time. Do not run a second
bridge manually. A legacy per-user agent with the same label is disabled during
package installation; its plist is preserved with a `.disabled` suffix.

## Recovery fix in 0.1.1

A captured silent bridge was stuck in libusb 1.0.29 shutdown: the main thread held
`darwin_cached_devices_mutex` while waiting for a hotplug thread that needed the
same mutex. 0.1.1 backports the [upstream fix](https://github.com/libusb/libusb/commit/94a5224ea1a7515c38c618694e8794e058c16412).
The build always extracts clean sources and applies the patch before compiling.
A separate five-second cleanup deadline terminates the process if cancellation,
release or shutdown still stalls, allowing launchd to restart it. No gain, routing,
sample conversion, USB queue length or clock-feedback behavior was changed.

This fixes the observed failure to recover, **not all causes of USB disconnection**.
A recovery still interrupts audio. Physical cable/power issues and other errors
remain possible. The deadline begins at cleanup, not during a blocked USB startup
call or arbitrary streaming stall; it is not a general-purpose watchdog.

Regression checks (after building):

```sh
python3 tests/test_recovery.py
./scripts/package.sh
python3 tests/verify_package.py
```

The regression coordinates the exact lock cycle using the actual old/patched
shutdown function: the old code must time out and the patched code must complete.
It also checks multiple contexts, the bridge cleanup deadline, and repeated real
libusb init/enumerate/exit without opening or resetting the audio device.

## What works and what does not yet

- The tester confirmed clean music at normal digital gain on A and B, with
  independent master and cue routing.
- A larger independent USB queue removed the frequent initial dropouts.
- Following the device's sample count removed persistent signal-dependent crackle.
- Three minutes of subsequent observation showed no USB errors and no additional
  underruns after startup. **Later logs contained two USB errors followed by
  automatic restarts** and an initial buffer overflow on one restart. These were
  not reproduced or fully diagnosed. This is not a claim of gig-ready stability.
- The USB queue alone is nominally **128 ms**, plus software buffering. This is
  high latency for DJ monitoring. End-to-end latency is unmeasured; the HAL still
  advertises an older 1536-frame latency plus 256-frame safety offset, which does
  not account for this queue. Accurate latency reporting and lower-latency
  scheduling remain open work.
- Unplug/replug, restart, sleep/wake, multi-user switching and long sets need more
  testing. USB reset can require a retry. The bridge may count an initial underrun
  while filling the output queue.
- Fixed 48 kHz, four outputs, no inputs, no software volume controls. Use physical
  volume controls. No persistent test gain reduction or A/B mirroring remains.

## Build and package

On an Intel Mac with Xcode Command Line Tools:

```sh
git clone git@github.com:alberthild/audio2dj.git
cd audio2dj
./scripts/build.sh
./scripts/package.sh
```

Output: `dist/Audio2DJ-Experimental-0.1.1-intel.pkg`. The repository includes the
pinned libASPL source, the original libusb source archive and a documented shutdown patch, so no Homebrew or
network dependency is needed after cloning. Build flags match the prototype;
only the build host's architecture is supported. See `THIRD_PARTY_NOTICES.md`.

## Diagnose

Read `~/Library/Logs/Audio2DJExperimental.log`. `errors`, `device`, `underruns`,
`overruns` and feedback counts are cumulative per bridge process. The log rolls
over above 5 MiB when the packaged launcher next starts. Do not interpret the
absence of a USB error as proof of uninterrupted audio.

```sh
launchctl print "gui/$(id -u)/org.local.audio2dj.bridge"
tail -n 30 "$HOME/Library/Logs/Audio2DJExperimental.log"
```

When reporting an issue, include macOS/model, USB hub/cable details, app/sample
rate, whether the symptom is crackle or a full dropout, and a redacted log excerpt.
Do not post full system reports, personal paths, serial numbers or music captures.

## Uninstall

Close audio apps, then run the installed helper:

```sh
sudo bash '/Library/Application Support/Audio2DJExperimental/uninstall.sh' --yes
```

It removes this package's HAL plugin, bridge and global LaunchAgent, then restarts
CoreAudio. Logs, disabled legacy agent plists and backups of a previous HAL bundle
are retained. Backups live in `/Library/Application Support/Audio2DJExperimental-Backups`.
To restore a previous version, reinstall its package or manually restore its HAL
backup and matching bridge. The helper does not silently enable legacy drivers.

## Architecture and licensing

CoreAudio → libASPL HAL plugin → loopback UDP (`127.0.0.1:49172`, interleaved Float32)
→ user-space bridge → libusb isochronous transfers → Audio 2 DJ.

The bridge converts audio to the caiaq 24-bit wire format. Device feedback supplies
the required total samples per 32 microframes. That count is distributed across
nonempty output packets because zero-length output packets were rejected by the
macOS USB path. Software resampling reconciles the CoreAudio and device clocks.
No kernel extension is loaded. Loopback UDP has no authentication; this prototype
assumes trusted processes on a single-user Mac and is not hardened against hostile
local applications. Do not expose this port beyond loopback.

Bridge and project scripts: GPL-2.0-or-later. HAL adapter/libASPL: MIT and included
Apple notices. libusb: LGPL-2.1-or-later. Full source for the statically linked
library and build instructions are included to permit modification and relinking.
No proprietary NI binaries or firmware are redistributed.
