// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';
import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fake_platform.dart';

void main() {
  late FakeCarPlayPlatform platform;
  late CarPlayController controller;

  setUp(() {
    platform = FakeCarPlayPlatform();
    CarPlayPlatform.instance = platform;
    controller = CarPlayController();
  });

  Future<void> pumpView(WidgetTester tester, Size size) async {
    tester.view.physicalSize = size;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: CarPlayView(controller: controller, placeholder: const Text('waiting')),
      ),
    );
  }

  Future<void> project(WidgetTester tester, int width, int height) async {
    await tester.runAsync(() async {
      await controller.start();
      platform.project(width, height);
      await pumpEventQueue();
    });
    await tester.pump();
  }

  testWidgets('reports its size in physical pixels and shows a placeholder', (
    tester,
  ) async {
    await pumpView(tester, const Size(1280, 632));
    expect(platform.viewWidth, 1280);
    expect(platform.viewHeight, 632);
    expect(find.text('waiting'), findsOneWidget);
    expect(find.byType(Texture), findsNothing);
  });

  testWidgets('a picture of the view size is drawn one to one', (tester) async {
    await pumpView(tester, const Size(1280, 632));
    await project(tester, 1280, 632);
    expect(find.byType(Texture), findsOneWidget);
    expect(platform.displayWidth, 1280);
    expect(platform.displayHeight, 632);

    await tester.tapAt(const Offset(100, 200));
    expect(platform.touches.map((t) => t.action), [
      CarPlayTouchAction.down,
      CarPlayTouchAction.up,
    ]);
    final point = platform.touches.first.pointers.single;
    expect((point.id, point.x, point.y), (0, 100, 200));
  });

  testWidgets('a smaller picture is scaled into the view and touch scaled back', (
    tester,
  ) async {
    await pumpView(tester, const Size(1280, 720));
    await project(tester, 640, 360);
    await tester.tapAt(const Offset(640, 360));
    final point = platform.touches.first.pointers.single;
    expect((point.x, point.y), (320, 180));
  });

  testWidgets('a touch on the letterbox is dropped', (tester) async {
    // A 4:3 picture in a wide view leaves bars at the sides.
    await pumpView(tester, const Size(1600, 600));
    await project(tester, 800, 600);
    await tester.tapAt(const Offset(50, 300));
    expect(platform.touches, isEmpty);
    await tester.tapAt(const Offset(800, 300));
    expect(platform.touches, isNotEmpty);
  });

  testWidgets('fingers get small reused slots and a sixth is ignored', (tester) async {
    await pumpView(tester, const Size(1280, 720));
    await project(tester, 1280, 720);
    final gestures = <TestGesture>[];
    for (var i = 0; i < 6; i++) {
      gestures.add(
        await tester.startGesture(Offset(100.0 + i * 100, 100), pointer: 40 + i),
      );
    }
    final downs = platform.touches.where(
      (t) =>
          t.action == CarPlayTouchAction.down ||
          t.action == CarPlayTouchAction.pointerDown,
    );
    expect(downs.length, 5);
    expect(downs.last.pointers.map((p) => p.id), [0, 1, 2, 3, 4]);
    expect(downs.last.actionIndex, 4);
    for (final gesture in gestures) {
      await gesture.up();
    }
    expect(platform.touches.last.action, CarPlayTouchAction.up);
  });
}
