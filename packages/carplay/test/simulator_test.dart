// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:async';

import 'package:carplay/carplay.dart';
import 'package:carplay/src/simulator/frame_size.dart';
import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late CarPlaySimulator simulator;
  late CarPlayController controller;

  // Made inside each test rather than in setUp: setUp runs outside the test's fake time,
  // and stream events scheduled from there are never delivered by pump().
  Future<void> pumpHeadUnit(WidgetTester tester) async {
    simulator = CarPlaySimulator(
      searchTime: Duration.zero,
      connectTime: Duration.zero,
      firstFrameTime: Duration.zero,
    );
    CarPlayPlatform.instance = simulator;
    controller = CarPlayController();
    tester.view.physicalSize = const Size(1280, 720);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: CarPlayView(controller: controller)),
      ),
    );
  }

  // Everything in fake time: the simulator's delays are plain timers, so pumping fires
  // them. No runAsync, which is also what a host app's own tests should do.
  Future<void> settle(WidgetTester tester) async {
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 20));
    }
  }

  Future<void> connect(WidgetTester tester) async {
    unawaited(controller.start());
    await settle(tester);
  }

  Future<void> stop(WidgetTester tester) async {
    unawaited(controller.stop());
    await settle(tester);
  }

  testWidgets('connects like a phone, at the view size, and stops cleanly', (
    tester,
  ) async {
    await pumpHeadUnit(tester);
    final states = <CarPlayConnectionState>[];
    controller.events.listen((event) => states.add(event.state));
    await connect(tester);
    expect(
      states,
      containsAllInOrder([
        CarPlayConnectionState.searching,
        CarPlayConnectionState.waitingForPhone,
        CarPlayConnectionState.connected,
      ]),
    );
    expect(controller.hasVideo, isTrue);
    expect(
      controller.videoInfo,
      const CarPlayVideoInfo(width: 1280, height: 720, decoder: 'simulated'),
    );
    expect(controller.phone?.type, CarPlayPhoneType.carPlay);
    expect(find.text('SIMULATED CARPLAY'), findsOneWidget);

    await stop(tester);
    expect(controller.hasVideo, isFalse);
    expect(controller.phone, isNull);
  });

  testWidgets('a tap through the view opens an app and plays music', (tester) async {
    await pumpHeadUnit(tester);
    await connect(tester);
    // The home screen's Music icon, tapped where it is drawn: the view maps it to video
    // pixels and the simulator maps it back, as the adapter path would.
    await tester.tap(find.text('Music').last);
    await tester.pump();
    expect(find.text('Night Drive'), findsOneWidget);

    final playing = <CarPlayNowPlaying>[];
    final subscription = simulator.nowPlaying.listen(playing.add);
    await tester.tap(find.text('>'));
    await tester.pump();
    expect(playing.last.playing, isTrue);
    expect(playing.last.albumArt, isNotEmpty);
    expect(controller.nowPlaying.title, 'Night Drive');
    // Not awaited: in fake time the cancel completes on a timer nothing pumps.
    unawaited(subscription.cancel());
    await stop(tester);
  });

  testWidgets('the car icon asks for the host interface', (tester) async {
    await pumpHeadUnit(tester);
    await connect(tester);
    final requests = <CarPlayRequest>[];
    controller.requests.listen(requests.add);
    await tester.tap(find.text('Car'));
    await tester.pump();
    expect(requests, [CarPlayRequest.showHostUi]);
    await stop(tester);
  });

  testWidgets('Siri holds the microphone while the key is down', (tester) async {
    await pumpHeadUnit(tester);
    await connect(tester);
    controller.sendKey(CarPlayKey.siri, down: true);
    expect(controller.microphoneActive, isTrue);
    controller.sendKey(CarPlayKey.siri, down: false);
    expect(controller.microphoneActive, isFalse);
    await stop(tester);
  });

  testWidgets('a route sends guidance, and a call shows up', (tester) async {
    await pumpHeadUnit(tester);
    await connect(tester);
    simulator.startRoute();
    simulator.simulateIncomingCall();
    await tester.pump();
    expect(controller.navigation.active, isTrue);
    expect(controller.navigation.maneuver, CarPlayManeuver.right);
    expect(controller.navigation.roadName, 'Main Street');
    expect(controller.call.state, CarPlayCallState.ringing);
    expect(find.text('Answer'), findsOneWidget);
    await tester.tap(find.text('Answer'));
    await tester.pump();
    expect(controller.call.state, CarPlayCallState.active);
    await stop(tester);
  });

  test('the frame size matches the native rules', () {
    // The same cases as native_test/frame_size_test.cc.
    expect(simulatedFrameForView(const Size(1280, 632)), const Size(1280, 632));
    expect(simulatedFrameForView(const Size(1281.4, 633.6)), const Size(1280, 634));
    expect(simulatedFrameForView(Size.zero), const Size(1280, 720));
    expect(simulatedFrameForView(const Size(3840, 2160)), const Size(2560, 1440));
    expect(simulatedFrameForView(const Size(5120, 1080)), const Size(2560, 540));
    expect(simulatedFrameForView(const Size(320, 180)), const Size(640, 360));
  });
}
