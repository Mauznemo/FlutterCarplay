// SPDX-License-Identifier: GPL-3.0-or-later
/// Platform interface for the `carplay` plugin.
///
/// Contains no native code, so another implementation (a native CarPlay stack, a
/// simulator, another adapter) can replace `carplay_native` without any host app
/// change. The contract is shaped after `android_auto_platform_interface` on purpose,
/// so a head unit that offers both can drive them the same way.
library;

import 'dart:typed_data';

import 'package:flutter/widgets.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'src/metadata.dart';

export 'src/metadata.dart';

/// Where a head unit session is in its lifecycle.
///
/// The integers cross FFI as `CpState` in the native `cp_core.h`, so the order matters.
enum CarPlayConnectionState {
  /// Nothing is running.
  idle,

  /// Looking for the adapter on USB.
  searching,

  /// The adapter is ready and no phone is connected to it.
  waitingForPhone,

  /// A phone is connected. Its picture follows a moment later, see
  /// [CarPlayPlatform.hasVideo].
  connected,

  /// The session ended because of an error. See [CarPlayEvent.message].
  error,
}

/// Which microphone the phone listens through, for Siri and calls.
///
/// The integers cross FFI as `CpMicrophone`, so the order matters.
enum CarPlayMicrophone {
  /// This machine's, captured here and sent to the adapter. The CPC200-CCPA has no
  /// microphone of its own, so this is the one that works with it.
  headUnit,

  /// The adapter's own, on models that have one.
  adapter,

  /// The phone's.
  phone,
}

/// Which band the adapter's Wi-Fi uses for wireless CarPlay.
enum CarPlayWifiBand {
  /// 5 GHz, the adapter's default. Faster and less crowded.
  fiveGhz,

  /// 2.4 GHz, for a car where 5 GHz is not allowed or does not reach.
  twoPointFourGhz,
}

/// Which side of the car the driver sits on. CarPlay puts its dock on that side.
enum CarPlayDriveSide {
  /// Left hand drive.
  left,

  /// Right hand drive.
  right,
}

/// How the head unit describes itself to the adapter and the phone.
///
/// Read when the adapter is configured, which is on [CarPlayPlatform.start] and again
/// each time the adapter is plugged back in.
class CarPlayConfig {
  /// Width in pixels of the frame the phone draws, or null to follow the view.
  ///
  /// Left null, which is the default, the frame is the view's own size in physical
  /// pixels, rounded to even, so the phone lays out a screen exactly that big and it is
  /// drawn one to one. Unlike Android Auto, CarPlay has no fixed list of sizes. A view
  /// larger than 2560x1440 gets a frame of its shape that fits inside that, stretched,
  /// because the adapter's memory is the limit. The size is settled when the session
  /// starts and holds until it is started again.
  ///
  /// Set [width] and [height] together to name a size instead.
  final int? width;

  /// Height in pixels of the frame the phone draws, or null to follow the view. See
  /// [width].
  final int? height;

  /// The most frames per second the phone may send. CarPlay sends far fewer while
  /// nothing on screen moves. 20 to 60.
  final int fps;

  /// Screen density the phone lays out for, which decides how big its text and
  /// buttons are.
  final int dpi;

  /// Shown on the phone as the car's name, and used for the adapter's Bluetooth and
  /// Wi-Fi names. Letters, digits, spaces, `-`, `_` and `.`, at most 16 characters;
  /// anything else is dropped, because the adapter hands these to a shell unescaped.
  final String name;

  /// Whether CarPlay starts in its dark appearance. [CarPlayPlatform.setNightMode]
  /// changes it afterwards.
  final bool nightMode;

  /// Which side the driver sits on.
  final CarPlayDriveSide driveSide;

  /// The adapter's Wi-Fi band. The adapter applies a change on its next reboot, not at
  /// once.
  final CarPlayWifiBand wifiBand;

  /// Whether the adapter pairs and reconnects iPhones over Bluetooth and Wi-Fi, so a
  /// phone that has been paired once connects without the cable.
  final bool wireless;

  /// How much audio the adapter buffers before sending it. It will not go below
  /// 500 ms.
  final Duration mediaDelay;

  /// Which microphone the phone listens through.
  final CarPlayMicrophone microphone;

  /// Whether the phone's audio goes to the car's own Bluetooth rather than over USB
  /// to this machine. Off for a head unit that plays the audio itself, which is the
  /// point of this plugin.
  final bool audioOverBluetooth;

  /// Whether the adapter charges the phone at its faster rate.
  final bool fastCharge;

  /// Whether the phone sends turn by turn guidance for [CarPlayPlatform.navigation].
  final bool navigationMetadata;

  /// The icon CarPlay shows for the car's own interface, as PNG, or null for the
  /// adapter's. Tapping it arrives as [CarPlayRequest.showHostUi].
  final Uint8List? carIcon;

  /// The label under [carIcon].
  final String? carIconLabel;

  /// Creates a head unit description. The defaults suit a CPC200-CCPA on a screen of
  /// any size.
  const CarPlayConfig({
    this.width,
    this.height,
    this.fps = 60,
    this.dpi = 160,
    this.name = 'Flutter',
    this.nightMode = false,
    this.driveSide = CarPlayDriveSide.left,
    this.wifiBand = CarPlayWifiBand.fiveGhz,
    this.wireless = true,
    this.mediaDelay = const Duration(milliseconds: 1000),
    this.microphone = CarPlayMicrophone.headUnit,
    this.audioOverBluetooth = false,
    this.fastCharge = true,
    this.navigationMetadata = true,
    this.carIcon,
    this.carIconLabel,
  });
}

/// What the phone is actually sending, once video is flowing.
///
/// Distinct from [CarPlayConfig] on purpose: the config is what was asked for, this is
/// what arrived. An iPhone's first frames can be a small boot screen before it switches
/// to the size it was asked for.
class CarPlayVideoInfo {
  /// Width of the decoded picture in pixels.
  final int width;

  /// Height of the decoded picture in pixels.
  final int height;

  /// "VA-API" or "software".
  final String decoder;

  /// Creates a description of the incoming video.
  const CarPlayVideoInfo({
    required this.width,
    required this.height,
    required this.decoder,
  });

  /// Width over height, or 0 for an empty picture.
  double get aspectRatio => height == 0 ? 0 : width / height;

  @override
  bool operator ==(Object other) =>
      other is CarPlayVideoInfo &&
      other.width == width &&
      other.height == height &&
      other.decoder == decoder;

  @override
  int get hashCode => Object.hash(width, height, decoder);

  @override
  String toString() => 'CarPlayVideoInfo($width x $height, $decoder)';
}

/// What a touch report describes. Android's `MotionEvent` numbering, the same as
/// android_auto's, which is why there are gaps.
enum CarPlayTouchAction {
  /// The first finger landed.
  down(0),

  /// The last finger lifted.
  up(1),

  /// Fingers moved.
  move(2),

  /// Another finger landed while one was already down.
  pointerDown(5),

  /// One finger of several lifted.
  pointerUp(6);

  /// The number that crosses FFI.
  final int code;

  const CarPlayTouchAction(this.code);
}

/// One finger, in projected video pixels from the texture's top left corner.
class CarPlayTouchPoint {
  /// A small index identifying the finger across a gesture, 0 to 4.
  final int id;

  /// Horizontal position in video pixels.
  final int x;

  /// Vertical position in video pixels.
  final int y;

  /// Creates a touch point.
  const CarPlayTouchPoint({required this.id, required this.x, required this.y});

  @override
  String toString() => 'CarPlayTouchPoint(#$id at $x,$y)';
}

/// The keys a head unit can send: steering wheel buttons, a rotary controller, media
/// keys and Siri.
///
/// Each is a command the adapter forwards to the phone. [code] goes out on press, and
/// [releaseCode], where there is one, on release; a key without one acts on press.
enum CarPlayKey {
  /// CarPlay's home screen.
  home(200),

  /// Back.
  back(106),

  /// Focus left, for a rotary controller or a D-pad.
  left(100),

  /// Focus right.
  right(101),

  /// Focus up.
  up(102),

  /// Focus down.
  down(103),

  /// Select what has focus.
  select(104, 105),

  /// Rotary controller one step counter clockwise.
  knobLeft(111),

  /// Rotary controller one step clockwise.
  knobRight(112),

  /// Play.
  play(201),

  /// Pause.
  pause(202),

  /// Play or pause, whichever applies.
  playPause(203),

  /// Next track.
  next(204),

  /// Previous track.
  previous(205),

  /// Siri, held for as long as the button is.
  siri(5, 6),

  /// Answer a call.
  answerCall(300),

  /// Hang up or decline a call.
  hangUpCall(301);

  /// The command sent when the key goes down.
  final int code;

  /// The command sent when it comes up, or null when release sends nothing.
  final int? releaseCode;

  const CarPlayKey(this.code, [this.releaseCode]);
}

/// Which of the three audio streams something is about.
///
/// The integers cross FFI as `CpAudioStream`, so the order matters.
enum CarPlayAudioStream {
  /// Music, podcasts, alerts and ringtones.
  media,

  /// Siri and phone calls.
  voice,

  /// Turn by turn prompts. Media is ducked while one plays.
  navigation,
}

/// An audio output or input the machine has.
class CarPlayAudioDevice {
  /// The name to pass back to select it.
  final String name;

  /// A description a person can read.
  final String description;

  /// Whether it is the audio server's default.
  final bool isDefault;

  /// Creates a device description.
  const CarPlayAudioDevice({
    required this.name,
    required this.description,
    required this.isDefault,
  });

  @override
  bool operator ==(Object other) =>
      other is CarPlayAudioDevice &&
      other.name == name &&
      other.description == description &&
      other.isDefault == isDefault;

  @override
  int get hashCode => Object.hash(name, description, isDefault);

  @override
  String toString() => 'CarPlayAudioDevice($description)';
}

/// One buffer of PCM on its way to the speakers, before volume and ducking.
class CarPlayAudioBuffer {
  /// Which stream it belongs to.
  final CarPlayAudioStream stream;

  /// Interleaved signed 16 bit little endian samples.
  final Uint8List samples;

  /// Samples per second, per channel.
  final int sampleRate;

  /// 1 or 2.
  final int channels;

  /// Creates a buffer description.
  const CarPlayAudioBuffer({
    required this.stream,
    required this.samples,
    required this.sampleRate,
    required this.channels,
  });

  @override
  String toString() =>
      'CarPlayAudioBuffer($stream, ${samples.length} bytes, $sampleRate Hz x $channels)';
}

/// A lifecycle change or a piece of news from the session.
class CarPlayEvent {
  /// The state after this event.
  final CarPlayConnectionState state;

  /// Detail, for a person to read. Nothing should match on its wording.
  final String? message;

  /// Creates an event.
  const CarPlayEvent(this.state, [this.message]);

  @override
  String toString() => 'CarPlayEvent($state${message == null ? '' : ': $message'})';
}

/// The contract every implementation of the `carplay` plugin fulfils.
///
/// Platform implementations extend this rather than implement it, so that a method
/// added here later with a default does not break them.
abstract class CarPlayPlatform extends PlatformInterface {
  /// Constructs a platform implementation.
  CarPlayPlatform() : super(token: _token);

  static final Object _token = Object();

  static CarPlayPlatform? _instance;

  /// The implementation in use. Set by the platform package's registration.
  static CarPlayPlatform get instance {
    final instance = _instance;
    if (instance == null) {
      throw UnsupportedError(
        'No carplay implementation is registered for this platform. The plugin runs '
        'on Linux.',
      );
    }
    return instance;
  }

  /// Registers [instance] as the implementation.
  static set instance(CarPlayPlatform instance) {
    PlatformInterface.verify(instance, _token);
    _instance = instance;
  }

  /// Starts looking for the adapter, and configures it with [config] once found.
  Future<void> start(CarPlayConfig config);

  /// Says goodbye to the phone and the adapter.
  Future<void> stop();

  /// Releases everything. The implementation is unusable afterwards.
  Future<void> dispose() async {}

  /// Lifecycle and news.
  Stream<CarPlayEvent> get events;

  /// The texture the projection is drawn into, or null before the first start.
  ///
  /// The texture outlives a connection, so an id is no promise of a picture. See
  /// [hasVideo].
  Future<int?> get textureId;

  /// Whether the texture holds a picture from the phone connected now.
  Future<bool> get hasVideo async => await textureId != null;

  /// What the phone is sending, or null while there is no picture.
  Future<CarPlayVideoInfo?> get videoInfo async => null;

  /// The widget the projection is drawn with. A [Texture] unless an implementation has
  /// no native video to show.
  Widget buildProjection(BuildContext context, int textureId) =>
      Texture(textureId: textureId);

  /// The view's size in physical pixels. With no size in the config, the frame the
  /// phone is asked for follows this.
  void setViewSize(double width, double height) {}

  /// The size the texture is drawn at, in physical pixels, so a picture shown smaller
  /// than it was sent is scaled down cleanly before Flutter samples it.
  void setDisplaySize(int width, int height) {}

  /// Drives the texture from a generated pattern, for laying out overlays with no
  /// adapter or phone.
  Future<void> startTestPattern() async {}

  /// Stops the test pattern.
  Future<void> stopTestPattern() async {}

  /// Sends touch to the phone, in projected video pixels.
  ///
  /// [pointers] holds every finger that is down, in the order they landed, and
  /// [actionIndex] says which of them this report is about.
  void sendTouch(
    CarPlayTouchAction action,
    List<CarPlayTouchPoint> pointers, {
    int actionIndex = 0,
  }) {}

  /// Sends a key going down or coming up.
  void sendKey(CarPlayKey key, {required bool down}) {}

  /// Presses and releases [key].
  void pressKey(CarPlayKey key) {
    sendKey(key, down: true);
    sendKey(key, down: false);
  }

  /// Asks the phone for a fresh keyframe. The plugin does this by itself when its
  /// decoder needs one; this is for a host app that recreated something.
  void requestKeyFrame() {}

  /// Whether CarPlay is in its dark appearance.
  bool get nightMode => false;

  /// Switches CarPlay between its light and dark appearance, at once.
  void setNightMode(bool night) {}

  /// Volume of [stream], 0 to 1.
  double audioVolume(CarPlayAudioStream stream) => 1.0;

  /// Sets the volume of [stream], 0 to 1. Applied by the head unit, in software.
  void setAudioVolume(CarPlayAudioStream stream, double volume) {}

  /// Whether [stream] is muted.
  bool audioMuted(CarPlayAudioStream stream) => false;

  /// Mutes or unmutes [stream].
  void setAudioMuted(CarPlayAudioStream stream, bool muted) {}

  /// Every audio output the machine has.
  Future<List<CarPlayAudioDevice>> audioDevices() async => const <CarPlayAudioDevice>[];

  /// The output in use, or empty for the default.
  String get audioDevice => '';

  /// Selects an output by [CarPlayAudioDevice.name], or the default for null.
  void setAudioDevice(String? name) {}

  /// Whether audio reaches the speakers at all.
  bool get audioOutputEnabled => true;

  /// Turns the speakers off without touching the session, for an app that mixes the
  /// PCM from [audioBuffers] itself.
  void setAudioOutputEnabled(bool enabled) {}

  /// The audio backend, such as "PulseAudio", or "none".
  String get audioBackend => 'none';

  /// Times [stream] nearly ran the speakers dry.
  int audioUnderruns(CarPlayAudioStream stream) => 0;

  /// Buffers of [stream] dropped because the phone sent faster than they played.
  int audioDropped(CarPlayAudioStream stream) => 0;

  /// How far behind the speakers [stream] is.
  Duration audioLatency(CarPlayAudioStream stream) => Duration.zero;

  /// A copy of the PCM on its way to the speakers. Costs a copy per buffer only while
  /// something listens.
  Stream<CarPlayAudioBuffer> get audioBuffers => const Stream<CarPlayAudioBuffer>.empty();

  /// Whether this machine's microphone is open. It is only while the phone asks for
  /// it: during Siri and calls.
  bool get microphoneActive => false;

  /// Peak of the most recent microphone buffer, 0 to 1.
  double get microphoneLevel => 0.0;

  /// Bytes captured since the session was made.
  int get microphoneBytes => 0;

  /// Every audio input the machine has.
  Future<List<CarPlayAudioDevice>> microphoneDevices() async =>
      const <CarPlayAudioDevice>[];

  /// The input in use, or empty for the default.
  String get microphoneDevice => '';

  /// Selects an input by [CarPlayAudioDevice.name], or the default for null. Applies
  /// the next time the phone asks for the microphone.
  void setMicrophoneDevice(String? name) {}

  /// The capture backend, such as "PulseAudio", or "none".
  String get microphoneBackend => 'none';

  /// The track playing on the phone, every time it changes.
  Stream<CarPlayNowPlaying> get nowPlaying => const Stream<CarPlayNowPlaying>.empty();

  /// Turn by turn guidance, every time it changes.
  Stream<CarPlayNavigation> get navigation => const Stream<CarPlayNavigation>.empty();

  /// Phone calls, every time one changes.
  Stream<CarPlayCall> get calls => const Stream<CarPlayCall>.empty();

  /// The phone connected now, or null each time one goes.
  Stream<CarPlayPhone?> get phone => const Stream<CarPlayPhone?>.empty();

  /// What the adapter says about itself, every time that changes.
  Stream<CarPlayAdapterInfo> get adapter => const Stream<CarPlayAdapterInfo>.empty();

  /// What the phone asks the head unit to do.
  Stream<CarPlayRequest> get requests => const Stream<CarPlayRequest>.empty();

  /// Whether a Carlinkit adapter is on USB right now. Works without a session.
  bool get adapterPresent => false;

  /// Every change to [adapterPresent].
  Stream<bool> get adapterPresentChanges => const Stream<bool>.empty();
}
