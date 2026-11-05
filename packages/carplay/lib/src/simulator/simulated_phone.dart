// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:async';
import 'dart:typed_data';

import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import 'simulated_art.dart';

/// Something on the simulated screen that reacts to a tap.
///
/// Put on the screen inside a [MetaData] widget, which is what lets [SimulatedPhone] hit
/// test a touch that arrived as projected video pixels against the widgets that drew it.
class SimulatedTarget {
  /// What the tap does.
  final VoidCallback onTap;

  /// Creates a target.
  const SimulatedTarget(this.onTap);
}

/// The apps the simulated CarPlay screen has.
enum SimulatedApp {
  /// The home screen's grid of apps.
  home,

  /// A map, with a route to start.
  maps,

  /// The now playing screen.
  music,

  /// Favourites and the call screen.
  phone,
}

/// One step of the simulated route.
class _Leg {
  final CarPlayManeuver maneuver;
  final String road;
  final int metres;

  const _Leg(this.maneuver, this.road, this.metres);
}

/// One track of the simulated library.
class _Track {
  final String title;
  final String artist;
  final String album;
  final Duration duration;
  final List<Color> colors;

  const _Track(this.title, this.artist, this.album, this.duration, this.colors);
}

/// The pretend iPhone: what its screen shows, and what it says on the metadata streams.
///
/// Pure Dart and timer driven. Nothing in here may need the engine to finish a future:
/// a host app's own tests connect this, and `flutter test` never completes one.
class SimulatedPhone extends ChangeNotifier {
  /// Told every time the track or its position changes.
  final void Function(CarPlayNowPlaying nowPlaying) onNowPlaying;

  /// Told every time guidance changes.
  final void Function(CarPlayNavigation navigation) onNavigation;

  /// Told every time a call changes.
  final void Function(CarPlayCall call) onCall;

  /// Told when the screen asks the head unit for something.
  final void Function(CarPlayRequest request) onRequest;

  /// Told when the microphone opens or closes, as Siri listens.
  final VoidCallback onMicrophone;

  static const List<_Track> _library = [
    _Track(
      'Night Drive',
      'The Simulators',
      'Test Signals',
      Duration(minutes: 3, seconds: 41),
      [Color(0xFF1E3A8A), Color(0xFFDB2777)],
    ),
    _Track(
      'Coastal Road',
      'Flutter Ensemble',
      'Widgets',
      Duration(minutes: 4, seconds: 5),
      [Color(0xFF0F766E), Color(0xFFFBBF24)],
    ),
    _Track(
      'Tunnel Vision',
      'Texture Club',
      'Zero Copy',
      Duration(minutes: 2, seconds: 58),
      [Color(0xFF7C2D12), Color(0xFF38BDF8)],
    ),
  ];

  static const List<_Leg> _route = [
    _Leg(CarPlayManeuver.right, 'Main Street', 400),
    _Leg(CarPlayManeuver.roundaboutExit2, 'Harbour Road', 900),
    _Leg(CarPlayManeuver.slightLeft, 'Coast Highway', 1600),
    _Leg(CarPlayManeuver.arrived, 'Home', 300),
  ];

  /// The contacts the phone screen offers.
  static const List<(String, String)> favourites = [
    ('Alice', '+49 30 1234567'),
    ('Bob', '+44 20 7946 0000'),
    ('Workshop', '+1 555 0100'),
  ];

  SimulatedApp _app = SimulatedApp.home;
  bool _night = false;
  bool _connected = false;
  BuildContext? _canvas;
  final Map<int, Offset> _fingers = {};
  Timer? _ticker;

  int _track = 0;
  bool _playing = false;
  Duration _position = Duration.zero;
  Uint8List? _art;

  bool _routeActive = false;
  int _leg = 0;
  double _legMetres = 0;

  CarPlayCall _call = CarPlayCall.none;
  DateTime? _siriUntil;

  /// Creates a pretend phone. It does nothing until [connect].
  SimulatedPhone({
    required this.onNowPlaying,
    required this.onNavigation,
    required this.onCall,
    required this.onRequest,
    required this.onMicrophone,
  });

  /// The app on screen.
  SimulatedApp get app => _app;

  /// Whether CarPlay is in its dark appearance.
  bool get night => _night;
  set night(bool value) {
    if (value != _night) {
      _night = value;
      notifyListeners();
    }
  }

  /// Fingers down, in the screen's logical coordinates, for drawing them.
  Map<int, Offset> get fingers => Map.unmodifiable(_fingers);

  /// The track's title.
  String get title => _library[_track].title;

  /// The artist of the track.
  String get artist => _library[_track].artist;

  /// Whether music is playing.
  bool get playing => _playing;

  /// How far into the track.
  Duration get position => _position;

  /// How long the track is.
  Duration get duration => _library[_track].duration;

  /// The colours the cover is drawn from.
  List<Color> get artColors => _library[_track].colors;

  /// The cover, a PNG, as the metadata stream carries it.
  Uint8List get art => _art ??= simulatedCoverArt(_library[_track].colors);

  /// Whether guidance is running.
  bool get routeActive => _routeActive;

  /// The next maneuver while guidance runs.
  CarPlayManeuver get maneuver => _route[_leg].maneuver;

  /// The road the next maneuver leads onto.
  String get road => _route[_leg].road;

  /// Metres to the next maneuver.
  int get metresToManeuver => _legMetres.round();

  /// The call, [CarPlayCall.none] when there is none.
  CarPlayCall get call => _call;

  /// Whether Siri is listening, which is when the head unit's microphone would be open.
  bool get listening => _siriUntil != null;

  /// The pretend phone plugged in: guidance off, music paused, nothing on the line.
  void connect() {
    _connected = true;
    _app = SimulatedApp.home;
    _ticker ??= Timer.periodic(const Duration(seconds: 1), (_) => _tick());
    _emitNowPlaying();
    notifyListeners();
  }

  /// The pretend phone unplugged.
  void disconnect() {
    _connected = false;
    _ticker?.cancel();
    _ticker = null;
    _fingers.clear();
    _routeActive = false;
    _playing = false;
    _call = CarPlayCall.none;
    if (_siriUntil != null) {
      _siriUntil = null;
      onMicrophone();
    }
    notifyListeners();
  }

  /// Shows [app].
  void open(SimulatedApp app) {
    _app = app;
    notifyListeners();
  }

  /// The screen that touches are hit tested against.
  void attach(BuildContext canvas) => _canvas = canvas;

  /// Forgets [canvas], if it is the one attached.
  void detach(BuildContext canvas) {
    if (identical(_canvas, canvas)) {
      _canvas = null;
    }
  }

  /// A touch, with [fingers] in the screen's logical coordinates and [subject] the
  /// finger the report is about. A tap is a finger lifting on a target.
  void touch(CarPlayTouchAction action, Map<int, Offset> fingers, int subject) {
    if (!_connected) {
      return;
    }
    final lifted =
        action == CarPlayTouchAction.up || action == CarPlayTouchAction.pointerUp;
    final position = fingers[subject];
    _fingers
      ..clear()
      ..addAll(fingers);
    if (lifted) {
      _fingers.remove(subject);
      if (position != null) {
        _targetAt(position)?.onTap();
      }
    }
    notifyListeners();
  }

  SimulatedTarget? _targetAt(Offset position) {
    final box = _canvas?.findRenderObject();
    if (box is! RenderBox || !box.hasSize) {
      return null;
    }
    final result = BoxHitTestResult();
    if (!box.hitTest(result, position: position)) {
      return null;
    }
    for (final entry in result.path) {
      final target = entry.target;
      if (target is RenderMetaData && target.metaData is SimulatedTarget) {
        return target.metaData as SimulatedTarget;
      }
    }
    return null;
  }

  /// A key from the head unit, going down or up.
  void key(CarPlayKey key, {required bool down}) {
    if (!_connected) {
      return;
    }
    if (key == CarPlayKey.siri) {
      down ? startSiri() : stopSiri();
      return;
    }
    if (!down) {
      return;
    }
    switch (key) {
      case CarPlayKey.home:
      case CarPlayKey.back:
        open(SimulatedApp.home);
      case CarPlayKey.play:
        _setPlaying(true);
      case CarPlayKey.pause:
        _setPlaying(false);
      case CarPlayKey.playPause:
        playPause();
      case CarPlayKey.next:
        skip(1);
      case CarPlayKey.previous:
        skip(-1);
      case CarPlayKey.answerCall:
        answer();
      case CarPlayKey.hangUpCall:
        hangUp();
      default:
        break;
    }
  }

  /// The car's own icon in the dock, which asks the head unit for its interface.
  void requestHostUi() => onRequest(CarPlayRequest.showHostUi);

  /// Plays or pauses.
  void playPause() => _setPlaying(!_playing);

  /// Moves [delta] tracks through the library.
  void skip(int delta) {
    _track = (_track + delta) % _library.length;
    _position = Duration.zero;
    _art = null;
    _emitNowPlaying();
    notifyListeners();
  }

  void _setPlaying(bool playing) {
    if (playing == _playing) {
      return;
    }
    _playing = playing;
    _emitNowPlaying();
    notifyListeners();
  }

  void _emitNowPlaying() {
    final track = _library[_track];
    onNowPlaying(
      CarPlayNowPlaying(
        title: track.title,
        artist: track.artist,
        album: track.album,
        app: 'Music',
        duration: track.duration,
        position: _position,
        playing: _playing,
        albumArt: art,
      ),
    );
  }

  /// Starts guidance, as tapping Go on the map does.
  void startRoute() {
    _routeActive = true;
    _leg = 0;
    _legMetres = _route.first.metres.toDouble();
    _emitNavigation();
    notifyListeners();
  }

  /// Ends guidance.
  void stopRoute() {
    if (!_routeActive) {
      return;
    }
    _routeActive = false;
    onNavigation(const CarPlayNavigation(appName: 'Maps'));
    notifyListeners();
  }

  void _emitNavigation() {
    final remaining = _route
        .skip(_leg + 1)
        .fold<double>(_legMetres, (sum, leg) => sum + leg.metres);
    onNavigation(
      CarPlayNavigation(
        active: true,
        maneuver: maneuver,
        distanceToManeuver: metresToManeuver,
        roadName: road,
        destinationName: 'Home',
        distanceToDestination: remaining.round(),
        // 50 km/h.
        timeToDestination: Duration(seconds: (remaining / 13.9).round()),
        appName: 'Maps',
        roundaboutExit: maneuver.roundaboutExit,
      ),
    );
  }

  /// Makes the phone ring.
  void simulateIncomingCall() {
    final (name, number) = favourites.first;
    _setCall(
      CarPlayCall(
        state: CarPlayCallState.ringing,
        incoming: true,
        name: name,
        number: number,
      ),
    );
    _app = SimulatedApp.phone;
  }

  /// Calls one of the [favourites].
  void callFavourite(int index) {
    final (name, number) = favourites[index];
    _setCall(
      CarPlayCall(
        state: CarPlayCallState.dialing,
        incoming: false,
        name: name,
        number: number,
      ),
    );
    // The other end picks up after a moment.
    Timer(const Duration(seconds: 2), () {
      if (_call.state == CarPlayCallState.dialing) {
        answer();
      }
    });
  }

  /// Answers a ringing call, or the other end answering an outgoing one.
  void answer() {
    if (_call.state == CarPlayCallState.ringing ||
        _call.state == CarPlayCallState.dialing) {
      _setCall(
        CarPlayCall(
          state: CarPlayCallState.active,
          incoming: _call.incoming,
          name: _call.name,
          number: _call.number,
        ),
      );
    }
  }

  /// Ends or declines the call.
  void hangUp() {
    if (_call.state != CarPlayCallState.idle) {
      _setCall(CarPlayCall.none);
    }
  }

  void _setCall(CarPlayCall call) {
    _call = call;
    onCall(call);
    notifyListeners();
  }

  /// Siri starts listening, which is when a real phone would open the head unit's
  /// microphone. Stops by itself after a few seconds if [stopSiri] never comes.
  void startSiri() {
    final was = listening;
    _siriUntil = DateTime.now().add(const Duration(seconds: 4));
    if (!was) {
      onMicrophone();
    }
    notifyListeners();
  }

  /// Siri stops listening.
  void stopSiri() {
    if (_siriUntil == null) {
      return;
    }
    _siriUntil = null;
    onMicrophone();
    notifyListeners();
  }

  void _tick() {
    var changed = false;
    if (_playing) {
      _position += const Duration(seconds: 1);
      if (_position >= duration) {
        skip(1);
      } else {
        _emitNowPlaying();
      }
      changed = true;
    }
    if (_routeActive) {
      _legMetres -= 70;
      if (_legMetres <= 0) {
        if (_leg == _route.length - 1) {
          stopRoute();
        } else {
          _leg++;
          _legMetres = _route[_leg].metres.toDouble();
        }
      }
      if (_routeActive) {
        _emitNavigation();
      }
      changed = true;
    }
    final until = _siriUntil;
    if (until != null && DateTime.now().isAfter(until)) {
      stopSiri();
    }
    if (changed) {
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _ticker?.cancel();
    super.dispose();
  }
}
