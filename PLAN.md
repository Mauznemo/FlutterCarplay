# Flutter CarPlay: implementation plan

A Flutter plugin that runs an Apple CarPlay head unit **inside** the Flutter app, rendering the
projected iPhone screen into a Flutter `Texture` so the host app can draw its own widgets on top.
The sibling of [FlutterAndroidAuto](https://github.com/Mauznemo/FlutterAndroidAuto), shaped the
same way so one infotainment system can carry both.

Linux first. CarPlay comes from a Carlinkit CPC200-CCPA adapter first, and from a native CarPlay
backend later; `docs/research.md` says why.

> Working agreement: every box below is a checkpoint. Tick it only when the thing is actually
> verified on this machine, not when the code merely compiles. Sessions are expected to pick up
> from the first unticked box.

---

## 0. Status at a glance

| Milestone | Title | State |
|---|---|---|
| M0 | Project setup, research, decisions | **done** |
| M1 | Shared pipeline from android_auto: texture, decoder, audio, events | **done** |
| M2 | Adapter protocol layer, unit tested | **done** |
| M3 | USB link and session, against a fake adapter | **done** |
| M4 | First projection from a real CPC200-CCPA | blocked on the hardware |
| M5 | Audio: media, voice, navigation, ducking | written, the pipeline verified against the fake adapter |
| M6 | Microphone: Siri and calls | written, the pipeline verified against the fake adapter |
| M7 | Input: touch, keys, Siri button, night mode | written, unverified |
| M8 | Metadata: now playing, album art, navigation, calls | written, now playing and navigation verified against the fake adapter |
| M8b | Extras: safe area, the car's position, cluster video | all three verified against the fake |
| M9 | Wireless CarPlay through the adapter | written, unverified |
| M10 | Robustness: replugs, restarts, firmware versions | **in progress**, every case passing against the fake adapter, none yet on hardware |
| M11 | Packaging, CI, ARM64, release | in progress |
| M12 | Simulator for macOS and Windows | **done**, bar running it on a Mac or Windows machine |
| M13 | Native CarPlay backend | not started |
| M14 | Android as the head unit | **in progress**, builds and runs on the test device, bar a real adapter |

---

## M0. Project setup, research, decisions

- [x] Research whether CarPlay can run without extra hardware (`docs/research.md`)
- [x] Decide the hardware: a CPC200-CCPA first, a native backend later, confirmed with the author 2026-11-05
- [x] **Decide the licence**: GPL-3.0-or-later, confirmed 2026-11-05
- [x] Decide the package names: `carplay`, `carplay_platform_interface`, `carplay_native`, confirmed 2026-11-05
- [x] Decide the platforms: Linux now, Android later, confirmed 2026-11-05
- [x] Write the adapter protocol reference from public sources (`docs/dongle-protocol.md`)
- [x] `git init`, a private GitHub repository at Mauznemo/FlutterCarplay, linked as `origin`
- [x] Scaffold the federated packages, the Dart workspace and the example app
- [x] Write `AGENTS.md`, `PLAN.md`, `docs/architecture.md`, `docs/research.md`
- [ ] Order a CPC200-CCPA and note its firmware version here when it arrives (blocks M4)

## M1. Shared pipeline from android_auto

Copied from android_auto rather than written: the parts of its native core that never touched
aasdk. Renamed to the `cp` namespace, aasdk's logger replaced by `src/log.h`.

- [x] `frame_ring`, `present/gl_adapter`, `present/texture_registry`, `test_pattern`
- [x] `video/libav_decoder`, `video/annex_b`, `video/video_margins`
- [x] `audio/` sinks, sources, `AudioOutput`, `AudioInput`, with the streams renamed media, voice, navigation
- [x] `event_bus`, `metadata/json`
- [x] The decoder asks for a keyframe when it drops packets waiting for one (`SetKeyframeRequest`), because CarPlay hardly ever sends one by itself
- [x] The microphone sends 20 ms buffers, the adapter's frame size, instead of android_auto's 32
- [x] Linux plugin build, version script exporting only `cp_*` and the GTK entry point (checked with `nm -D`: 44 symbols)
- [x] Verify by screenshot: the test pattern renders in `CarPlayView` at exactly the view's size (1280x632 under the example's status bar), corner markers flush, overlays on top
- [x] Verify audio output and ducking with a PCM source: against the fake adapter, media opened at 48 kHz stereo and ducked and came back around a navigation prompt (into a null sink)
- [x] Verify the microphone opens only when asked and closes after: against the fake adapter, on Command 1/2 and on Siri, 16 kHz, about 32000 bytes/s, decode type 5

## M2. Adapter protocol layer

- [x] `dongle/protocol.*`: every message type, command id, audio command and the framing, no state, no libusb
- [x] `dongle/stream_parser.*`: messages back out of fixed size reads, whatever the chunk boundaries, resyncing on garbage
- [x] `native_test/`: byte level tests against the published captures (heartbeat, keyframe request, the `/tmp/screen_dpi` SendFile, Open, MultiTouch, microphone upload, audio and video parsing, Plugged), and the parser at chunk sizes from 1 byte to 1 MB with an album art sized message. `tools/native-test.sh` prints OK

## M3. USB link and session, against a fake adapter

- [x] `dongle/usb_link.*`: claim, four 64 KiB reads always in flight, a writer thread, failure reporting
- [x] `dongle/dongle_session.*`: discovery every second, stale session cleared on open, the start up sequence 120 ms apart, heartbeat every 2 s from the moment the link is up, auto connect and pairing timers, dispatch
- [x] With no adapter: Start reports searching and "No Carlinkit adapter on USB." once, retries quietly
- [x] A fake adapter on this machine (`dev/fake-adapter.sh`, dummy_hcd and FunctionFS) that answers like a CCPA and streams H.264
- [x] Against it: the start up sequence goes out in order, heartbeats every 2 s, the picture appears (1280x632, VA-API, 1.0 ms wire to frame), the overlays fill from metadata. Verified by screenshot
- [x] Against it: the fake unplugging the phone ends the picture; replugging brings it back
- [x] Against it: a USB unbind mid stream fails the link at once (fixed: a read that could not be resubmitted went unnoticed until the next write), and a rebind reconfigures it and the picture returns
- [x] Against it: Stop sends DisconnectPhone and CloseDongle, releases the interface, and Start works again (`example/integration_test/adapter_test.dart`)
- [x] `dev/scenarios.sh` runs every case below against the fake and says PASS or FAIL for each: basic, phase13, sticky, unplug, all PASS on 2026-11-05

## M4. First projection from a real CPC200-CCPA

- [ ] udev rule installed (`tools/setup-dev-machine.sh --udev`), the adapter opens as a normal user
- [ ] Note the firmware version, hardware revision and USB product id in `AGENTS.md`
- [ ] A wired iPhone projects, picture at the view's size, VA-API decoding
- [ ] Measure wire to frame latency and compare with android_auto's 1 ms
- [ ] The first frames' boot screen size switch is handled without a visible glitch

## M5. Audio

- [ ] Music plays on the media stream at 48 kHz stereo, no underruns over ten minutes
- [ ] A Maps prompt plays on the navigation stream and media ducks under it
- [ ] Siri's voice and call audio play on the voice stream at 16 kHz mono
- [ ] Volume and mute per stream, device selection
- [ ] Decide what to do with CarPlay's own volume ramp packets, logged and ignored today

## M6. Microphone

- [ ] Siri hears the head unit's microphone (Command 1 or audio command 8 opens it)
- [ ] A call's uplink works (audio command 4, the rate from INPUT_CONFIG)
- [ ] `pactl list short source-outputs` is empty except while Siri or a call is active

## M7. Input

- [ ] Touch lands where the finger is, at the view's size and at a shrunk view
- [ ] Two finger pinch zooms in Maps
- [ ] Every `CarPlayKey`, with a note of which ones CarPlay actually reacts to
- [ ] Siri held from the dock button
- [ ] Night mode switches the phone's appearance at once, both ways

## M8. Metadata

- [ ] Now playing, merged across partial updates, with album art
- [ ] Turn by turn from Apple Maps, every maneuver icon the example maps
- [ ] A call's status, incoming and outgoing
- [ ] The car icon in CarPlay sends `CarPlayRequest.showHostUi`, with a custom icon from `CarPlayConfig.carIcon`
- [ ] Adapter info and phone info on the example's adapter panel

## M8b. Extras from the protocol

- [x] Safe area (`CarPlayConfig.safeArea`): `HU_VIEWAREA_INFO` and `HU_SAFEAREA_INFO` after Open and BoxSettings, clamped and even. Reaches the fake as `760, 460, 40, 0, 1` for 40 left and 20 bottom on 800x480 (`scenarios.sh features`)
- [x] The car's position (`CarPlayConfig.gps`, `setLocation`): GPGGA and GPRMC sent while the phone asks (Command 18), once a second and on every new fix, `GNSSCapability` 3. Reaches the fake (`scenarios.sh features`). Found and fixed on the way: the NMEA had decimal commas under a German locale
- [ ] Real: CarPlay keeps its controls inside the safe area
- [ ] Real: Maps uses the car's position (and whether GnssData wants the u32 length prefix the CCPA docs show)
- [x] Instrument cluster video (`CarPlayConfig.cluster`, `CarPlayClusterView`): `naviScreenInfo` in BoxSettings, the Command 508 handshake both ways (the adapter starts it over a cable, the head unit over Wi-Fi), the 0x2C stream into a second decoder and texture. Against the fake, both directions, verified by screenshot beside the main picture (`scenarios.sh features`, `CP_CLUSTER=1`)
- [ ] Real: a phone sends the cluster stream (the CCPA docs disagree on whether stock units need an `AdvancedFeatures` unlock first)

## M9. Wireless CarPlay through the adapter

- [ ] Pair an iPhone with the adapter, "Use This Car" on the phone
- [ ] It reconnects wirelessly on the next start without the cable
- [ ] The pairing mode timer does not abort a reconnect in progress
- [ ] 5 GHz and 2.4 GHz both work, after the adapter's reboot

## M10. Robustness

Each case first against the fake adapter (which only proves the plugin does what
`docs/dongle-protocol.md` says the adapter needs), then on the real one.

- [x] Fake: adapter unbound and rebound mid session
- [ ] Real: adapter unplugged and replugged mid session
- [x] Fake: phone unplugged and replugged mid session (`scenarios.sh unplug`)
- [ ] Real: phone unplugged and replugged mid session
- [x] Fake: host app killed with a phone connected while the adapter keeps the session (`--sticky-session`), restarted: the stale session is cleared and it projects again (`scenarios.sh sticky`)
- [ ] Real: the same, which is what the stale session clearing is for
- [x] Fake: Phase 13 on the first negotiation, the plugin reconfigures once and projects (`scenarios.sh phase13`)
- [ ] Real: Phase 13 provoked with a deliberately oversized view area
- [x] A view resized mid session is followed: a new Open at the new size after the view holds still for a second, the phone back in about 3.6 s against the fake. Optional, `CarPlayConfig.followViewResize`
- [ ] Two firmware versions compared, 2025.02.25.1521 and 2025.10.15.1127

## M11. Packaging, CI, ARM64, release

- [x] CI: analyze, Dart tests, native unit tests, PR title check
- [ ] CI: Linux build on x86_64 and ARM64
- [x] `docs/packaging.md`: what a machine needs to run a build, read off the built library
- [ ] Publish to pub.dev, once M4 has projected (the pubspecs say `publish_to: none` until then)

## M12. Simulator for macOS and Windows

android_auto registers a pure Dart simulator there, because most host apps are written on a Mac
or a Windows machine. The same idea here, sharing as much of its shape as makes sense.

- [x] `CarPlaySimulator` in `packages/carplay`, through the same seams as a phone: a dock, home screen, maps with guidance, music, phone with calls, Siri, the car icon sending `showHostUi`, night mode, following a resize
- [x] Registered with `dartPluginClass` for macOS and Windows; `CP_SIMULATE=1` in the example on Linux
- [x] Verified by screenshot on Linux: two clicks through the real view opened Maps and started a route, and the example's own turn card followed the simulated guidance
- [x] Tests in fake time (`packages/carplay/test/simulator_test.dart`)
- [ ] Built and run on macOS or Windows (CI's job, once CI runs)

## M13. Native CarPlay backend

The full integration, the way LIVI does it: iAP2 and AirPlay on the host, the adapter or an MFi
chip only for the authentication signature. See `docs/research.md`.

- [ ] Decide the signer hardware: a CCPA or a Carlinkit Mini Ultra 3 reflashed as LIVI Link, or an MFi chip on I2C through a CH341
- [ ] Study LIVI's `iap2-*` and `livi-cp` crates (GPL-3.0 with an attribution term) and decide: port to C++, or link the Rust crates through a C ABI
- [ ] A `native/` backend directory beside `dongle/`, behind the same decoder, audio and hook seam
- [ ] Wired iAP2 identification and authentication against a signer
- [ ] AirPlay screen and audio over USB NCM
- [ ] Wireless over the host's own Wi-Fi and Bluetooth, reusing what android_auto learned

## M14. Android as the head unit

android_auto's Android port is one file per seam (`mediacodec_decoder`, `gl_adapter_android`,
`aaudio_sink`, `aaudio_source`, USB from Java through `UsbManager`). The same files apply here.

- [x] Copy the Android seams (`mediacodec_decoder`, `gl_adapter_android`, `aaudio_sink`, `aaudio_source`, `platform/android_jni`), with the keyframe hook added to the MediaCodec decoder and the AAudio usages mapped to media, voice and navigation
- [x] `CarplayNativePlugin.java`: texture surfaces, and USB watching for 1314:1520/1521 with the permission dialog, handing the adapter over as a file descriptor
- [x] The USB device source as a seam (`dongle/usb_device_source*.cc`): libusb lists the bus on Linux, wraps the descriptor on Android. libusb 1.0.29 is a submodule compiled into the Android build, no prebuilt dependencies
- [x] Verified on the test device (Galaxy M21, LineageOS 20, Android 13): the APK builds for arm64, installs and starts; the test pattern renders through the SurfaceProducer; Start reports "No Carlinkit adapter on USB." from the Java side and keeps looking; the simulator projects and an `adb` tap on the touchscreen opens Maps through the view's touch mapping
- [x] Found on the device and fixed for both platforms: a start made before the view's first layout asked for a frame the size of the window. The controller now gives the view 300 ms to report its own size first
- [ ] Project through a CCPA on an Android head unit
- [ ] armeabi-v7a and x86_64 builds (configured, not yet built)
