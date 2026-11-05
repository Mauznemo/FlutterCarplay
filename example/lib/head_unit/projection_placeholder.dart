// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';

/// What the view shows while there is no picture: what the head unit is waiting for.
class ProjectionPlaceholder extends StatelessWidget {
  final CarPlayController controller;

  const ProjectionPlaceholder({super.key, required this.controller});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final (icon, text) = switch (controller.state) {
      CarPlayConnectionState.idle => (Icons.power_settings_new, 'Press Start'),
      CarPlayConnectionState.searching => (Icons.usb, 'Plug in the Carlinkit adapter'),
      CarPlayConnectionState.waitingForPhone => (
        Icons.phone_iphone,
        'Connect an iPhone to the adapter, or wait for it to join wirelessly',
      ),
      CarPlayConnectionState.connected => (Icons.hourglass_top, 'Starting CarPlay'),
      CarPlayConnectionState.error => (Icons.error_outline, 'Something went wrong'),
    };
    return ColoredBox(
      color: Colors.black,
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 56, color: theme.colorScheme.primary),
            const SizedBox(height: 16),
            Text(text, style: theme.textTheme.titleMedium, textAlign: TextAlign.center),
            if (controller.message != null) ...[
              const SizedBox(height: 8),
              ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 560),
                child: Text(
                  controller.message!,
                  style: theme.textTheme.bodySmall,
                  textAlign: TextAlign.center,
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
