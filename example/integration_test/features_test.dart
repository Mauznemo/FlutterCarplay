// SPDX-License-Identifier: GPL-3.0-or-later
// The optional features against whatever adapter is on USB: a safe area and the car's
// position. The plugin side only; dev/scenarios.sh checks what reached the fake adapter.
// Skipped when there is no adapter.
//
//   cd example && flutter test integration_test/features_test.dart -d linux
import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('projects with a safe area and feeds the car position', (tester) async {
    final controller = CarPlayController(
      config: const CarPlayConfig(
        gps: true,
        cluster: CarPlayCluster(width: 640, height: 240),
        safeArea: EdgeInsets.only(left: 40, bottom: 20),
      ),
    );
    addTearDown(controller.dispose);
    if (!controller.adapterPresent) {
      markTestSkipped('No Carlinkit adapter on USB, real or fake.');
      return;
    }
    await tester.pumpWidget(
      MaterialApp(
        home: Center(
          child: SizedBox(
            width: 800,
            height: 480,
            child: CarPlayView(controller: controller),
          ),
        ),
      ),
    );
    await controller.start();
    final deadline = DateTime.now().add(const Duration(seconds: 40));
    while (!controller.hasVideo) {
      if (DateTime.now().isAfter(deadline)) {
        fail('No picture. Last message: ${controller.message}');
      }
      await Future<void>.delayed(const Duration(milliseconds: 100));
      await tester.pump();
    }
    // The cluster stream follows the main one once the handshake completes.
    final clusterDeadline = DateTime.now().add(const Duration(seconds: 20));
    while (!controller.hasClusterVideo) {
      if (DateTime.now().isAfter(clusterDeadline)) {
        fail('No cluster picture. Last message: ${controller.message}');
      }
      await Future<void>.delayed(const Duration(milliseconds: 100));
      await tester.pump();
    }
    expect(controller.clusterVideoInfo!.width, 640);
    expect(controller.clusterVideoInfo!.height, 240);

    // A few seconds of a car driving east through Berlin, a fix every half second.
    for (var i = 0; i < 8; i++) {
      controller.setLocation(
        CarPlayLocation(
          latitude: 52.52,
          longitude: 13.40 + i * 0.0001,
          speed: 13.9,
          heading: 90,
          accuracy: 5,
        ),
      );
      await Future<void>.delayed(const Duration(milliseconds: 500));
      await tester.pump();
    }
    await controller.stop();
  });
}
