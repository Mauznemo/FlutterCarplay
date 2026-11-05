// SPDX-License-Identifier: GPL-3.0-or-later
/// Linux implementation of the `carplay` plugin.
///
/// Drives a Carlinkit adapter through the native core in `src/`, bound with
/// `dart:ffi`. The repository's `docs/architecture.md` has how the pieces fit.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';
import 'dart:ui' show PlatformDispatcher;

import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:ffi/ffi.dart';

import 'src/bindings/cp_core_bindings.dart';
import 'src/cp_library.dart';

/// Registers itself as the platform implementation on Linux.
///
/// Everything hot goes over FFI rather than a method channel. The plugin still has a
/// `pluginClass` registration on the native side, but only so it can capture Flutter's
/// texture registrar at startup.
class CarPlayNative extends CarPlayPlatform {
  final _events = StreamController<CarPlayEvent>.broadcast();

  /// Settings that live here as well as in the core, so a host app can set them before
  /// it has ever started a session and have them apply when one exists. Each is written
  /// through to the core whenever there is one, and replayed into each new one.
  final Map<CarPlayAudioStream, double> _volumes = {
    for (final stream in CarPlayAudioStream.values) stream: 1.0,
  };
  final Map<CarPlayAudioStream, bool> _muted = {
    for (final stream in CarPlayAudioStream.values) stream: false,
  };
  String _audioDevice = '';
  bool _audioOutputEnabled = true;
  String _microphoneDevice = '';
  bool? _nightMode;

  /// The view's size, held for the same reason: the view is laid out before there is a
  /// session to tell, and the frame size is settled when the session starts.
  double _viewWidth = 0;
  double _viewHeight = 0;
  int _displayWidth = 0;
  int _displayHeight = 0;

  final _nowPlaying = StreamController<CarPlayNowPlaying>.broadcast();
  final _navigation = StreamController<CarPlayNavigation>.broadcast();
  final _calls = StreamController<CarPlayCall>.broadcast();
  final _phone = StreamController<CarPlayPhone?>.broadcast();
  final _adapter = StreamController<CarPlayAdapterInfo>.broadcast();
  final _requests = StreamController<CarPlayRequest>.broadcast();
  CarPlayNowPlaying _lastNowPlaying = CarPlayNowPlaying.empty;
  CarPlayNavigation _lastNavigation = CarPlayNavigation.inactive;

  Pointer<CpSession> _session = nullptr;
  NativeCallable<Void Function(Int32, Pointer<Char>)>? _eventCallback;
  NativeCallable<Void Function(Int32, Pointer<Char>)>? _metadataCallback;
  NativeCallable<Void Function(Int32, Pointer<Uint8>, Int32, Int32, Int32)>?
  _audioCallback;
  StreamController<CarPlayAudioBuffer>? _audioController;

  /// [adapterPresentChanges], polled once a second while anything listens. Asking is
  /// cheap, it reads the kernel's device descriptors and opens nothing.
  late final _adapterPresent = StreamController<bool>.broadcast(
    onListen: _watchUsb,
    onCancel: _unwatchUsb,
  );
  Timer? _usbPoll;
  bool _lastAdapterPresent = false;

  CpCoreBindings get _bindings => CpLibrary.instance.bindings;

  /// Called by the Flutter tooling through `dartPluginClass`.
  static void registerWith() {
    CarPlayPlatform.instance = CarPlayNative();
  }

  @override
  Stream<CarPlayEvent> get events => _events.stream;

  @override
  Future<int?> get textureId async {
    if (_session == nullptr) {
      return null;
    }
    final id = _bindings.cp_session_texture_id(_session);
    return id < 0 ? null : id;
  }

  @override
  Future<bool> get hasVideo async =>
      _session != nullptr && _bindings.cp_session_video_active(_session) != 0;

  @override
  Future<CarPlayVideoInfo?> get videoInfo async {
    if (_session == nullptr) {
      return null;
    }
    final width = _bindings.cp_session_video_width(_session);
    final height = _bindings.cp_session_video_height(_session);
    if (width <= 0 || height <= 0) {
      return null;
    }
    return CarPlayVideoInfo(
      width: width,
      height: height,
      decoder: _takeString(_bindings.cp_session_video_backend(_session)),
    );
  }

  @override
  Future<void> start(CarPlayConfig config) async {
    // A stopped session is restarted, not rebuilt, so the texture id and the audio
    // settings carry over.
    if (_session == nullptr) {
      _createSession(config);
      if (_session == nullptr) {
        return;
      }
    }
    final result = _bindings.cp_session_start(_session);
    if (result != 0) {
      _emit(CarPlayConnectionState.error, 'Could not start the session (code $result).');
    }
  }

  void _createSession(CarPlayConfig config) {
    // Listener callables, not isolate local closures: the core calls these from its own
    // threads, and only a listener is safe to invoke from those.
    _eventCallback ??= NativeCallable<Void Function(Int32, Pointer<Char>)>.listener(
      _onNativeEvent,
    );
    _metadataCallback ??= NativeCallable<Void Function(Int32, Pointer<Char>)>.listener(
      _onNativeMetadata,
    );

    // The safe area is given in logical pixels and the frame is physical. A host app
    // that scales its own canvas should give insets already scaled by its own ratio.
    final ratio = PlatformDispatcher.instance.implicitView?.devicePixelRatio ?? 1.0;
    final native = calloc<CpConfig>();
    final name = config.name.toNativeUtf8();
    final label = (config.carIconLabel ?? '').toNativeUtf8();
    final icon = config.carIcon;
    final iconBytes = icon == null || icon.isEmpty ? nullptr : calloc<Uint8>(icon.length);
    try {
      if (icon != null && iconBytes != nullptr) {
        iconBytes.asTypedList(icon.length).setAll(0, icon);
      }
      native.ref
        // Zero is the core following the view.
        ..width = config.width ?? 0
        ..height = config.height ?? 0
        ..fps = config.fps
        ..dpi = config.dpi
        ..name = name.cast()
        ..night_mode = (_nightMode ?? config.nightMode) ? 1 : 0
        ..right_hand_drive = config.driveSide == CarPlayDriveSide.right ? 1 : 0
        ..wifi_band = config.wifiBand == CarPlayWifiBand.fiveGhz ? 1 : 0
        ..media_delay_ms = config.mediaDelay.inMilliseconds
        ..microphone = config.microphone.index
        ..audio_over_bluetooth = config.audioOverBluetooth ? 1 : 0
        ..wireless = config.wireless ? 1 : 0
        ..fast_charge = config.fastCharge ? 1 : 0
        // Now playing and vehicle status always, route guidance when asked for.
        ..dashboard_info = config.navigationMetadata ? 7 : 3
        ..oem_icon_png = iconBytes
        ..oem_icon_png_size = icon?.length ?? 0
        ..oem_icon_label = label.cast()
        ..follow_view_resize = config.followViewResize ? 1 : 0
        ..safe_left = (config.safeArea.left * ratio).round()
        ..safe_top = (config.safeArea.top * ratio).round()
        ..safe_right = (config.safeArea.right * ratio).round()
        ..safe_bottom = (config.safeArea.bottom * ratio).round()
        ..gps = config.gps ? 1 : 0;
      _nightMode ??= config.nightMode;

      _session = _bindings.cp_session_create(native, _eventCallback!.nativeFunction);
      if (_session == nullptr) {
        _emit(CarPlayConnectionState.error, 'Could not create a head unit session.');
        return;
      }
      _bindings.cp_session_set_view_size(_session, _viewWidth, _viewHeight);
      _bindings.cp_session_set_display_size(_session, _displayWidth, _displayHeight);
      _bindings.cp_session_set_metadata_callback(
        _session,
        _metadataCallback!.nativeFunction,
      );
      _applyAudioSettings();
      if (_audioController?.hasListener ?? false) {
        _armAudioTap();
      }
    } finally {
      // The core copies everything during cp_session_create.
      calloc
        ..free(native)
        ..free(name)
        ..free(label);
      if (iconBytes != nullptr) {
        calloc.free(iconBytes);
      }
    }
  }

  void _applyAudioSettings() {
    for (final stream in CarPlayAudioStream.values) {
      _bindings.cp_session_set_audio_volume(_session, stream.index, _volumes[stream]!);
      _bindings.cp_session_set_audio_muted(
        _session,
        stream.index,
        _muted[stream]! ? 1 : 0,
      );
    }
    _withString(
      _audioDevice,
      (device) => _bindings.cp_session_set_audio_device(_session, device),
    );
    _bindings.cp_session_set_audio_output_enabled(_session, _audioOutputEnabled ? 1 : 0);
    _withString(
      _microphoneDevice,
      (device) => _bindings.cp_session_set_microphone_device(_session, device),
    );
  }

  @override
  Future<void> stop() async {
    if (_session != nullptr) {
      _bindings.cp_session_stop(_session);
    }
    // What the phone was playing is not true any more.
    _lastNowPlaying = CarPlayNowPlaying.empty;
    _lastNavigation = CarPlayNavigation.inactive;
    _emit(CarPlayConnectionState.idle);
  }

  @override
  Future<void> dispose() async {
    if (_session != nullptr) {
      _bindings.cp_session_destroy(_session);
      _session = nullptr;
    }
    // Only after the session is gone: destroying it closes the event bus and clears the
    // taps, so no further calls can arrive at the callables being closed here.
    _eventCallback?.close();
    _eventCallback = null;
    _metadataCallback?.close();
    _metadataCallback = null;
    _audioCallback?.close();
    _audioCallback = null;
    await _audioController?.close();
    _audioController = null;
    _unwatchUsb();
    for (final controller in <StreamController<Object?>>[
      _nowPlaying,
      _navigation,
      _calls,
      _phone,
      _adapter,
      _requests,
      _adapterPresent,
      _events,
    ]) {
      await controller.close();
    }
  }

  @override
  void setViewSize(double width, double height) {
    _viewWidth = width;
    _viewHeight = height;
    if (_session != nullptr) {
      _bindings.cp_session_set_view_size(_session, width, height);
    }
  }

  @override
  void setDisplaySize(int width, int height) {
    _displayWidth = width;
    _displayHeight = height;
    if (_session != nullptr) {
      _bindings.cp_session_set_display_size(_session, width, height);
    }
  }

  @override
  Future<void> startTestPattern() async {
    if (_session == nullptr) {
      _createSession(const CarPlayConfig());
    }
    if (_session != nullptr) {
      _bindings.cp_session_start_test_pattern(_session);
    }
  }

  @override
  Future<void> stopTestPattern() async {
    if (_session != nullptr) {
      _bindings.cp_session_stop_test_pattern(_session);
    }
  }

  @override
  void sendTouch(
    CarPlayTouchAction action,
    List<CarPlayTouchPoint> pointers, {
    int actionIndex = 0,
  }) {
    if (_session == nullptr || pointers.isEmpty) {
      return;
    }
    // Allocated per call rather than kept: a gesture is at most a few hundred calls a
    // second, and calloc costs far less than the USB write that follows.
    final points = calloc<CpTouchPoint>(pointers.length);
    try {
      for (var i = 0; i < pointers.length; i++) {
        points[i]
          ..id = pointers[i].id
          ..x = pointers[i].x
          ..y = pointers[i].y;
      }
      _bindings.cp_session_send_touch(
        _session,
        action.code,
        actionIndex,
        points,
        pointers.length,
      );
    } finally {
      calloc.free(points);
    }
  }

  @override
  void sendKey(CarPlayKey key, {required bool down}) {
    if (_session == nullptr) {
      return;
    }
    final code = down ? key.code : key.releaseCode;
    if (code != null) {
      _bindings.cp_session_send_command(_session, code);
    }
  }

  @override
  void requestKeyFrame() {
    if (_session != nullptr) {
      _bindings.cp_session_request_key_frame(_session);
    }
  }

  @override
  void setLocation(CarPlayLocation location) {
    if (_session == nullptr) {
      return;
    }
    _bindings.cp_session_set_location(
      _session,
      location.latitude,
      location.longitude,
      location.altitude ?? double.nan,
      location.speed ?? double.nan,
      location.heading ?? double.nan,
      location.accuracy ?? double.nan,
      location.time?.millisecondsSinceEpoch ?? 0,
    );
  }

  @override
  bool get nightMode => _nightMode ?? false;

  @override
  void setNightMode(bool night) {
    _nightMode = night;
    if (_session != nullptr) {
      _bindings.cp_session_set_night_mode(_session, night ? 1 : 0);
    }
  }

  @override
  double audioVolume(CarPlayAudioStream stream) => _volumes[stream]!;

  @override
  void setAudioVolume(CarPlayAudioStream stream, double volume) {
    _volumes[stream] = volume.clamp(0.0, 1.0);
    if (_session != nullptr) {
      _bindings.cp_session_set_audio_volume(_session, stream.index, _volumes[stream]!);
    }
  }

  @override
  bool audioMuted(CarPlayAudioStream stream) => _muted[stream]!;

  @override
  void setAudioMuted(CarPlayAudioStream stream, bool muted) {
    _muted[stream] = muted;
    if (_session != nullptr) {
      _bindings.cp_session_set_audio_muted(_session, stream.index, muted ? 1 : 0);
    }
  }

  @override
  Future<List<CarPlayAudioDevice>> audioDevices() async =>
      _parseDevices(_takeString(_bindings.cp_audio_devices()));

  @override
  String get audioDevice => _audioDevice;

  @override
  void setAudioDevice(String? name) {
    _audioDevice = name ?? '';
    if (_session != nullptr) {
      _withString(
        _audioDevice,
        (device) => _bindings.cp_session_set_audio_device(_session, device),
      );
    }
  }

  @override
  bool get audioOutputEnabled => _audioOutputEnabled;

  @override
  void setAudioOutputEnabled(bool enabled) {
    _audioOutputEnabled = enabled;
    if (_session != nullptr) {
      _bindings.cp_session_set_audio_output_enabled(_session, enabled ? 1 : 0);
    }
  }

  @override
  String get audioBackend => _session == nullptr
      ? 'none'
      : _takeString(_bindings.cp_session_audio_backend(_session));

  @override
  int audioUnderruns(CarPlayAudioStream stream) => _session == nullptr
      ? 0
      : _bindings.cp_session_audio_underruns(_session, stream.index);

  @override
  int audioDropped(CarPlayAudioStream stream) => _session == nullptr
      ? 0
      : _bindings.cp_session_audio_dropped(_session, stream.index);

  @override
  Duration audioLatency(CarPlayAudioStream stream) => _session == nullptr
      ? Duration.zero
      : Duration(
          microseconds: _bindings.cp_session_audio_latency(_session, stream.index),
        );

  @override
  Stream<CarPlayAudioBuffer> get audioBuffers {
    // Armed only while something listens: every buffer is a copy across the boundary.
    _audioController ??= StreamController<CarPlayAudioBuffer>.broadcast(
      onListen: _armAudioTap,
      onCancel: _disarmAudioTap,
    );
    return _audioController!.stream;
  }

  void _armAudioTap() {
    if (_session == nullptr) {
      return;
    }
    _audioCallback ??=
        NativeCallable<
          Void Function(Int32, Pointer<Uint8>, Int32, Int32, Int32)
        >.listener(_onNativeAudio);
    _bindings.cp_session_set_audio_callback(_session, _audioCallback!.nativeFunction);
  }

  void _disarmAudioTap() {
    if (_session != nullptr) {
      _bindings.cp_session_set_audio_callback(_session, nullptr);
    }
  }

  void _onNativeAudio(int stream, Pointer<Uint8> data, int size, int rate, int channels) {
    if (data == nullptr) {
      return;
    }
    final samples = Uint8List.fromList(data.asTypedList(size));
    _bindings.cp_audio_buffer_free(data);
    final controller = _audioController;
    if (controller == null || controller.isClosed || stream < 0) {
      return;
    }
    if (stream >= CarPlayAudioStream.values.length) {
      return;
    }
    controller.add(
      CarPlayAudioBuffer(
        stream: CarPlayAudioStream.values[stream],
        samples: samples,
        sampleRate: rate,
        channels: channels,
      ),
    );
  }

  @override
  bool get microphoneActive =>
      _session != nullptr && _bindings.cp_session_microphone_active(_session) != 0;

  @override
  double get microphoneLevel =>
      _session == nullptr ? 0.0 : _bindings.cp_session_microphone_level(_session);

  @override
  int get microphoneBytes =>
      _session == nullptr ? 0 : _bindings.cp_session_microphone_bytes(_session);

  @override
  Future<List<CarPlayAudioDevice>> microphoneDevices() async =>
      _parseDevices(_takeString(_bindings.cp_microphone_devices()));

  @override
  String get microphoneDevice => _microphoneDevice;

  @override
  void setMicrophoneDevice(String? name) {
    _microphoneDevice = name ?? '';
    if (_session != nullptr) {
      _withString(
        _microphoneDevice,
        (device) => _bindings.cp_session_set_microphone_device(_session, device),
      );
    }
  }

  @override
  String get microphoneBackend => _session == nullptr
      ? 'none'
      : _takeString(_bindings.cp_session_microphone_backend(_session));

  @override
  Stream<CarPlayNowPlaying> get nowPlaying => _nowPlaying.stream;

  @override
  Stream<CarPlayNavigation> get navigation => _navigation.stream;

  @override
  Stream<CarPlayCall> get calls => _calls.stream;

  @override
  Stream<CarPlayPhone?> get phone => _phone.stream;

  @override
  Stream<CarPlayAdapterInfo> get adapter => _adapter.stream;

  @override
  Stream<CarPlayRequest> get requests => _requests.stream;

  void _onNativeMetadata(int kind, Pointer<Char> json) {
    if (json == nullptr) {
      return;
    }
    final text = json.cast<Utf8>().toDartString();
    // Back to the core's heap before anything else can throw.
    _bindings.cp_string_free(json);
    if (kind < 0 || kind >= CarPlayMetadata.values.length) {
      return;
    }
    // The phone's own JSON comes through untouched, so a failure to read it is a phone
    // saying something odd rather than a bug here. Dropped, not thrown: a malformed
    // update must not take down the isolate drawing the projection.
    final decoded = decodeJsonObject(text);
    if (decoded == null) {
      return;
    }
    switch (CarPlayMetadata.values[kind]) {
      case CarPlayMetadata.nowPlaying:
        _lastNowPlaying = _lastNowPlaying.merge(decoded);
        _add(_nowPlaying, _lastNowPlaying);
      case CarPlayMetadata.albumArt:
        final image = decoded['image'];
        if (image is String && image.isNotEmpty) {
          try {
            _lastNowPlaying = _lastNowPlaying.withAlbumArt(base64Decode(image));
            _add(_nowPlaying, _lastNowPlaying);
          } on FormatException {
            return;
          }
        }
      case CarPlayMetadata.navigation:
        _lastNavigation = _lastNavigation.merge(decoded);
        _add(_navigation, _lastNavigation);
      case CarPlayMetadata.call:
        _add(_calls, CarPlayCall.fromJson(decoded));
      case CarPlayMetadata.adapter:
        _add(_adapter, CarPlayAdapterInfo.fromJson(decoded));
      case CarPlayMetadata.phone:
        final phone = CarPlayPhone.fromJson(decoded);
        if (phone == null) {
          // The phone went, and what it was playing went with it.
          _lastNowPlaying = CarPlayNowPlaying.empty;
          _lastNavigation = CarPlayNavigation.inactive;
          _add(_nowPlaying, _lastNowPlaying);
          _add(_navigation, _lastNavigation);
        }
        _add(_phone, phone);
      case CarPlayMetadata.request:
        final request = switch (decoded['request']) {
          'hostUi' => CarPlayRequest.showHostUi,
          'hide' => CarPlayRequest.hide,
          'videoFocus' => CarPlayRequest.videoFocus,
          'videoFocusReleased' => CarPlayRequest.videoFocusReleased,
          _ => null,
        };
        if (request != null) {
          _add(_requests, request);
        }
    }
  }

  @override
  bool get adapterPresent => _bindings.cp_adapter_present() != 0;

  @override
  Stream<bool> get adapterPresentChanges => _adapterPresent.stream;

  void _watchUsb() {
    _lastAdapterPresent = adapterPresent;
    _usbPoll ??= Timer.periodic(const Duration(seconds: 1), (_) {
      final present = adapterPresent;
      if (present != _lastAdapterPresent) {
        _lastAdapterPresent = present;
        _add(_adapterPresent, present);
      }
    });
  }

  void _unwatchUsb() {
    _usbPoll?.cancel();
    _usbPoll = null;
  }

  void _onNativeEvent(int state, Pointer<Char> message) {
    String? text;
    if (message != nullptr) {
      text = message.cast<Utf8>().toDartString();
      _bindings.cp_string_free(message);
    }
    if (state < 0 || state >= CarPlayConnectionState.values.length) {
      return;
    }
    _emit(CarPlayConnectionState.values[state], text);
  }

  void _emit(CarPlayConnectionState state, [String? message]) {
    if (!_events.isClosed) {
      _events.add(CarPlayEvent(state, message));
    }
  }

  static void _add<T>(StreamController<T> controller, T value) {
    if (!controller.isClosed) {
      controller.add(value);
    }
  }

  String _takeString(Pointer<Char> native) {
    if (native == nullptr) {
      return '';
    }
    final text = native.cast<Utf8>().toDartString();
    _bindings.cp_string_free(native);
    return text;
  }

  static void _withString(String value, void Function(Pointer<Char>) body) {
    final native = value.toNativeUtf8();
    try {
      body(native.cast());
    } finally {
      calloc.free(native);
    }
  }

  static List<CarPlayAudioDevice> _parseDevices(String listing) => [
    for (final line in const LineSplitter().convert(listing))
      if (line.split('\t') case [final name, final description, final isDefault])
        CarPlayAudioDevice(
          name: name,
          description: description,
          isDefault: isDefault == '1',
        ),
  ];
}
