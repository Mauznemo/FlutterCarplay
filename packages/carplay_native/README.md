# carplay_native

The Linux implementation of [`carplay`](../carplay): a native core behind a flat C ABI
(`src/cp_core.h`), bound with `dart:ffi`.

**You do not depend on this directly.** Depend on `carplay`; this package is endorsed by it.

- **The adapter**: a Carlinkit CPC200-CCPA over libusb, its protocol in `src/dongle/`.
- **Video**: H.264 decoded through VA-API into a dmabuf, imported as an `EGLImage` and converted
  on Flutter's raster thread, with a libswscale fallback. Shared with android_auto.
- **Audio**: media, voice and navigation as three PulseAudio streams, mixed and ducked here, and
  the microphone, opened only while the phone asks for it.

Building needs libusb, libavcodec, libavutil, libswscale, libpulse and GTK 3 development
packages. A normal user needs a udev rule to open the adapter; the repository's
`tools/setup-dev-machine.sh --udev` writes one. GPL-3.0-or-later.
