# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

# Project-level configuration.
set(PROJECT_NAME "carplay_native")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed.
set(PLUGIN_NAME "carplay_native_plugin")

# === build caching ===
#
# Not the three minutes android_auto saves with it, there is no aasdk here, but a clean
# rebuild still compiles libavcodec glue and the GL converter. ccache keys on
# preprocessed source and its cache survives `flutter clean`. Set before add_library,
# because CMAKE_<LANG>_COMPILER_LAUNCHER initialises a target property at creation.
option(CP_USE_CCACHE "Compile through ccache when it is installed" ON)
if(CP_USE_CCACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(CP_CCACHE_EXECUTABLE NAMES ccache)
  if(CP_CCACHE_EXECUTABLE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CP_CCACHE_EXECUTABLE}")
  endif()
endif()

# The native core lives at the package root, beside this directory, so an Android build
# can share it later. Only the GTK entry point is Linux's own.
set(CP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

list(APPEND PLUGIN_SOURCES
  "carplay_native_plugin.cc"
  "${CP_SRC}/cp_core.cc"
  "${CP_SRC}/log.cc"
  "${CP_SRC}/event_bus.cc"
  "${CP_SRC}/frame_ring.cc"
  "${CP_SRC}/frame_size.cc"
  "${CP_SRC}/test_pattern.cc"
  "${CP_SRC}/audio/audio_input.cc"
  "${CP_SRC}/audio/audio_output.cc"
  "${CP_SRC}/audio/pulse_sink.cc"
  "${CP_SRC}/audio/pulse_source.cc"
  "${CP_SRC}/dongle/dongle_session.cc"
  "${CP_SRC}/dongle/nmea.cc"
  "${CP_SRC}/dongle/protocol.cc"
  "${CP_SRC}/dongle/stream_parser.cc"
  "${CP_SRC}/dongle/usb_device_source.cc"
  "${CP_SRC}/dongle/usb_link.cc"
  "${CP_SRC}/metadata/json.cc"
  "${CP_SRC}/present/gl_adapter.cc"
  "${CP_SRC}/present/texture_registry.cc"
  "${CP_SRC}/video/libav_decoder.cc"
  "${CP_SRC}/video/video_margins.cc"
)

add_library(${PLUGIN_NAME} SHARED
  ${PLUGIN_SOURCES}
)

apply_standard_settings(${PLUGIN_NAME})

# apply_standard_settings pins C++14, and the core is C++17. target_compile_features
# takes the higher of the two, so this has to come after.
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

# Symbols are hidden by default to reduce the chance of accidental conflicts between
# plugins. The C ABI in src/cp_core.h opts back in with CP_EXPORT, because Dart has to
# be able to dlsym it, and the version script below says the same thing for the linker.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# LINK_FLAGS rather than target_link_options, which needs CMake 3.13 and this file is
# pinned at 3.10 on purpose, see the note at the top.
set(PLUGIN_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/carplay_native_plugin.map")
set_property(TARGET ${PLUGIN_NAME} APPEND_STRING PROPERTY
  LINK_FLAGS " -Wl,--version-script=${PLUGIN_VERSION_SCRIPT}")
set_property(TARGET ${PLUGIN_NAME} APPEND PROPERTY
  LINK_DEPENDS "${PLUGIN_VERSION_SCRIPT}")

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)

find_package(PkgConfig REQUIRED)

# libepoxy is how GTK itself resolves GL entry points, so use the same thing rather
# than linking libGL directly and risking a different dispatch table than Flutter's.
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::EPOXY)

# The adapter is a vendor class USB device, read and written with libusb.
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::LIBUSB)

# libavcodec does the H.264, libavutil owns the VA-API device and the DRM prime export
# that makes the zero copy path possible, libswscale converts to RGBA on the software
# fallback. No libavformat: the adapter sends raw Annex B, there is no container.
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::LIBAV)

# libpulse-simple plays the phone's PCM and reads the microphone, libpulse lists the
# devices. The same code runs on PipeWire, PulseAudio, and the compatibility layer most
# infotainment images ship.
pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse libpulse-simple)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::LIBPULSE)

# drm_fourcc.h is a header of macros that ships with the kernel headers, so there is
# nothing to find and nothing extra for an end user to install.

# List of absolute paths to libraries that should be bundled with the plugin. Empty:
# everything beyond the system libraries is inside libcarplay_native_plugin.so.
set(carplay_native_bundled_libraries
  ""
  PARENT_SCOPE
)
