# Packaging: shipping a build, and what a machine needs to run it

## Linux

`flutter build linux` produces a bundle with `lib/libcarplay_native_plugin.so` in it. The
plugin links nothing statically and bundles nothing beside itself: there is no aasdk here, so
everything it needs beyond Flutter comes from the system.

### What the machine running the build needs

Read off the built library with `readelf -d` on Ubuntu 26.04; the package names are that
release's, and the sonames tell you which version a distribution has to provide.

| Library | Debian / Ubuntu package | For |
|---|---|---|
| `libusb-1.0.so.0` | `libusb-1.0-0` | the Carlinkit adapter |
| `libavcodec.so.62`, `libavutil.so.60`, `libswscale.so.9` | `libavcodec62`, `libavutil60`, `libswscale9` | H.264 decode, VA-API, and the software fallback's conversion |
| `libpulse.so.0`, `libpulse-simple.so.0` | `libpulse0` | audio out and the microphone, through PulseAudio or PipeWire's PulseAudio server |
| `libepoxy.so.0` | `libepoxy0` | GL entry points, the same dispatch GTK uses |
| GTK 3 and its stack | `libgtk-3-0t64` | Flutter's Linux embedder, needed by any Flutter app |

For the hardware decode path the machine also needs a VA-API driver for its GPU
(`intel-media-va-driver`, `mesa-va-drivers`, or the vendor's). Without one, the decoder
falls back to the CPU by itself and says so in the log; nothing fails.

A head unit image built with something other than apt needs the same sonames. An ARM64 image
needs them for ARM64; the plugin has no x86 assumption in it.

### Permissions

A normal user cannot open the adapter without a udev rule:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1314", ATTR{idProduct}=="1520", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="1314", ATTR{idProduct}=="1521", MODE="0660", GROUP="plugdev", TAG+="uaccess"
```

`tools/setup-dev-machine.sh --udev` writes exactly this to
`/etc/udev/rules.d/99-carplay-adapter.rules`. `uaccess` covers a desktop session; `plugdev`
covers a head unit that runs its app without a seat session, whose user then has to be in that
group. Both product ids, because the id is a setting in the adapter's firmware.

### Building it

The build machine needs the development packages instead:

```bash
sudo apt-get install cmake ninja-build pkg-config clang \
  libgtk-3-dev libusb-1.0-0-dev libavcodec-dev libavutil-dev libswscale-dev \
  libpulse-dev libepoxy-dev libegl1-mesa-dev libgles2-mesa-dev
```

`tools/setup-dev-machine.sh --build-deps` installs these and ccache, and the libstdc++
development package clang needs on distributions where the newest GCC is not the default.

## Android

The plugin builds `libcarplay_native.so` for arm64-v8a, armeabi-v7a and x86_64 with the NDK,
inside the host app's own build. Its only native dependency, libusb, is compiled from the
submodule at `packages/carplay_native/third_party/libusb`, so nothing has to be built
beforehand and nothing prebuilt is shipped.

A host app needs:

- `minSdk = 28`, for AAudio's stream usages and the voice recognition microphone.
- The `RECORD_AUDIO` permission, asked for at runtime by the host app; the plugin declares it
  but never asks. Without it Siri and calls hear silence.
- Optionally a `USB_DEVICE_ATTACHED` intent filter with a device filter for 1314:1520 and
  1314:1521, so plugging the adapter in opens the app and the user can allow it for good. The
  example app has one, see `example/android/app/src/main/res/xml/usb_device_filter.xml`.
  Without it, the plugin asks for USB permission itself when the adapter appears.

USB host mode is declared as a feature that is not required, so an app built with the plugin
still installs on devices without it.

## Publishing

All three pubspecs say `publish_to: none` until something has projected through a real
adapter (`PLAN.md`, M4). When they are published, the cross package dependencies are caret
constraints rather than paths, for the reason android_auto's `docs/releasing.md` gives: pub
will not publish a path dependency, and a Dart workspace resolves its own members anyway while
the versions satisfy the constraints.
