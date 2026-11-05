// SPDX-License-Identifier: GPL-3.0-or-later
import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';

/// The test bench controls along the bottom: the session, the hardware keys a car
/// would have, and the panels.
class Dock extends StatelessWidget {
  final CarPlayController controller;
  final bool patternRunning;
  final bool adapterPanelOpen;
  final VoidCallback onStart;
  final VoidCallback onStop;
  final VoidCallback onTogglePattern;
  final VoidCallback onToggleAdapterPanel;
  final ValueChanged<String> onInput;

  const Dock({
    super.key,
    required this.controller,
    required this.patternRunning,
    required this.adapterPanelOpen,
    required this.onStart,
    required this.onStop,
    required this.onTogglePattern,
    required this.onToggleAdapterPanel,
    required this.onInput,
  });

  void _press(CarPlayKey key) {
    controller.pressKey(key);
    onInput(key.name);
  }

  @override
  Widget build(BuildContext context) {
    final running = controller.state != CarPlayConnectionState.idle;
    return Material(
      color: Theme.of(context).colorScheme.surfaceContainer,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
        child: Wrap(
          spacing: 6,
          runSpacing: 6,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            FilledButton.icon(
              onPressed: running ? onStop : onStart,
              icon: Icon(running ? Icons.stop : Icons.play_arrow),
              label: Text(running ? 'Stop' : 'Start'),
            ),
            OutlinedButton(
              onPressed: onTogglePattern,
              child: Text(patternRunning ? 'Pattern off' : 'Test pattern'),
            ),
            const SizedBox(width: 8),
            _KeyButton(
              icon: Icons.home,
              tooltip: 'Home',
              onPressed: () => _press(CarPlayKey.home),
            ),
            _KeyButton(
              icon: Icons.arrow_back,
              tooltip: 'Back',
              onPressed: () => _press(CarPlayKey.back),
            ),
            _KeyButton(
              icon: Icons.rotate_left,
              tooltip: 'Knob left',
              onPressed: () => _press(CarPlayKey.knobLeft),
            ),
            _KeyButton(
              icon: Icons.radio_button_checked,
              tooltip: 'Select',
              onPressed: () => _press(CarPlayKey.select),
            ),
            _KeyButton(
              icon: Icons.rotate_right,
              tooltip: 'Knob right',
              onPressed: () => _press(CarPlayKey.knobRight),
            ),
            _KeyButton(
              icon: Icons.skip_previous,
              tooltip: 'Previous',
              onPressed: () => _press(CarPlayKey.previous),
            ),
            _KeyButton(
              icon: Icons.play_arrow,
              tooltip: 'Play or pause',
              onPressed: () => _press(CarPlayKey.playPause),
            ),
            _KeyButton(
              icon: Icons.skip_next,
              tooltip: 'Next',
              onPressed: () => _press(CarPlayKey.next),
            ),
            // Siri listens for as long as the button is held, so it sends both edges
            // rather than a press.
            Listener(
              onPointerDown: (_) {
                controller.sendKey(CarPlayKey.siri, down: true);
                onInput('siri down');
              },
              onPointerUp: (_) {
                controller.sendKey(CarPlayKey.siri, down: false);
                onInput('siri up');
              },
              child: const _KeyButton(
                icon: Icons.mic,
                tooltip: 'Siri (hold)',
                onPressed: _noop,
              ),
            ),
            const SizedBox(width: 8),
            IconButton(
              tooltip: 'Night mode',
              onPressed: () => controller.setNightMode(!controller.nightMode),
              icon: Icon(controller.nightMode ? Icons.dark_mode : Icons.light_mode),
            ),
            IconButton(
              tooltip: 'Adapter',
              isSelected: adapterPanelOpen,
              onPressed: onToggleAdapterPanel,
              icon: const Icon(Icons.settings_input_component),
            ),
          ],
        ),
      ),
    );
  }

  static void _noop() {}
}

class _KeyButton extends StatelessWidget {
  final IconData icon;
  final String tooltip;
  final VoidCallback onPressed;

  const _KeyButton({required this.icon, required this.tooltip, required this.onPressed});

  @override
  Widget build(BuildContext context) =>
      IconButton.filledTonal(tooltip: tooltip, onPressed: onPressed, icon: Icon(icon));
}
