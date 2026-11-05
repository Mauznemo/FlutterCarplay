// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:async';
import 'dart:ui' show PlatformDispatcher, Size;

import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/foundation.dart';

/// Starts, stops and observes a CarPlay head unit session.
///
/// A host app normally creates one of these for the lifetime of the app and hands it to
/// a [CarPlayView]. Everything the phone says is here too, both as the latest value
/// ([nowPlaying], [navigation], [call], [phone], [adapter]) and as a change
/// notification, so a widget that listens to the controller redraws when a track or a
/// turn changes.
class CarPlayController extends ChangeNotifier {
  /// How the head unit describes itself, as the controller was made with.
  final CarPlayConfig config;

  final List<StreamSubscription<void>> _subscriptions = [];
  final _requests = StreamController<CarPlayRequest>.broadcast();
  CarPlayConnectionState _state = CarPlayConnectionState.idle;
  String? _message;
  int? _textureId;
  bool _hasVideo = false;
  CarPlayVideoInfo? _videoInfo;
  int? _clusterTextureId;
  bool _hasClusterVideo = false;
  CarPlayVideoInfo? _clusterVideoInfo;
  Size? _clusterDisplaySize;
  Size? _viewSize;
  Size? _displaySize;
  CarPlayNowPlaying _nowPlaying = CarPlayNowPlaying.empty;
  CarPlayNavigation _navigation = CarPlayNavigation.inactive;
  CarPlayCall _call = CarPlayCall.none;
  CarPlayPhone? _phone;
  CarPlayAdapterInfo _adapter = CarPlayAdapterInfo.none;

  /// Creates a controller. Nothing happens on USB until [start] is called.
  CarPlayController({this.config = const CarPlayConfig()}) {
    _subscriptions.addAll([
      _platform.events.listen(_onEvent),
      _platform.nowPlaying.listen((value) => _set(() => _nowPlaying = value)),
      _platform.navigation.listen((value) => _set(() => _navigation = value)),
      _platform.calls.listen((value) => _set(() => _call = value)),
      _platform.phone.listen((value) {
        _set(() {
          _phone = value;
          if (value == null) {
            // What the phone was doing went with it.
            _nowPlaying = CarPlayNowPlaying.empty;
            _navigation = CarPlayNavigation.inactive;
            _call = CarPlayCall.none;
          }
        });
      }),
      _platform.adapter.listen((value) => _set(() => _adapter = value)),
      _platform.requests.listen(_requests.add),
    ]);
  }

  CarPlayPlatform get _platform => CarPlayPlatform.instance;

  /// Lifecycle and news from the native session.
  Stream<CarPlayEvent> get events => _platform.events;

  /// Where the session is in its lifecycle.
  CarPlayConnectionState get state => _state;

  /// The latest news, or the reason for [CarPlayConnectionState.error]. For a person to
  /// read; nothing should match on its wording.
  String? get message => _message;

  /// Id of the texture carrying the projected video, or null while there is none.
  ///
  /// The texture outlives a connection, so an id is no promise of a picture. Check
  /// [hasVideo] before showing it, as [CarPlayView] does.
  int? get textureId => _textureId;

  /// Whether the phone's picture is on screen, which is later than
  /// [CarPlayConnectionState.connected]: the adapter reports the phone a few seconds
  /// before the first frame. The signal to hide a "connecting" screen on.
  bool get hasVideo => _hasVideo;

  /// The size and decoder of the incoming video, or null while [hasVideo] is false.
  CarPlayVideoInfo? get videoInfo => _videoInfo;

  /// Id of the instrument cluster's texture, or null without [CarPlayConfig.cluster].
  int? get clusterTextureId => _clusterTextureId;

  /// Whether the cluster texture shows the phone's cluster stream. Later than
  /// [hasVideo]: the cluster stream starts after a handshake once the phone projects.
  bool get hasClusterVideo => _hasClusterVideo;

  /// The size of the cluster stream, or null while [hasClusterVideo] is false.
  CarPlayVideoInfo? get clusterVideoInfo => _clusterVideoInfo;

  /// The track playing on the phone. [CarPlayNowPlaying.empty] while nothing is.
  CarPlayNowPlaying get nowPlaying => _nowPlaying;

  /// Turn by turn guidance. [CarPlayNavigation.inactive] while there is none.
  CarPlayNavigation get navigation => _navigation;

  /// The current call. [CarPlayCall.none] while there is none.
  CarPlayCall get call => _call;

  /// The phone connected now, or null.
  CarPlayPhone? get phone => _phone;

  /// What the adapter says about itself.
  CarPlayAdapterInfo get adapter => _adapter;

  /// What the phone asks of the head unit, such as [CarPlayRequest.showHostUi] when the
  /// driver taps the car's icon in CarPlay. The head unit decides what to do about it.
  Stream<CarPlayRequest> get requests => _requests.stream;

  /// Starts looking for the adapter, and configures it once found.
  ///
  /// The frame the phone draws is settled here, from the view's size when the config
  /// names none. So when no [CarPlayView] has been laid out yet, the window's size
  /// stands in for it: no view can be larger than its window. A head unit that knows
  /// where the view will be can say so with [setViewSize] before starting.
  Future<void> start() async {
    if (_viewSize == null) {
      final window = await _windowSize();
      if (_viewSize == null && window != null) {
        setViewSize(window);
      }
    }
    await _platform.start(config);
    await _refreshVideoState();
  }

  /// Says goodbye to the phone and the adapter.
  Future<void> stop() async {
    await _platform.stop();
    _textureId = null;
    _hasVideo = false;
    _videoInfo = null;
    _clusterTextureId = null;
    _hasClusterVideo = false;
    _clusterVideoInfo = null;
    _phone = null;
    _nowPlaying = CarPlayNowPlaying.empty;
    _navigation = CarPlayNavigation.inactive;
    _call = CarPlayCall.none;
    notifyListeners();
  }

  /// Whether a Carlinkit adapter is on USB right now. Works without a session.
  bool get adapterPresent => _platform.adapterPresent;

  /// Every change to [adapterPresent]. The way to start a session when the adapter is
  /// plugged in. Cancel the subscription when done, since it is polled.
  Stream<bool> get adapterPresentChanges => _platform.adapterPresentChanges;

  /// Drives the video path from a generated pattern instead of a phone, for laying out
  /// an overlay before any hardware is involved.
  Future<void> startTestPattern() async {
    if (_viewSize == null) {
      final window = await _windowSize();
      if (window != null) {
        setViewSize(window);
      }
    }
    await _platform.startTestPattern();
    await _refreshVideoState();
  }

  /// Stops the pattern started by [startTestPattern].
  Future<void> stopTestPattern() async {
    await _platform.stopTestPattern();
    await _refreshVideoState();
  }

  /// Tells the head unit the size of the view the projection is shown in, in physical
  /// pixels. [CarPlayView] calls this whenever it is laid out; calling it directly is
  /// for a head unit that shows the texture some other way.
  void setViewSize(Size physicalSize) {
    if (physicalSize == _viewSize) {
      return;
    }
    _viewSize = physicalSize;
    _platform.setViewSize(physicalSize.width, physicalSize.height);
  }

  /// Tells the head unit how many physical pixels the texture is drawn across, so a
  /// projection drawn smaller than the video is shrunk properly rather than looking
  /// grainy. [CarPlayView] calls this whenever it is laid out.
  void setDisplaySize(Size physicalSize) {
    if (physicalSize == _displaySize) {
      return;
    }
    _displaySize = physicalSize;
    _platform.setDisplaySize(physicalSize.width.round(), physicalSize.height.round());
  }

  /// Tells the head unit how many physical pixels the cluster texture is drawn across.
  /// [CarPlayClusterView] calls this whenever it is laid out.
  void setClusterDisplaySize(Size physicalSize) {
    if (physicalSize == _clusterDisplaySize) {
      return;
    }
    _clusterDisplaySize = physicalSize;
    _platform.setClusterDisplaySize(
      physicalSize.width.round(),
      physicalSize.height.round(),
    );
  }

  /// Reports a touch to the phone, in projected video pixels. [CarPlayView] does this
  /// for every pointer over the projection.
  void sendTouch(
    CarPlayTouchAction action,
    List<CarPlayTouchPoint> pointers, {
    int actionIndex = 0,
  }) => _platform.sendTouch(action, pointers, actionIndex: actionIndex);

  /// Presses and releases a key: a steering wheel button, a media key, the rotary
  /// controller. For Siri, which listens for as long as the button is held, use
  /// [sendKey] for each edge instead.
  void pressKey(CarPlayKey key) => _platform.pressKey(key);

  /// Reports one half of a key press.
  void sendKey(CarPlayKey key, {required bool down}) =>
      _platform.sendKey(key, down: down);

  /// The car's position, from the car's own receiver. Sent to the phone while it asks
  /// for one, and only with [CarPlayConfig.gps] set; call it whenever a fix arrives.
  void setLocation(CarPlayLocation location) => _platform.setLocation(location);

  /// Whether CarPlay is in its dark appearance.
  bool get nightMode => _platform.nightMode;

  /// Switches CarPlay's appearance, at once. The car's headlight switch or a light
  /// sensor is the usual source.
  void setNightMode(bool night) {
    _platform.setNightMode(night);
    notifyListeners();
  }

  /// Playback volume of one audio stream, 0.0 to 1.0.
  double volume(CarPlayAudioStream stream) => _platform.audioVolume(stream);

  /// Sets the playback volume of one audio stream. Applied by the head unit.
  void setVolume(CarPlayAudioStream stream, double volume) {
    _platform.setAudioVolume(stream, volume);
    notifyListeners();
  }

  /// Whether one audio stream is muted.
  bool muted(CarPlayAudioStream stream) => _platform.audioMuted(stream);

  /// Mutes or unmutes one audio stream.
  void setMuted(CarPlayAudioStream stream, bool muted) {
    _platform.setAudioMuted(stream, muted);
    notifyListeners();
  }

  /// Every audio output the machine has.
  Future<List<CarPlayAudioDevice>> audioDevices() => _platform.audioDevices();

  /// The output in use, empty for the default.
  String get audioDevice => _platform.audioDevice;

  /// Selects an output, null for the default.
  void setAudioDevice(String? name) {
    _platform.setAudioDevice(name);
    notifyListeners();
  }

  /// Whether audio reaches the speakers.
  bool get audioOutputEnabled => _platform.audioOutputEnabled;

  /// Turns the speakers off without touching the session, for an app that mixes
  /// [audioBuffers] itself.
  void setAudioOutputEnabled(bool enabled) {
    _platform.setAudioOutputEnabled(enabled);
    notifyListeners();
  }

  /// A copy of the PCM on its way to the speakers.
  Stream<CarPlayAudioBuffer> get audioBuffers => _platform.audioBuffers;

  /// The audio backend, such as "PulseAudio".
  String get audioBackend => _platform.audioBackend;

  /// Times [stream] nearly ran the speakers dry.
  int audioUnderruns(CarPlayAudioStream stream) => _platform.audioUnderruns(stream);

  /// Whether the phone has this machine's microphone open: Siri or a call.
  bool get microphoneActive => _platform.microphoneActive;

  /// Peak of the most recent microphone buffer, 0 to 1.
  double get microphoneLevel => _platform.microphoneLevel;

  /// Every audio input the machine has.
  Future<List<CarPlayAudioDevice>> microphoneDevices() => _platform.microphoneDevices();

  /// The input in use, empty for the default.
  String get microphoneDevice => _platform.microphoneDevice;

  /// Selects an input, null for the default. Applies the next time the phone asks for
  /// the microphone.
  void setMicrophoneDevice(String? name) {
    _platform.setMicrophoneDevice(name);
    notifyListeners();
  }

  /// The window's size in physical pixels, once it has one, or null if it still has
  /// none after a second. Returns early if a view reports its own size meanwhile.
  Future<Size?> _windowSize() async {
    for (var waited = 0; waited < 1000; waited += 20) {
      final window = PlatformDispatcher.instance.implicitView?.physicalSize;
      if (window != null && !window.isEmpty) {
        return window;
      }
      if (_viewSize != null) {
        return null;
      }
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    return null;
  }

  Future<void> _refreshVideoState() async {
    final id = await _platform.textureId;
    final live = await _platform.hasVideo;
    final info = await _platform.videoInfo;
    final clusterId = await _platform.clusterTextureId;
    final clusterLive = await _platform.hasClusterVideo;
    final clusterInfo = await _platform.clusterVideoInfo;
    if (id != _textureId ||
        live != _hasVideo ||
        info != _videoInfo ||
        clusterId != _clusterTextureId ||
        clusterLive != _hasClusterVideo ||
        clusterInfo != _clusterVideoInfo) {
      _textureId = id;
      _hasVideo = live;
      _videoInfo = info;
      _clusterTextureId = clusterId;
      _hasClusterVideo = clusterLive;
      _clusterVideoInfo = clusterInfo;
      notifyListeners();
    }
  }

  void _onEvent(CarPlayEvent event) {
    _state = event.state;
    _message = event.message ?? _message;
    if (event.state == CarPlayConnectionState.idle) {
      _message = event.message;
    }
    notifyListeners();
    // The picture and its size arrive on the event the decoder raises once it has a
    // frame, and go on the one it raises when the picture ends.
    _refreshVideoState();
  }

  void _set(void Function() change) {
    change();
    notifyListeners();
  }

  @override
  void dispose() {
    for (final subscription in _subscriptions) {
      subscription.cancel();
    }
    _subscriptions.clear();
    _requests.close();
    _platform.dispose();
    super.dispose();
  }
}
