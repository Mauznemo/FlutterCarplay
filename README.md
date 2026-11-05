# Flutter CarPlay

A Flutter plugin that runs an Apple CarPlay head unit **inside** a Flutter app.

The iPhone's screen is rendered into a Flutter `Texture`, so the host app can composite its own
widgets on top of it, and what the phone says about itself (the track playing, turn by turn
guidance, calls) arrives as data for the app to draw in its own design. No separate window, no
second process.

The sibling of [Flutter Android Auto](https://github.com/Mauznemo/FlutterAndroidAuto), with the
same shape of API, so one infotainment system can carry both.

> **Status: in development, nothing has projected yet.** CarPlay comes from a Carlinkit
> CPC200-CCPA adapter over USB. The plugin is written against the adapter's protocol from public
> documentation, its byte layer is unit tested, and the video, audio and texture pipeline is the
> one android_auto ships. The first real adapter has not arrived yet. See [`PLAN.md`](PLAN.md)
> for exactly what is verified and what is not.

## Why an adapter

An iPhone only projects to a head unit that proves it is an Apple licensed accessory, with a
certificate and a signature from an authentication chip. A Linux machine has no such chip. A
Carlinkit CPC200-CCPA has one: it is the CarPlay receiver, wired and wireless, and hands the host
H.264 video and PCM audio over USB. [`docs/research.md`](docs/research.md) has the research,
including the native route LIVI took in 2026 (CarPlay implemented on the host, with only the
signature from hardware), which is this project's planned second backend.

## Usage

```dart
final controller = CarPlayController(
  config: const CarPlayConfig(name: 'My Car'),
);

Column(
  children: [
    MyStatusBar(),                              // your widgets, beside it
    Expanded(
      child: Stack(
        children: [
          CarPlayView(controller: controller),  // the iPhone's screen
          MyNowPlayingCard(),                   // on top, fed by metadata
        ],
      ),
    ),
  ],
)
```

`controller.start()` begins looking for the adapter and `controller.state` follows it: searching,
waiting for a phone, connected. The phone draws a screen exactly the view's size, so a status bar
beside the view takes space from CarPlay rather than covering it. `CarPlayView` maps its own
touches, up to five fingers, so input needs no wiring.

Everything else is on the controller:

| | |
|---|---|
| Keys | `pressKey(CarPlayKey.home)`, the rotary controller, media keys, Siri held with `sendKey` |
| Appearance | `setNightMode` |
| Audio | media, voice and navigation as three streams with volume, mute and device selection, media ducked under navigation prompts |
| Microphone | this machine's, opened only while Siri or a call wants it |
| Metadata | `nowPlaying` with album art, `navigation` with the maneuver and distance, `call`, `phone`, `adapter` |
| Requests | `requests` says when the driver taps the car's icon in CarPlay, `CarPlayRequest.showHostUi` |

The example app under [`example/`](example/) is the reference integration.

**On macOS and Windows the package runs a simulator** rather than failing, because that is where
most Flutter apps are written. `start()` connects a pretend adapter and iPhone that draw a
labelled stand in for CarPlay, react to touches and keys through the same view and controller,
and send now playing, guidance, calls and the car icon's request on the same streams.
`CarPlaySimulator.registerWith()` puts it on Linux too.

## Repository layout

```
packages/
  carplay                       app facing API, pure Dart
  carplay_platform_interface    the contract, pure Dart
  carplay_native                Linux implementation, native core in src/
example/                        the reference integration and test bench
docs/
  research.md                   why an adapter, and the native route
  dongle-protocol.md            the adapter's USB protocol, from public sources
  architecture.md               how the pieces fit together
tools/                          what anyone cloning this needs
  setup-dev-machine.sh          build dependencies and the udev rule
  native-test.sh                the native unit tests
dev/                            the author's own machine tooling, not shipped
PLAN.md                         milestones, and what is verified
```

## Getting started

On Ubuntu or Debian:

```bash
tools/setup-dev-machine.sh --build-deps --udev
flutter pub get
cd example && flutter run -d linux
```

`--udev` installs the rule that lets a normal user open the adapter, without which it needs
root. The minimum a machine that only runs a build needs is libusb, libavcodec, libavutil,
libswscale, libpulse and GTK 3.

Press **Test pattern** in the example to see the texture path working with no hardware at all.

Keep the adapter's firmware where it works: do not press update in Carlinkit's own app, and keep
it off the internet. Firmware updates have changed the protocol's behaviour before.

## Licence

**GPL-3.0-or-later**, see [`LICENSE`](LICENSE).

Not affiliated with or endorsed by Apple or Carlinkit. CarPlay is a trademark of Apple Inc.
