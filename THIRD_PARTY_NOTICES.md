# Third-party sources and notices

- `src/bridge.c`: derived from the Linux `sound/usb/caiaq` protocol and packing
  implementation, GPL-2.0-or-later. Original copyright 2006–2008 Daniel Mack and
  Karsten Wiese. macOS bridge modifications copyright 2026 Albert Hild.
  Reference: https://github.com/torvalds/linux/tree/master/sound/usb/caiaq
- `src/Driver.cpp`: adapted from the libASPL NetcatDevice example, MIT.
  `vendor/libASPL`: https://github.com/gavv/libASPL at commit
  `633e0f70203edd87d320fc5a3cae901e1363aac5`.
  Preserve `LICENSE`, `LICENSE.apple2012`, `LICENSE.apple2020` and source notices.
- `vendor/libusb-1.0.29.tar.bz2`: unmodified official libusb 1.0.29 source,
  https://github.com/libusb/libusb/releases/tag/v1.0.29, LGPL-2.1-or-later;
  see the archive's `COPYING` and per-file notices.
  SHA-256: `5977fc950f8d1395ccea9bd48c06b3f808fd3c2c961b44b0c2e6e29fc3a70a85`.
- `vendor/patches/libusb-darwin-shutdown.patch`: backport of upstream commit
  `94a5224ea1a7515c38c618694e8794e058c16412` by Sylvain Fasel, retaining libusb's
  LGPL-2.1-or-later license. Adapted to 1.0.29 pthread mutex/condition types.
  https://github.com/libusb/libusb/commit/94a5224ea1a7515c38c618694e8794e058c16412
- Root `LICENSE` supplies GPL version 2; project bridge/scripts permit version 2
  or any later version. Individual permissively licensed components retain their
  licenses. All required source and relinking/build inputs are distributed in this
  repository and the accompanying source archive.

Native Instruments, Audio 2 DJ, Traktor and djay are trademarks of their owners.
This independent community project is not endorsed by those companies.
