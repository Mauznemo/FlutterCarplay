// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:async';

import 'package:carplay_platform_interface/carplay_platform_interface.dart';

/// One touch report as the view sent it.
class SentTouch {
  final CarPlayTouchAction action;
  final List<CarPlayTouchPoint> pointers;
  final int actionIndex;

  const SentTouch(this.action, this.pointers, this.actionIndex);
}

/// A platform that records what it is asked and plays back what a test tells it to.
class FakeCarPlayPlatform extends CarPlayPlatform {
  final events_ = StreamController<CarPlayEvent>.broadcast();
  final nowPlaying_ = StreamController<CarPlayNowPlaying>.broadcast();
  final navigation_ = StreamController<CarPlayNavigation>.broadcast();
  final calls_ = StreamController<CarPlayCall>.broadcast();
  final phone_ = StreamController<CarPlayPhone?>.broadcast();
  final adapter_ = StreamController<CarPlayAdapterInfo>.broadcast();
  final requests_ = StreamController<CarPlayRequest>.broadcast();

  final List<SentTouch> touches = [];
  final List<String> keys = [];
  int? texture;
  bool video = false;
  CarPlayVideoInfo? info;
  double viewWidth = 0;
  double viewHeight = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  int starts = 0;
  bool night = false;

  @override
  Future<void> start(CarPlayConfig config) async {
    starts++;
    texture = 7;
  }

  @override
  Future<void> stop() async {}

  @override
  Stream<CarPlayEvent> get events => events_.stream;

  @override
  Future<int?> get textureId async => texture;

  @override
  Future<bool> get hasVideo async => video;

  @override
  Future<CarPlayVideoInfo?> get videoInfo async => info;

  @override
  void setViewSize(double width, double height) {
    viewWidth = width;
    viewHeight = height;
  }

  @override
  void setDisplaySize(int width, int height) {
    displayWidth = width;
    displayHeight = height;
  }

  @override
  void sendTouch(
    CarPlayTouchAction action,
    List<CarPlayTouchPoint> pointers, {
    int actionIndex = 0,
  }) => touches.add(SentTouch(action, List.of(pointers), actionIndex));

  @override
  void sendKey(CarPlayKey key, {required bool down}) =>
      keys.add('${key.name} ${down ? 'down' : 'up'}');

  @override
  bool get nightMode => night;

  @override
  void setNightMode(bool value) => night = value;

  @override
  Stream<CarPlayNowPlaying> get nowPlaying => nowPlaying_.stream;

  @override
  Stream<CarPlayNavigation> get navigation => navigation_.stream;

  @override
  Stream<CarPlayCall> get calls => calls_.stream;

  @override
  Stream<CarPlayPhone?> get phone => phone_.stream;

  @override
  Stream<CarPlayAdapterInfo> get adapter => adapter_.stream;

  @override
  Stream<CarPlayRequest> get requests => requests_.stream;

  /// A phone projecting at [width] by [height], announced the way the native side does:
  /// an event, after which the controller reads the video state again.
  void project(int width, int height) {
    video = true;
    info = CarPlayVideoInfo(width: width, height: height, decoder: 'VA-API');
    events_.add(const CarPlayEvent(CarPlayConnectionState.connected, 'picture'));
  }
}
