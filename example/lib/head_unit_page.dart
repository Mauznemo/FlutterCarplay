// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:async';
import 'dart:io';

import 'package:carplay/carplay.dart';
import 'package:flutter/material.dart';

import 'bench/adapter_panel.dart';
import 'bench/dock.dart';
import 'config.dart';
import 'head_unit/metadata_overlay.dart';
import 'head_unit/projection_placeholder.dart';
import 'head_unit/status_bar.dart';

/// The whole head unit: the projection, and Flutter drawn over it.
///
/// Shaped like the real thing: a status bar of the head unit's own above the
/// projection, and every other piece of chrome an ordinary Flutter widget stacked on
/// top of it. Touches on the chrome go to the chrome, and everywhere else they go
/// through to [CarPlayView], which forwards them to the phone. Nothing here arranges
/// that: it is ordinary Flutter hit testing.
class HeadUnitPage extends StatefulWidget {
  const HeadUnitPage({super.key});

  @override
  State<HeadUnitPage> createState() => _HeadUnitPageState();
}

class _HeadUnitPageState extends State<HeadUnitPage> {
  final CarPlayController _controller = CarPlayController(config: headUnitConfig());

  bool _patternRunning = false;
  bool _adapterPanelOpen = false;
  bool _hostUiShown = false;
  String _lastInput = 'none';
  StreamSubscription<CarPlayRequest>? _requests;
  StreamSubscription<bool>? _adapterPresence;

  @override
  void initState() {
    super.initState();
    _controller.addListener(_onChanged);
    // The driver tapped the car's icon in CarPlay. A product shows its own home screen
    // here; the bench shows a card saying so, over the projection.
    _requests = _controller.requests.listen((request) {
      debugPrint('[Bench] phone asks: ${request.name}');
      if (request == CarPlayRequest.showHostUi) {
        setState(() => _hostUiShown = true);
      }
    });
    // Logged rather than acted on: a product would start a session when the adapter is
    // plugged in, and the bench leaves starting to its button.
    debugPrint('[Bench] adapter on USB: ${_controller.adapterPresent}');
    _adapterPresence = _controller.adapterPresentChanges.listen(
      (present) => debugPrint('[Bench] adapter on USB: $present'),
    );
    // Example app knobs, not plugin ones, for runs driven from a script.
    if (Platform.environment['CP_AUTOSTART'] == '1') {
      WidgetsBinding.instance.addPostFrameCallback((_) => _controller.start());
    }
    if (Platform.environment['CP_TEST_PATTERN'] == '1') {
      WidgetsBinding.instance.addPostFrameCallback((_) => _togglePattern());
    }
  }

  void _onChanged() {
    if (mounted) {
      setState(() {});
    }
  }

  Future<void> _togglePattern() async {
    if (_patternRunning) {
      await _controller.stopTestPattern();
    } else {
      await _controller.startTestPattern();
    }
    setState(() => _patternRunning = !_patternRunning);
  }

  @override
  void dispose() {
    _requests?.cancel();
    _adapterPresence?.cancel();
    _controller.removeListener(_onChanged);
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: Column(
        children: [
          StatusBar(controller: _controller, lastInput: _lastInput),
          Expanded(
            child: Stack(
              children: [
                Positioned.fill(
                  child: CarPlayView(
                    controller: _controller,
                    placeholder: ProjectionPlaceholder(controller: _controller),
                  ),
                ),
                Positioned(
                  left: 12,
                  bottom: 12,
                  child: MetadataOverlay(controller: _controller),
                ),
                if (_adapterPanelOpen)
                  Positioned(
                    right: 12,
                    top: 12,
                    bottom: 12,
                    child: AdapterPanel(controller: _controller),
                  ),
                if (_hostUiShown)
                  Positioned.fill(
                    child: _HostUiCard(
                      onBack: () => setState(() => _hostUiShown = false),
                    ),
                  ),
              ],
            ),
          ),
          Dock(
            controller: _controller,
            patternRunning: _patternRunning,
            adapterPanelOpen: _adapterPanelOpen,
            onStart: _controller.start,
            onStop: _controller.stop,
            onTogglePattern: _togglePattern,
            onToggleAdapterPanel: () =>
                setState(() => _adapterPanelOpen = !_adapterPanelOpen),
            onInput: (input) => setState(() => _lastInput = input),
          ),
        ],
      ),
    );
  }
}

/// Stands in for the head unit's own interface, which CarPlay's car icon asks for.
class _HostUiCard extends StatelessWidget {
  final VoidCallback onBack;

  const _HostUiCard({required this.onBack});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return ColoredBox(
      color: theme.colorScheme.surface,
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text("The head unit's own interface", style: theme.textTheme.headlineSmall),
            const SizedBox(height: 8),
            const Text('CarPlay asked for it. The phone is still connected.'),
            const SizedBox(height: 16),
            FilledButton(onPressed: onBack, child: const Text('Back to CarPlay')),
          ],
        ),
      ),
    );
  }
}
