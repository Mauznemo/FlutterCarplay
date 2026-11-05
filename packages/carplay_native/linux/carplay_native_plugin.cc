// SPDX-License-Identifier: GPL-3.0-or-later
#include "include/carplay_native/carplay_native_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include "src/present/texture_registry.h"

// This plugin exists for one reason: to run at startup and capture the
// FlTextureRegistrar.
//
// Everything else the plugin does goes through the C ABI in src/cp_core.h, called
// directly from Dart over FFI, because touch events and per frame signalling through a
// method channel would add latency and garbage for no benefit. But a pure ffiPlugin
// never receives an FlPluginRegistrar, and without one there is no texture registrar
// and therefore no way to put video on screen. Hence the pluginClass entry in
// pubspec.yaml and this file.
//
// Do not add a method channel here without a reason that FFI cannot serve.

#define CARPLAY_NATIVE_PLUGIN(obj)                                     \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), carplay_native_plugin_get_type(), \
                              CarplayNativePlugin))

struct _CarplayNativePlugin {
  GObject parent_instance;
};

G_DEFINE_TYPE(CarplayNativePlugin, carplay_native_plugin, g_object_get_type())

static void carplay_native_plugin_class_init(CarplayNativePluginClass* klass) {}

static void carplay_native_plugin_init(CarplayNativePlugin* self) {}

void carplay_native_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  CarplayNativePlugin* plugin =
      CARPLAY_NATIVE_PLUGIN(g_object_new(carplay_native_plugin_get_type(), nullptr));

  cp::SetFlutterRegistrar(fl_plugin_registrar_get_texture_registrar(registrar),
                          fl_plugin_registrar_get_view(registrar));

  g_object_unref(plugin);
}
