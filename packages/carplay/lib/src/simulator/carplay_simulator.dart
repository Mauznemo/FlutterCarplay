// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:async';

import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/widgets.dart';

import 'frame_size.dart';
import 'simulated_phone.dart';
import 'simulated_screen.dart';

/// A head unit with a pretend adapter and iPhone, for developing a host app on a machine
/// that cannot run the real one.
///
/// The real head unit only runs on Linux. Most host apps are written on macOS or Windows,
/// so this is what the `carplay` package registers there: pure Dart, no native code, no
/// USB, no audio. Everything else about the app, the controller, the view, the overlays
/// and every metadata stream, runs as it will in the car.
///
/// * [start] walks through `searching` and `waitingForPhone` to `connected`, as plugging
///   in the adapter and then an iPhone would, and the picture follows a moment later, as
///   a real first frame does. So an app that hides its connecting screen too early finds
///   out here.
/// * The projection is a drawn stand in for CarPlay: a dock on the driver's side, a home
///   screen, a map, a player and a phone app, laid out for the frame a real head unit
///   would ask for, and labelled as simulated.
/// * Touches reach it through [sendTouch] in projected video pixels, exactly as they reach
///   the adapter, so the view's touch mapping is exercised rather than bypassed. Fingers
///   are drawn, which makes a mis-mapped touch obvious.
/// * Keys work: media keys drive the player, [CarPlayKey.siri] held opens the microphone
///   (as [microphoneActive] says), [CarPlayKey.home] goes home.
/// * Going home on the map sends turn by turn guidance on [navigation], the player sends
///   [nowPlaying] with cover art, the phone app sends [calls], and the car's icon on the
///   home screen sends [CarPlayRequest.showHostUi] on [requests].
/// * Night mode switches the screen to its dark colours.
///
/// Registered for macOS and Windows automatically. A host app that wants it on Linux too,
/// to work without an adapter to hand, calls [registerWith] before creating its
/// controller.
class CarPlaySimulator extends CarPlayPlatform {
  /// How long [start] spends looking for the pretend adapter.
  final Duration searchTime;

  /// How long the pretend iPhone takes to connect once the adapter is there.
  final Duration connectTime;

  /// How long after connecting the picture appears.
  final Duration firstFrameTime;

  final _events = StreamController<CarPlayEvent>.broadcast();
  final _nowPlaying = StreamController<CarPlayNowPlaying>.broadcast();
  final _navigation = StreamController<CarPlayNavigation>.broadcast();
  final _calls = StreamController<CarPlayCall>.broadcast();
  final _phoneInfo = StreamController<CarPlayPhone?>.broadcast();
  final _adapter = StreamController<CarPlayAdapterInfo>.broadcast();
  final _requests = StreamController<CarPlayRequest>.broadcast();
  late final SimulatedPhone _phone = SimulatedPhone(
    onNowPlaying: (value) => _add(_nowPlaying, value),
    onNavigation: (value) => _add(_navigation, value),
    onCall: (value) => _add(_calls, value),
    onRequest: (value) => _add(_requests, value),
    onMicrophone: () =>
        _emit(_state, _phone.listening ? 'Siri is listening.' : 'Siri stopped.'),
  );

  CarPlayConfig _config = const CarPlayConfig();
  CarPlayConnectionState _state = CarPlayConnectionState.idle;
  final List<Timer> _pending = [];
  Timer? _resize;
  bool _streaming = false;
  bool _testPattern = false;
  Size _view = Size.zero;
  Size? _frame;

  final Map<CarPlayAudioStream, double> _volumes = {};
  final Map<CarPlayAudioStream, bool> _muted = {};
  String _audioDevice = '';
  bool _audioOutputEnabled = true;
  String _microphoneDevice = '';

  /// Creates a simulator. The delays make the connection sequence visible; a test can
  /// set them to zero.
  CarPlaySimulator({
    this.searchTime = const Duration(milliseconds: 600),
    this.connectTime = const Duration(milliseconds: 1200),
    this.firstFrameTime = const Duration(milliseconds: 600),
  });

  /// Called by the Flutter tooling through `dartPluginClass` on macOS and Windows.
  static void registerWith() {
    CarPlayPlatform.instance = CarPlaySimulator();
  }

  /// The simulator in use, or null when a real head unit is.
  static CarPlaySimulator? get current {
    try {
      final instance = CarPlayPlatform.instance;
      return instance is CarPlaySimulator ? instance : null;
    } on UnsupportedError {
      return null;
    }
  }

  /// Starts guidance on the pretend phone, as tapping Go on its map does.
  void startRoute() => _phone.startRoute();

  /// Ends guidance.
  void stopRoute() => _phone.stopRoute();

  /// Makes the pretend phone ring.
  void simulateIncomingCall() => _phone.simulateIncomingCall();

  /// Ends or declines the pretend call.
  void hangUp() => _phone.hangUp();

  @override
  Stream<CarPlayEvent> get events => _events.stream;

  @override
  Future<void> start(CarPlayConfig config) async {
    if (_state != CarPlayConnectionState.idle && _state != CarPlayConnectionState.error) {
      return;
    }
    _config = config;
    _frame = _frameFor(_view);
    _emit(
      CarPlayConnectionState.searching,
      'Looking for a Carlinkit adapter (simulated).',
    );
    _after(searchTime, () {
      _emit(CarPlayConnectionState.waitingForPhone, 'Adapter found (simulated).');
      _add(
        _adapter,
        const CarPlayAdapterInfo(
          open: true,
          usb: 'simulated',
          softwareVersion: 'simulator',
          bluetoothName: 'Simulated',
          wifiName: 'Simulated',
          model: 'simulator',
        ),
      );
      _after(connectTime, _connectPhone);
    });
  }

  void _connectPhone() {
    _phone
      ..night = nightMode
      ..connect();
    _add(
      _phoneInfo,
      const CarPlayPhone(
        type: CarPlayPhoneType.carPlay,
        wireless: false,
        model: 'Simulator',
      ),
    );
    _emit(CarPlayConnectionState.connected, 'An iPhone connected over USB (simulated).');
    _after(firstFrameTime, () {
      _streaming = true;
      _emit(CarPlayConnectionState.connected, "The phone's picture is on screen.");
    });
  }

  @override
  Future<void> stop() async {
    _cancelPending();
    _resize?.cancel();
    final hadPhone = _streaming || _state == CarPlayConnectionState.connected;
    _streaming = false;
    _phone.disconnect();
    if (hadPhone) {
      _add(_phoneInfo, null);
    }
    _emit(CarPlayConnectionState.idle);
  }

  @override
  Future<void> dispose() async {
    _cancelPending();
    _resize?.cancel();
    _phone.dispose();
    for (final controller in <StreamController<Object?>>[
      _events,
      _nowPlaying,
      _navigation,
      _calls,
      _phoneInfo,
      _adapter,
      _requests,
    ]) {
      await controller.close();
    }
  }

  @override
  Future<int?> get textureId async =>
      _streaming || _testPattern || _frame != null ? 0 : null;

  @override
  Future<bool> get hasVideo async => _streaming || _testPattern;

  @override
  Future<int?> get clusterTextureId async =>
      _config.cluster != null && (_streaming || _frame != null) ? 1 : null;

  @override
  Future<bool> get hasClusterVideo async => _streaming && _config.cluster != null;

  @override
  Future<CarPlayVideoInfo?> get clusterVideoInfo async {
    final cluster = _config.cluster;
    if (!_streaming || cluster == null) {
      return null;
    }
    return CarPlayVideoInfo(
      width: cluster.width - cluster.width % 2,
      height: cluster.height - cluster.height % 2,
      decoder: 'simulated',
    );
  }

  @override
  Widget buildClusterProjection(BuildContext context, int textureId) =>
      SimulatedCluster(phone: _phone);

  @override
  Future<CarPlayVideoInfo?> get videoInfo async {
    final frame = _frame ?? _frameFor(_view);
    if (!_streaming && !_testPattern) {
      return null;
    }
    return CarPlayVideoInfo(
      width: frame.width.round(),
      height: frame.height.round(),
      decoder: 'simulated',
    );
  }

  @override
  Widget buildProjection(BuildContext context, int textureId) {
    if (_testPattern && !_streaming) {
      return const _Pattern();
    }
    return SimulatedScreen(
      phone: _phone,
      driveSide: _config.driveSide,
      safeArea: _config.safeArea,
    );
  }

  Size _frameFor(Size view) {
    final width = _config.width;
    final height = _config.height;
    if (width != null && height != null) {
      return Size((width - width % 2).toDouble(), (height - height % 2).toDouble());
    }
    return simulatedFrameForView(view);
  }

  @override
  void setViewSize(double width, double height) {
    _view = Size(width, height);
    if (!_config.followViewResize) {
      return;
    }
    final frame = _frameFor(_view);
    if (frame == _frame) {
      return;
    }
    // Not projecting yet: the pretend phone has not been asked for a size, so the new
    // one simply applies, as the real head unit's next Open would.
    if (!_streaming) {
      if (_frame != null) {
        _frame = frame;
      }
      return;
    }
    // As the real head unit: wait for the view to hold still, then the phone
    // reconnects into the new size, picture gone for a moment.
    _resize?.cancel();
    _resize = Timer(const Duration(seconds: 1), () {
      _frame = frame;
      _streaming = false;
      _emit(
        CarPlayConnectionState.connected,
        'The view changed size (simulated reconnect).',
      );
      _after(firstFrameTime, () {
        _streaming = true;
        _emit(CarPlayConnectionState.connected, "The phone's picture is on screen.");
      });
    });
  }

  @override
  Future<void> startTestPattern() async {
    _frame ??= _frameFor(_view);
    _testPattern = true;
    _emit(_state, 'Test pattern started.');
  }

  @override
  Future<void> stopTestPattern() async {
    _testPattern = false;
    _emit(_state, 'Test pattern stopped.');
  }

  @override
  void sendTouch(
    CarPlayTouchAction action,
    List<CarPlayTouchPoint> pointers, {
    int actionIndex = 0,
  }) {
    final frame = _frame;
    final canvas = _phone;
    if (!_streaming || frame == null || pointers.isEmpty) {
      return;
    }
    // Projected video pixels to the screen's own logical coordinates. The screen is laid
    // out across the projection rectangle, which the view sizes from the frame, so the
    // ratio is the view's.
    final scale = _logicalScale(frame);
    canvas.touch(action, {
      for (final point in pointers)
        point.id: Offset(point.x * scale.width, point.y * scale.height),
    }, pointers[actionIndex.clamp(0, pointers.length - 1)].id);
  }

  /// Logical pixels of the drawn screen per video pixel. The view draws the frame across
  /// a rectangle of the view's logical size, so this is that size over the frame's.
  Size _logicalScale(Size frame) {
    final view = _view;
    final ratio = _devicePixelRatio;
    if (view.isEmpty || frame.isEmpty) {
      return const Size(1, 1);
    }
    // The projection is fitted into the view keeping its shape, as BoxFit.contain.
    final fit = [
      view.width / frame.width,
      view.height / frame.height,
    ].reduce((a, b) => a < b ? a : b);
    return Size(fit / ratio, fit / ratio);
  }

  double get _devicePixelRatio {
    final views = WidgetsBinding.instance.platformDispatcher.views;
    return views.isEmpty ? 1.0 : views.first.devicePixelRatio;
  }

  @override
  void sendKey(CarPlayKey key, {required bool down}) => _phone.key(key, down: down);

  CarPlayLocation? _location;

  /// The last position [setLocation] was given, which the pretend phone does nothing
  /// with but a test can read back.
  CarPlayLocation? get location => _location;

  @override
  void setLocation(CarPlayLocation location) => _location = location;

  bool _night = false;

  @override
  bool get nightMode => _night;

  @override
  void setNightMode(bool night) {
    _night = night;
    _phone.night = night;
  }

  @override
  double audioVolume(CarPlayAudioStream stream) => _volumes[stream] ?? 1.0;

  @override
  void setAudioVolume(CarPlayAudioStream stream, double volume) =>
      _volumes[stream] = volume.clamp(0.0, 1.0);

  @override
  bool audioMuted(CarPlayAudioStream stream) => _muted[stream] ?? false;

  @override
  void setAudioMuted(CarPlayAudioStream stream, bool muted) => _muted[stream] = muted;

  @override
  String get audioDevice => _audioDevice;

  @override
  void setAudioDevice(String? name) => _audioDevice = name ?? '';

  @override
  bool get audioOutputEnabled => _audioOutputEnabled;

  @override
  void setAudioOutputEnabled(bool enabled) => _audioOutputEnabled = enabled;

  @override
  String get audioBackend => 'simulated';

  @override
  bool get microphoneActive => _phone.listening;

  @override
  String get microphoneDevice => _microphoneDevice;

  @override
  void setMicrophoneDevice(String? name) => _microphoneDevice = name ?? '';

  @override
  Stream<CarPlayNowPlaying> get nowPlaying => _nowPlaying.stream;

  @override
  Stream<CarPlayNavigation> get navigation => _navigation.stream;

  @override
  Stream<CarPlayCall> get calls => _calls.stream;

  @override
  Stream<CarPlayPhone?> get phone => _phoneInfo.stream;

  @override
  Stream<CarPlayAdapterInfo> get adapter => _adapter.stream;

  @override
  Stream<CarPlayRequest> get requests => _requests.stream;

  /// Always true: the pretend adapter is plugged in throughout.
  @override
  bool get adapterPresent => true;

  void _after(Duration delay, void Function() action) {
    late final Timer timer;
    timer = Timer(delay, () {
      _pending.remove(timer);
      action();
    });
    _pending.add(timer);
  }

  void _cancelPending() {
    for (final timer in _pending) {
      timer.cancel();
    }
    _pending.clear();
  }

  void _emit(CarPlayConnectionState state, [String? message]) {
    _state = state;
    _add(_events, CarPlayEvent(state, message));
  }

  static void _add<T>(StreamController<T> controller, T value) {
    if (!controller.isClosed) {
      controller.add(value);
    }
  }
}

/// The simulator's test pattern: a checkerboard with marked corners, like the native one.
class _Pattern extends StatelessWidget {
  const _Pattern();

  @override
  Widget build(BuildContext context) => const CustomPaint(painter: _PatternPainter());
}

class _PatternPainter extends CustomPainter {
  const _PatternPainter();

  @override
  void paint(Canvas canvas, Size size) {
    const cell = 64.0;
    final dark = Paint()..color = const Color(0xFF231F3A);
    final light = Paint()..color = const Color(0xFF3B3560);
    for (var y = 0.0; y < size.height; y += cell) {
      for (var x = 0.0; x < size.width; x += cell) {
        final odd = ((x / cell).floor() + (y / cell).floor()).isOdd;
        canvas.drawRect(Rect.fromLTWH(x, y, cell, cell), odd ? light : dark);
      }
    }
    final corner = Paint()..color = const Color(0xFF2EE07A);
    for (final offset in [
      Offset.zero,
      Offset(size.width - 24, 0),
      Offset(0, size.height - 24),
      Offset(size.width - 24, size.height - 24),
    ]) {
      canvas.drawRect(offset & const Size(24, 24), corner);
    }
  }

  @override
  bool shouldRepaint(_PatternPainter oldDelegate) => false;
}
