// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:ffi';
import 'dart:io';

import 'bindings/cp_core_bindings.dart';

/// Loads the native head unit core and exposes its bindings.
///
/// On Linux the core lives inside `libcarplay_native_plugin.so`, the same shared object
/// Flutter already loaded to run the plugin's GTK registration, so opening it again by
/// name is cheap: `dlopen` returns a handle to the copy that is already mapped.
class CpLibrary {
  CpLibrary._(this.bindings);

  /// The generated FFI bindings.
  final CpCoreBindings bindings;

  static CpLibrary? _instance;

  /// The process wide instance, loaded on first use.
  ///
  /// Throws [UnsupportedError] off Linux, and [CpLibraryLoadException] if the shared
  /// object cannot be found, which in practice means the plugin was not bundled.
  static CpLibrary get instance => _instance ??= CpLibrary._(CpCoreBindings(_open()));

  static DynamicLibrary _open() {
    if (!Platform.isLinux) {
      throw UnsupportedError('carplay_native only supports Linux.');
    }
    const name = 'libcarplay_native_plugin.so';
    try {
      return DynamicLibrary.open(name);
    } on ArgumentError catch (error) {
      // Fall back to the running process. If Flutter loaded the plugin with its symbols
      // globally visible, this finds them even when the file itself is not on the
      // loader's search path.
      try {
        final process = DynamicLibrary.process();
        process.lookup<NativeFunction<Void Function(Pointer<Char>)>>('cp_string_free');
        return process;
      } on ArgumentError {
        throw CpLibraryLoadException(name, error.toString());
      }
    }
  }
}

/// Thrown when the native core cannot be loaded.
class CpLibraryLoadException implements Exception {
  /// Creates an exception naming the library that failed to load.
  const CpLibraryLoadException(this.libraryName, this.details);

  /// The shared object that could not be opened.
  final String libraryName;

  /// The loader's own error text.
  final String details;

  @override
  String toString() =>
      'Could not load $libraryName. The carplay_native plugin is probably not bundled '
      'with this application. Details: $details';
}
