// SPDX-License-Identifier: GPL-3.0-or-later
// Holds the Flutter objects that only the GTK plugin entry point can hand us.
//
// A pure FFI plugin never gets an FlPluginRegistrar, and without one there is no
// FlTextureRegistrar, and without that there is no way to show a texture. So the plugin
// keeps its `pluginClass` registration purely to capture these two on startup, and the
// FFI side reads them from here.

#ifndef CARPLAY_NATIVE_PRESENT_TEXTURE_REGISTRY_H_
#define CARPLAY_NATIVE_PRESENT_TEXTURE_REGISTRY_H_

#include <flutter_linux/flutter_linux.h>

namespace cp {

// Called once from carplay_native_plugin_register_with_registrar.
void SetFlutterRegistrar(FlTextureRegistrar* registrar, FlView* view);

// NULL until the plugin has registered, which happens before any Dart code runs.
FlTextureRegistrar* GetTextureRegistrar();
FlView* GetFlutterView();

}  // namespace cp

#endif  // CARPLAY_NATIVE_PRESENT_TEXTURE_REGISTRY_H_
