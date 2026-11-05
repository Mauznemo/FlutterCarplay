// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin end to end against whatever adapter is on USB: the real CPC200-CCPA, or the
// fake one from dev/fake-adapter.sh. Skipped when there is none, so it is safe to run
// anywhere. dev/scenarios.sh runs it against the fake with each of its quirks.
//
//   cd example && flutter test integration_test/adapter_test.dart -d linux
import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

/// How long a phone may take from Start to its first frame. A wired iPhone through the
/// adapter takes a few seconds; the generous margin is for the adapter's own start.
const _connectTimeout = Duration(seconds: 40);

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('projects, survives stop and start, follows a resize', (tester) async {
    final controller = CarPlayController();
    addTearDown(controller.dispose);
    if (!controller.adapterPresent) {
      markTestSkipped('No Carlinkit adapter on USB, real or fake.');
      return;
    }

    Future<void> view(Size size) => tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          backgroundColor: Colors.black,
          body: Center(
            child: SizedBox.fromSize(
              size: size,
              child: CarPlayView(controller: controller),
            ),
          ),
        ),
      ),
    );

    Future<void> waitFor(String what, bool Function() done) async {
      final deadline = DateTime.now().add(_connectTimeout);
      while (!done()) {
        if (DateTime.now().isAfter(deadline)) {
          fail(
            'Timed out waiting for $what. State ${controller.state}, '
            'last message: ${controller.message}',
          );
        }
        await Future<void>.delayed(const Duration(milliseconds: 100));
        await tester.pump();
      }
    }

    // Logical pixels at whatever ratio the test window has, so the expected frame is
    // computed the way the view computes it.
    Size physical(Size logical) => logical * tester.view.devicePixelRatio;

    const first = Size(800, 480);
    await view(first);
    await controller.start();
    await waitFor('the first picture', () => controller.hasVideo);
    expect(controller.state, CarPlayConnectionState.connected);
    expect(controller.videoInfo!.width, physical(first).width.round() & ~1);
    expect(controller.videoInfo!.height, physical(first).height.round() & ~1);
    expect(find.byType(Texture), findsOneWidget);

    await controller.stop();
    await tester.pump();
    expect(controller.hasVideo, isFalse);
    expect(controller.state, CarPlayConnectionState.idle);

    await controller.start();
    await waitFor('the picture after a restart', () => controller.hasVideo);

    // The phone is asked for the new size once the view holds still, and reconnects.
    const second = Size(640, 400);
    await view(second);
    await waitFor(
      'a picture of the new size',
      () =>
          controller.hasVideo &&
          controller.videoInfo?.width == (physical(second).width.round() & ~1),
    );

    await controller.stop();
  });
}
