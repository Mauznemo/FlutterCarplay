// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay_platform_interface/carplay_platform_interface.dart';
import 'package:flutter/widgets.dart';

import 'carplay_controller.dart';

/// Renders CarPlay's instrument cluster stream: a map with the next turn, which the
/// phone draws for a screen behind the steering wheel.
///
/// Needs [CarPlayConfig.cluster]. Shows [placeholder] until the phone sends the stream,
/// which is after the main picture: the two sides first agree on it with a handshake.
/// There is no touch, the cluster is for looking at.
///
/// Make it the size named in [CarPlayCluster] for a picture drawn one to one; any other
/// size is fitted by [fit].
class CarPlayClusterView extends StatelessWidget {
  /// The session whose cluster stream this shows.
  final CarPlayController controller;

  /// Shown while there is no cluster picture. Nothing when left null.
  final Widget? placeholder;

  /// How the picture is fitted into the space.
  final BoxFit fit;

  /// Creates a cluster view for [controller].
  const CarPlayClusterView({
    super.key,
    required this.controller,
    this.placeholder,
    this.fit = BoxFit.contain,
  });

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) {
        final textureId = controller.clusterTextureId;
        final info = controller.clusterVideoInfo;
        if (textureId == null || !controller.hasClusterVideo || info == null) {
          return placeholder ?? const SizedBox.expand();
        }
        return LayoutBuilder(
          builder: (context, constraints) {
            final ratio =
                MediaQuery.maybeDevicePixelRatioOf(context) ??
                View.of(context).devicePixelRatio;
            final source = Size(info.width.toDouble(), info.height.toDouble());
            final box = constraints.biggest;
            final fitted = applyBoxFit(fit, source, box).destination;
            controller.setClusterDisplaySize(fitted * ratio);
            return ClipRect(
              child: Center(
                child: SizedBox.fromSize(
                  size: fitted,
                  child: CarPlayPlatform.instance.buildClusterProjection(
                    context,
                    textureId,
                  ),
                ),
              ),
            );
          },
        );
      },
    );
  }
}
