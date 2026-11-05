// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';
import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fake_platform.dart';

void main() {
  late FakeCarPlayPlatform platform;

  setUp(() {
    platform = FakeCarPlayPlatform();
    CarPlayPlatform.instance = platform;
  });

  test('follows events into state and video', () async {
    final controller = CarPlayController();
    await controller.start();
    expect(controller.textureId, 7);
    expect(controller.hasVideo, isFalse);
    platform.project(1280, 632);
    await pumpEventQueue();
    expect(controller.state, CarPlayConnectionState.connected);
    expect(controller.hasVideo, isTrue);
    expect(controller.videoInfo?.width, 1280);
  });

  test('keeps the latest metadata and forgets it when the phone goes', () async {
    final controller = CarPlayController();
    var notified = 0;
    controller.addListener(() => notified++);
    platform.nowPlaying_.add(const CarPlayNowPlaying(title: 'Song'));
    platform.navigation_.add(const CarPlayNavigation(active: true, roadName: 'Road'));
    platform.phone_.add(
      const CarPlayPhone(type: CarPlayPhoneType.carPlay, wireless: false),
    );
    await pumpEventQueue();
    expect(controller.nowPlaying.title, 'Song');
    expect(controller.navigation.roadName, 'Road');
    expect(controller.phone, isNotNull);
    expect(notified, 3);

    platform.phone_.add(null);
    await pumpEventQueue();
    expect(controller.phone, isNull);
    expect(controller.nowPlaying.title, isNull);
    expect(controller.navigation.active, isFalse);
  });

  test('passes requests on', () async {
    final controller = CarPlayController();
    final requests = <CarPlayRequest>[];
    controller.requests.listen(requests.add);
    platform.requests_.add(CarPlayRequest.showHostUi);
    await pumpEventQueue();
    expect(requests, [CarPlayRequest.showHostUi]);
  });

  test('keys send their press and their release where there is one', () {
    final controller = CarPlayController();
    controller.pressKey(CarPlayKey.home);
    controller.sendKey(CarPlayKey.siri, down: true);
    controller.sendKey(CarPlayKey.siri, down: false);
    expect(platform.keys, ['home down', 'home up', 'siri down', 'siri up']);
    expect(CarPlayKey.siri.releaseCode, 6);
    expect(CarPlayKey.home.releaseCode, isNull);
  });
}
