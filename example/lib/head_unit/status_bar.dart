// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';

/// The head unit's own view of itself, above the projection rather than over it, so
/// the phone lays itself out for the space below.
class StatusBar extends StatelessWidget {
  final CarPlayController controller;

  const StatusBar({super.key, required this.controller});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final video = controller.videoInfo;
    final phone = controller.phone;
    final adapter = controller.adapter;
    final style = theme.textTheme.bodySmall;
    return Container(
      height: 36,
      color: theme.colorScheme.surfaceContainerHigh,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        children: [
          _StateDot(state: controller.state),
          const SizedBox(width: 8),
          Text(controller.state.name, style: style),
          const SizedBox(width: 16),
          if (video != null)
            Text('${video.width}x${video.height} ${video.decoder}', style: style),
          const SizedBox(width: 16),
          if (phone != null)
            Text(
              '${phone.model ?? 'iPhone'} ${phone.wireless ? '(Wi-Fi)' : '(USB)'}',
              style: style,
            ),
          const SizedBox(width: 16),
          if (adapter.softwareVersion != null)
            Text('adapter ${adapter.softwareVersion}', style: style),
          const Spacer(),
          if (controller.microphoneActive)
            Icon(Icons.mic, size: 18, color: theme.colorScheme.error),
          const SizedBox(width: 8),
          Icon(controller.nightMode ? Icons.dark_mode : Icons.light_mode, size: 18),
        ],
      ),
    );
  }
}

class _StateDot extends StatelessWidget {
  final CarPlayConnectionState state;

  const _StateDot({required this.state});

  @override
  Widget build(BuildContext context) {
    final color = switch (state) {
      CarPlayConnectionState.connected => Colors.green,
      CarPlayConnectionState.waitingForPhone => Colors.amber,
      CarPlayConnectionState.searching => Colors.orange,
      CarPlayConnectionState.error => Colors.red,
      CarPlayConnectionState.idle => Colors.grey,
    };
    return Container(
      width: 10,
      height: 10,
      decoration: BoxDecoration(color: color, shape: BoxShape.circle),
    );
  }
}
