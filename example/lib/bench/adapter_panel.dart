// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';

/// Everything the adapter and the phone have said about themselves, and the three
/// audio streams. What a bug report needs, in one place.
class AdapterPanel extends StatelessWidget {
  final CarPlayController controller;

  const AdapterPanel({super.key, required this.controller});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final adapter = controller.adapter;
    final phone = controller.phone;
    final rows = <(String, String?)>[
      ('Adapter on USB', controller.adapterPresent ? 'yes' : 'no'),
      ('USB', adapter.usb),
      ('Model', adapter.model),
      ('Hardware', adapter.hardwareVersion),
      ('Firmware', adapter.softwareVersion),
      (
        'Bluetooth',
        [adapter.bluetoothName, adapter.bluetoothAddress].whereType<String>().join('  '),
      ),
      ('Wi-Fi', adapter.wifiName),
      ('PIN', adapter.pin),
      ('Link status', adapter.linkStatus),
      (
        'Phone',
        phone == null
            ? null
            : '${phone.model ?? phone.type.name}, ${phone.wireless ? 'wireless' : 'wired'}',
      ),
      ('Phone OS', phone?.osVersion),
      ('Audio', controller.audioBackend),
      ('Message', controller.message),
    ];
    return Container(
      width: 380,
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: theme.colorScheme.surfaceContainerHighest.withValues(alpha: 0.95),
        borderRadius: BorderRadius.circular(12),
      ),
      child: SingleChildScrollView(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Text('Adapter', style: theme.textTheme.titleMedium),
            const SizedBox(height: 8),
            for (final (label, value) in rows)
              if (value != null && value.isNotEmpty)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 2),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      SizedBox(
                        width: 110,
                        child: Text(label, style: theme.textTheme.bodySmall),
                      ),
                      Expanded(
                        child: SelectableText(value, style: theme.textTheme.bodySmall),
                      ),
                    ],
                  ),
                ),
            const Divider(),
            for (final stream in CarPlayAudioStream.values)
              Row(
                children: [
                  SizedBox(width: 90, child: Text(stream.name)),
                  Expanded(
                    child: Slider(
                      value: controller.volume(stream),
                      onChanged: (value) => controller.setVolume(stream, value),
                    ),
                  ),
                  IconButton(
                    onPressed: () =>
                        controller.setMuted(stream, !controller.muted(stream)),
                    icon: Icon(
                      controller.muted(stream) ? Icons.volume_off : Icons.volume_up,
                    ),
                  ),
                ],
              ),
          ],
        ),
      ),
    );
  }
}
