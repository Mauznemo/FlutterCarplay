## Style

- Never use em dashes (—) or en dashes (–), anywhere: not in UI strings, dart docs, normal comments, or markdown. Use a comma, parentheses, or a separate sentence instead.
- When defining widgets or classes put all the vars on top of the constructor and not the other way around.
- Update this or other AGENTS.mds if the info in here changes or something new is worth adding if it will be needed for every later session. Do not clutter it with one of info or things that are just common sense or easy to figure out without having it here. If you think something one off needs explaining the dart docs with /// is the right place, not this file.

## What this project is

A Flutter plugin that runs an Apple CarPlay head unit inside a Flutter app, rendering the
projected iPhone screen into a Flutter `Texture` so the host app can draw widgets on top. The
sibling of FlutterAndroidAuto (`../FlutterAndroidAuto` on the author's machine,
github.com/Mauznemo/FlutterAndroidAuto), deliberately the same shape, and the source of most of
the native pipeline here. Linux first, Android second, from one native package: see "Android"
below.

CarPlay comes from a **Carlinkit CPC200-CCPA adapter** over USB: the adapter is the CarPlay
receiver and hands the host H.264 and PCM. A **native CarPlay backend** (iAP2 and AirPlay on the
host, LIVI style, the adapter or an MFi chip only signing) is the planned second backend behind
the same seam. `docs/research.md` has why.

**Read `PLAN.md` first in every session.** It holds the milestones with checkboxes and is the
source of truth for what is done and what is next. Tick a box only when the thing is verified on
this machine, not when it merely compiles. Also update the status table at the top of `PLAN.md`
when a milestone changes state.

Background reading, only when relevant: `docs/architecture.md` (how the pieces fit),
`docs/dongle-protocol.md` (the adapter's USB protocol, every id and offset with its source),
`dev/dev-environment.md` (this machine). `docs/research.md` is a dated record of what was known
before the work started, not current guidance; read it for why a decision was taken, never for
what the code does now.

## Layout

| Path | What |
|---|---|
| `packages/carplay` | app facing API, pure Dart: `CarPlayController`, `CarPlayView` |
| `packages/carplay_platform_interface` | the contract and the metadata models, pure Dart |
| `packages/carplay_native` | the Linux implementation. `src/` is the native core, `linux/` the GTK entry point and CMake, `native_test/` the native unit tests |
| `example/` | the reference integration and test bench, run this to verify anything visually. `lib/head_unit/` is what a product would ship, `lib/bench/` the diagnostic parts |
| `tools/` | what a stranger needs: `setup-dev-machine.sh` (build deps, the udev rule), `native-test.sh` |
| `dev/` | this machine's own tooling, not part of the plugin: `ui.sh`, `run-example.sh`, `dev-environment.md`, and the fake adapter |
| `pubspec.yaml` | the workspace root, not a package. Its only content is the member list |

The `tools/` and `dev/` split is android_auto's: `tools/` is for anyone who clones this, `dev/` is
for driving this particular machine. Nothing in `tools/` may call into `dev/`.

## Driving the author's machine, which is not every machine

**Everything in this section is one laptop.** The short version is KDE Plasma on Wayland, one
1920x1080 output at scale 1 so screenshot pixels equal screen coordinates, a German keyboard, and
passwordless sudo. X11 tools (xdotool, scrot, wmctrl) do not work here. On any other machine,
expect none of it to apply and do not teach the plugin about it.

```bash
dev/ui.sh setup              # once per boot: starts ydotoold, flattens pointer accel
dev/ui.sh shot               # full screen png, prints the path, then Read it
dev/ui.sh crop <f> <x> <y> <w> <h>
dev/ui.sh click <x> <y>      # also: move, rclick, drag, scroll
dev/ui.sh key ctrl+s         # also: esc, enter, tab, f1..f12
dev/ui.sh paste "text"       # use this, not `type`, see below
dev/run-example.sh --bundle  # build and run the binary directly, line buffered log in /tmp/cp-example.log
dev/run-example.sh --bg      # through flutter run, detached
```

Rules rather than description:

- **`dev/ui.sh setup` is required after every reboot.** Without the flat pointer acceleration it
  sets on the ydotool virtual device, clicks land in the wrong place.
- **The keyboard layout is German (QWERTZ)** and ydotool sends raw keycodes, so `ui.sh type`
  mangles y/z and symbols. Use `ui.sh paste` for exact text.
- **Never run `pkill -f <pattern>`**, it matches the agent's own shell command line and kills
  the session. Resolve the pid first with `pgrep` into a variable built by concatenation, then
  `kill` it. Match the example on `bundle/carplay_ex""ample`.
- **Never leave two copies of the example app running.** They fight over the adapter, and the
  loser looks exactly like a protocol bug. `dev/run-example.sh` kills the old one first, so use it.
- **The author uses this desktop while the agent works.** Prefer the example's environment knobs
  (`CP_AUTOSTART=1`, `CP_TEST_PATTERN=1`) over clicking, and never click into a window that is
  not the example app.
- An agent shell may have no `WAYLAND_DISPLAY`; `ui.sh` defaults it to `wayland-0`, without
  which spectacle falls back to X11 and aborts.
- **A heredoc inside a heredoc ends the outer one at the inner `EOF`**, and bash then runs the
  rest as commands, with passwordless sudo. Give nested heredocs distinct delimiters.

## Build and run

The three packages and the example are one Dart workspace, so `flutter pub get` and `flutter
analyze` run from the repository root and there is one `pubspec.lock`, at the root, which is
committed. **`flutter test` on its own does not work from the root**: name the members.

```bash
flutter pub get && flutter analyze
flutter test packages/*/test
tools/native-test.sh                    # the native unit tests, must print OK
cd example && flutter build linux --debug
```

**Wipe a build with `flutter clean`, never `rm -rf example/build`**: the second leaves
`build/native_assets/linux` missing and the next build fails at the install step.

`--bg` runs through `flutter run`, which block buffers the app's stdout, so native logging arrives
in 8 KB lumps minutes late. Use `--bundle` to watch a protocol exchange.

| Knob | What it does |
|---|---|
| `CP_LOG_LEVEL=DEBUG` | the native core's debug lines: every adapter message that is ignored, audio commands, keyframe requests |
| `CP_VIDEO_DECODER=software` | forces the software decoder, to tell a driver problem from a decoder problem |
| `CP_AUTOSTART=1` | the example app presses its own Start. Example app only |
| `CP_TEST_PATTERN=1` | the example app starts the test pattern. Example app only |

Built against Flutter 3.47.4 stable (the snap at `~/snap/flutter/common/flutter` here).
**Impeller is the only renderer on Linux** and runs its OpenGLES backend, so the video pipeline
hands Flutter a dmabuf, never a GL texture, ready for Impeller's Vulkan backend. See
`docs/architecture.md`.

## Committing and pull requests

Do NOT git commit unless you are told to do so! Once told to, for commit message and PR style:
read CONTRIBUTING.md before writing either. The repository is private on GitHub,
`origin` = github.com/Mauznemo/FlutterCarplay.

## Native layout

| File | What |
|---|---|
| `src/cp_core.h` | the flat C ABI, the only thing Dart binds to |
| `src/cp_core.cc` | owns the texture, decoder, audio, microphone and the backend; frame size resolution |
| `src/log.*` | `CP_LOG(info) << ...`, replacing the aasdk logger the copied files used |
| `src/dongle/protocol.*` | every byte offset of the adapter protocol, **no state, no libusb**, the one thing unit tested |
| `src/dongle/stream_parser.*` | messages back out of fixed size reads |
| `src/dongle/usb_link.*` | libusb: the process wide context and its event thread, claim, reads always in flight, the writer thread |
| `src/dongle/usb_device_source*.cc` | where the adapter comes from: libusb's device list on Linux, a descriptor from Java on Android, behind `usb_device_source.h` |
| `src/platform/android_jni.*` | **the only files that name a JNI type**: `JNI_OnLoad`, the Surface and USB calls into `CarplayNativePlugin.java`, stdout to logcat |
| `android/CMakeLists.txt` | the Android build of the same tree, which source replaces which Linux one is listed at its top, and libusb from the submodule |
| `src/dongle/dongle_session.*` | the adapter backend: discovery, start up sequence, heartbeat, timers, dispatch to the shared pipeline |
| `src/frame_ring.*`, `src/present/*`, `src/video/*`, `src/audio/*`, `src/event_bus.*`, `src/metadata/json.*`, `src/test_pattern.*` | **copied from android_auto** and renamed, see below |
| `linux/carplay_native_plugin.map` | the linker version script: `cp_*` and the GTK registrar exported, nothing else |

After changing `cp_core.h`, regenerate the Dart bindings:

```bash
cd packages/carplay_native && dart run ffigen --config ffigen.yaml
```

ffigen finds libclang here as `/usr/lib/x86_64-linux-gnu/libclang-21.so.1` on its own. `CpState`,
`CpAudioStream`, `CpMicrophone` and `CpMetadata` cross FFI as plain integers with Dart enums of
the same order (`CarPlayConnectionState`, `CarPlayAudioStream`, `CarPlayMicrophone`,
`CarPlayMetadata`); a test in the platform interface pins the orders.

## The shared pipeline, and keeping it in step with android_auto

The texture, the decoder, the audio and the microphone are android_auto's files, with these
changes and no others: the `cp` namespace, `CP_LOG` instead of aasdk's logger, the audio streams
renamed media, voice and navigation (ducking under navigation instead of speech), a 20 ms
microphone buffer, and `VideoDecoder::SetKeyframeRequest`. **A fix to one of these files in
either repository probably belongs in both.** android_auto's `AGENTS.md` has the hard won rules
about them (GL texture units, `GlStateGuard`, the dmabuf import, the audio writer threads, the
microphone only opening when asked); they hold here unchanged, read them before touching those
files. `CarPlayView` is likewise a port of `AndroidAutoView`.

## macOS and Windows, which run a simulator

Most host apps are written on a Mac or a Windows machine, so `carplay` registers
`CarPlaySimulator` there (`dartPluginClass` in its pubspec, no native code). It lives in
`packages/carplay/lib/src/simulator/`, and `CP_SIMULATE=1` puts it in the example on Linux. The
same rules as android_auto's simulator:

- **It goes through the same seams as a phone.** The view maps touches and calls `sendTouch` in
  projected video pixels; the simulator hit tests them against its own drawn screen through
  `MetaData` targets rather than taking Flutter gestures.
- **Its frame size is `src/frame_size.cc` ported to Dart** (`simulator/frame_size.dart`).
  Change one and change the other; a test in each pins the same cases.
- **Nothing in it may need the engine to finish a future**, because a host app's own tests run
  against it. Its cover is painted, not decoded from the PNG it sends on `nowPlaying`.
- **Its tests run in fake time.** Make the simulator and controller inside the test, not in
  `setUp` (events scheduled from outside the fake zone are never delivered by `pump()`), drive
  the zero delays with `pump`, never `runAsync`, and never `await` a stream subscription's
  `cancel()`: in fake time it completes on a timer nothing pumps, and the test hangs silently.

## Android

The Android device is the head unit, the same as android_auto's Android port, and the same
shape: `carplay_native` declares `android` beside `linux`, builds the same `src/` tree with the
NDK into `libcarplay_native.so`, and runs the same `CarPlayNative` Dart class over the same C
ABI. One file per seam, listed at the top of `android/CMakeLists.txt`; a new Android backend is
a new `.cc`, not an `#ifdef` in shared code.

```bash
cd example && flutter build apk --debug --target-platform android-arm64
flutter run -d R58N91XQV6V --dart-define=CP_SIMULATE=1 --dart-define=CP_AUTOSTART=1
```

- **The only native dependency is libusb**, a submodule at
  `packages/carplay_native/third_party/libusb` (v1.0.29), compiled straight into the Android
  build from the source list in its own `android/jni/libusb.mk`. No prebuilt deps directory, so
  `git submodule update --init` is the whole setup. Linux uses the system libusb.
- **USB starts in Java.** `CarplayNativePlugin.java` watches `UsbManager` for 1314:1520/1521,
  asks for permission, and hands the adapter over as a file descriptor, which
  `usb_device_source_android.cc` wraps with `libusb_wrap_sys_device`. Java owns the descriptor
  until `CloseAdapter` releases it; never close one under libusb. An adapter still attached
  after a session ended is not announced again, so opening asks Java to rescan.
- **Android needs API 28**, and a host app asks for `RECORD_AUDIO` itself.
- **The example's knobs are `--dart-define`s on Android**, since an app has no environment:
  `example/lib/knobs.dart` reads both.
- **The test device is a Galaxy M21** (LineageOS 20, Android 13, adb serial `R58N91XQV6V`), a
  phone standing in for a head unit; the author connects it when asked. `adb exec-out
  screencap -p` for screenshots, `adb shell input tap x y` for touch, `adb logcat -s CarPlay`
  for the native log. Its first Flutter frame has no size, which is what the controller's view
  grace period in `_windowSize` is for.
- Everything in android_auto's `AGENTS.md` about the present adapter on Android holds here:
  nothing may wait for Flutter's thread from the render thread, and a surface can be taken away
  at any moment.

## The backend seam

A backend gets the decoder, the audio output and the audio input, and three hooks (state, news,
metadata), and nothing else. `DongleSession` is the only one today. A second backend is a new
directory beside `src/dongle/` and a choice in `cp_core.cc`; nothing above the C ABI changes. Keep
everything adapter specific inside `src/dongle/`.

## What the adapter demands

**None of this is verified on hardware yet.** It is what `docs/dongle-protocol.md` collected from
other hosts and from the CCPA's firmware, and every rule cites its source there. When the adapter
arrives, confirm or correct each one and say so here.

- **A read is always pending.** The adapter resets the link when it cannot deliver data for
  4.5 s. `UsbLink` keeps four 64 KiB reads in flight and resubmits each from its own completion.
  Nothing on the event thread may block.
- **Never read until a short packet.** The adapter does not end a 512 byte aligned transfer
  with a zero length packet (album art triggers it). Reads are fixed multiples of 512 and
  `StreamParser` reassembles.
- **Heartbeat every 2 s from the moment the link is up**, never stopped while open. The
  adapter's watchdog is 15 s, with trouble seen from 10. It never echoes one.
- **A link starts with DisconnectPhone and CloseDongle, then 200 ms.** The adapter keeps a phone
  session across a host that died, and then ignores the next Open.
- **Commands 1000 to 1013 from the adapter are status, never a reason to end anything.** 1010
  (Wi-Fi not connected) arrives every twelve seconds while idle. A session ends on Unplugged,
  Phase 0, or the link failing.
- **CarPlay sends a keyframe when a stream starts and then hardly ever.** The decoder asks
  through `SetKeyframeRequest` when it is waiting for one, rationed to two a second because each
  request restarts the phone's encoder. Never request keyframes from an Android Auto phone through
  the adapter: it resets the phone's projection.
- **Open resets the view area to full screen**, so anything about the view or safe area goes
  after it, and a view or safe area larger than the Open size fails negotiation with Phase 13.
- **Names go to a shell on the adapter unescaped.** `SanitizeName` keeps letters, digits, space,
  `-`, `_` and `.`, at most 16.
- **Some paths start a firmware update.** `SendFile` is only ever given the fixed paths at the
  top of `dongle_session.cc`. Never add a path without checking `docs/dongle-protocol.md` 3.5.
- **BoxSettings persist on the adapter** across reboots, so every value the plugin cares about
  is sent on every start. `DayNightMode` 0 is required for the night mode commands to work.
- **Wi-Fi band commands apply after the adapter reboots**, not at once.
- **Firmware versions change behaviour.** Record the version with any finding. Do not let the
  adapter update itself.

## The fake adapter, for working without one

`dev/fake-adapter.sh setup` puts a software Carlinkit adapter on this machine's USB: a self
built `dummy_hcd` (Ubuntu does not ship it) and a configfs FunctionFS gadget, served by
`dev/fake-adapter.py` as the normal user. It answers Open like a CCPA, plugs a fake iPhone and
streams H.264 at the Open size, with flags for audio, navigation prompts, metadata, Siri and the
microphone, and unplug and replug timing (`setup -- --help`). Log in `/tmp/cp-fake-adapter.log`.
`teardown` removes everything, including the temporary udev rule in `/run/udev/rules.d`.

- **It is the protocol as `docs/dongle-protocol.md` describes it, not as the adapter behaves.**
  A pass against it proves the plugin's own plumbing (libusb, the stream parser, the session,
  the decoder, the texture), never that a real CCPA agrees. Anything found on hardware that
  the fake does differently goes into the fake as well.
- **`dev/scenarios.sh` is the regression suite for anything in `src/dongle/` or `cp_core.cc`.**
  Run it before calling such a change done; every scenario must say PASS. A new recovery path
  gets a scenario, and the fake a flag for the quirk it recovers from (`--sticky-session` and
  `--phase13-once` are the pattern).
- When exercising audio or the microphone, route the example to a null sink and source, so the
  author hears nothing and their real microphone is not opened.
- Loading `dummy_hcd` taints the kernel until the next reboot. Harmless, but say so.
- Tear it down when done, and never leave the example running against it.

## The instrument cluster

A second pipeline beside the main one, built only when `CarPlayConfig.cluster` names a screen:
its own `FrameRing`, `GlAdapter`, `VideoDecoder` and texture in `cp_core.cc`, fed from 0x2C by
`DongleSession`. The stream starts after a Command 508 handshake: one side sends 508, the other
answers, the first echoes. Over a cable the adapter starts it; over Wi-Fi it never does, so the
head unit starts it two seconds after Plugged if nothing came. Echoes are counted per phone
(`kMaxClusterEchoes`), not timed: a time gap swallowed the echo to the adapter's answer, which
arrives within milliseconds of the head unit's own 508. No keyframe requests for the cluster
stream: nothing documents one, and it sends an IDR about every 26 s anyway.

## Numbers in wire formats

**Never format a double with printf into anything that goes on the wire.** The Flutter app runs
with the user's locale, and on the author's German machine `%.4f` writes `5231,2005`: NMEA's
fields are comma separated, and JSON has no such number. Found with the GPS sentences, against
the fake adapter, after the native tests (which run in the C locale) had passed.
`src/text_format.h` formats through `std::to_chars`, which ignores the locale; use it, and test
under `de_DE` as `native_test/nmea_test.cc` does. android_auto's `Json::AddDouble` has the same
latent bug, unused there today.

## The car's position, which is a promise

`CarPlayConfig.gps` tells the phone the car has a position, and a phone told that stops using
its own receiver: offering it and then not feeding `setLocation` takes navigation away. The same
rule android_auto's sensors follow. Fixes go out only while the phone has asked (Command 18 until
19), once a second and on every new one.

## Audio and the microphone through the adapter

- Two inbound streams, main (1) and navigation (2). The main one carries music at 48 kHz stereo
  and Siri and calls at 16 kHz mono, so the session routes by format too: mono voice goes to the
  voice sink, so a Siri reply is not a reopen of the music sink mid song.
- What follows the 12 byte audio header is told apart by length: 1 byte is an audio command, 4 a
  volume ramp, nothing is empty, anything else PCM.
- The microphone opens on Command 1 or audio command 8 (Siri) or 4 (call), and closes on 2, 9 or
  5, but not while a call or Siri still wants it. Its rate is the decode type of the latest
  INPUT_CONFIG (3), 16 kHz normally. Mono, 20 ms buffers, audio type 3.

## Frame size

CarPlay takes any size, so the frame is the view's own in physical pixels, rounded to even,
within 2560x1440 (`src/frame_size.*`, unit tested), settled at Start. No margins, nothing
cropped, unlike android_auto, whose `FrameSizeForView` was removed from the copied
`video_margins.*` for that reason. Touch is measured against the decoded picture's size,
because the phone's first frames can be a boot screen of another size. A resize while running
needs a new Open, which drops the phone, so `DongleSession::RequestFrameSize` waits for the
view to hold still for a second before reconfiguring; `CarPlayConfig.followViewResize` turns it
off.

## Things this project does not do

- **Bundle or fetch a leaked accessory identity** (the Carlinkit key recovered from firmware in
  2026). If the native backend ever reads a certificate and key from a file, the file is the
  user's to provide.
- Present itself as certified or affiliated with Apple.
