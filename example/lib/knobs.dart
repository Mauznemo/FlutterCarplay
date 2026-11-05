// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:io';

/// The example app's knobs, not the plugin's: from the environment on Linux, and from
/// `--dart-define` where there is no environment, which is Android.
///
///     flutter run --dart-define=CP_TEST_PATTERN=1
class Knobs {
  static const _autostart = String.fromEnvironment('CP_AUTOSTART');
  static const _testPattern = String.fromEnvironment('CP_TEST_PATTERN');
  static const _simulate = String.fromEnvironment('CP_SIMULATE');
  static const _cluster = String.fromEnvironment('CP_CLUSTER');

  static bool _on(String name, String defined) =>
      Platform.environment[name] == '1' || defined == '1';

  /// Press Start on launch.
  static bool get autostart => _on('CP_AUTOSTART', _autostart);

  /// Start the test pattern on launch.
  static bool get testPattern => _on('CP_TEST_PATTERN', _testPattern);

  /// Use the simulator instead of the adapter.
  static bool get simulate => _on('CP_SIMULATE', _simulate);

  /// Add an instrument cluster screen.
  static bool get cluster => _on('CP_CLUSTER', _cluster);
}
