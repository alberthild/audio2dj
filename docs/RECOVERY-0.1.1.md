# USB recovery fix — 0.1.1

## Observed failure

After a device-not-found error, the original bridge remained alive but silent.
A process sample showed:

- Main thread: `libusb_exit → darwin_exit → pthread_cond_wait`.
- Hotplug thread: `darwin_devices_attached → darwin_get_cached_device → pthread_mutex_lock`.

In libusb 1.0.29, `darwin_exit` held the cached-device mutex while waiting for the
hotplug thread to exit; that thread could be waiting for the same mutex. launchd
could not restart a process that had not exited.

## Change

Backport upstream commit
[94a5224ea1a7515c38c618694e8794e058c16412](https://github.com/libusb/libusb/commit/94a5224ea1a7515c38c618694e8794e058c16412)
by Sylvain Fasel. The cache lock is released before stopping/joining the event
thread and reacquired afterward for cache cleanup. Backend init/exit remain
serialized by libusb's core default-context lock.

The bridge also arms a five-second deadline before transfer cancellation or early
error cleanup. If teardown remains stuck, an async-signal-safe handler exits with
failure so launchd can restart it. Normal teardown cancels this deadline.
The audio processing and routing are unchanged. This does not bound startup calls
or all possible streaming stalls, and does not eliminate physical USB interruptions.

## Validation

- Original shutdown code deterministically deadlocks in a coordinated hotplug test.
- Actual patched shutdown function passes 50 repetitions, including non-last-context exit.
- Production cleanup handler exits a stuck test process after five seconds; normal
  cleanup cancels the deadline.
- 100 real libusb init/enumerate/exit cycles pass without opening/resetting hardware.
- Full Intel build and package verification pass: payload hashes, architecture,
  ad-hoc HAL signature, LaunchAgent, script syntax and license files.
- After replacing the running bridge, an initial `LIBUSB_ERROR_NOT_FOUND` recovered
  automatically and streaming resumed with nonzero PCM and zero reported USB errors.
- Independent second-agent code review found no blocking issues, and checked lock
  ordering, core lifecycle serialization, signal safety, build patch application,
  regression-test design, shell/Python syntax and diff formatting.

The package Installer lifecycle on a clean Mac and long-term live performance
remain unvalidated. This release is still experimental and unsigned/not notarized.
