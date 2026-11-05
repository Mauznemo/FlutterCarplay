# The native core on Android: the same sources under ../src the Linux plugin builds,
# with the Android backends in place of the Linux ones.
#
#   Linux                            Android
#   present/gl_adapter.cc            present/gl_adapter_android.cc   EGL into a SurfaceProducer
#   video/libav_decoder.cc           video/mediacodec_decoder.cc     MediaCodec
#   audio/pulse_sink.cc              audio/aaudio_sink.cc
#   audio/pulse_source.cc            audio/aaudio_source.cc
#   dongle/usb_device_source.cc      dongle/usb_device_source_android.cc  UsbManager via Java
#   carplay_native_plugin.cc         platform/android_jni.cc         the plugin's entry point
#
# Everything else, the adapter protocol, the session and the C ABI, is shared. Only libusb
# is a dependency, and it is compiled here from the submodule rather than prebuilt.
cmake_minimum_required(VERSION 3.22)
project(carplay_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CP_PACKAGE "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CP_SRC "${CP_PACKAGE}/src")
set(LIBUSB_DIR "${CP_PACKAGE}/third_party/libusb")

option(CP_USE_CCACHE "Compile through ccache when it is installed" ON)
if(CP_USE_CCACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(CP_CCACHE_EXECUTABLE NAMES ccache)
  if(CP_CCACHE_EXECUTABLE)
    set(CMAKE_C_COMPILER_LAUNCHER "${CP_CCACHE_EXECUTABLE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CP_CCACHE_EXECUTABLE}")
  endif()
endif()

# === libusb ===
#
# The sources libusb's own android/jni/libusb.mk builds, with its android/config.h. No
# udev and no device discovery on Android: devices arrive from UsbManager as descriptors
# and are wrapped, see usb_device_source_android.cc.
if(NOT EXISTS "${LIBUSB_DIR}/libusb/core.c")
  message(FATAL_ERROR "The libusb submodule is missing. Run:\n  git submodule update --init --recursive")
endif()
add_library(usb-1.0 STATIC
  "${LIBUSB_DIR}/libusb/core.c"
  "${LIBUSB_DIR}/libusb/descriptor.c"
  "${LIBUSB_DIR}/libusb/hotplug.c"
  "${LIBUSB_DIR}/libusb/io.c"
  "${LIBUSB_DIR}/libusb/sync.c"
  "${LIBUSB_DIR}/libusb/strerror.c"
  "${LIBUSB_DIR}/libusb/os/linux_usbfs.c"
  "${LIBUSB_DIR}/libusb/os/events_posix.c"
  "${LIBUSB_DIR}/libusb/os/threads_posix.c"
  "${LIBUSB_DIR}/libusb/os/linux_netlink.c"
)
target_include_directories(usb-1.0
  PRIVATE "${LIBUSB_DIR}/android" "${LIBUSB_DIR}/libusb/os"
  PUBLIC "${LIBUSB_DIR}/libusb")
set_target_properties(usb-1.0 PROPERTIES C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
target_compile_options(usb-1.0 PRIVATE -pthread -w)

# === the core ===
add_library(carplay_native SHARED
  "${CP_SRC}/cp_core.cc"
  "${CP_SRC}/log.cc"
  "${CP_SRC}/event_bus.cc"
  "${CP_SRC}/frame_ring.cc"
  "${CP_SRC}/frame_size.cc"
  "${CP_SRC}/test_pattern.cc"
  "${CP_SRC}/audio/audio_input.cc"
  "${CP_SRC}/audio/audio_output.cc"
  "${CP_SRC}/audio/aaudio_sink.cc"
  "${CP_SRC}/audio/aaudio_source.cc"
  "${CP_SRC}/dongle/dongle_session.cc"
  "${CP_SRC}/dongle/nmea.cc"
  "${CP_SRC}/dongle/protocol.cc"
  "${CP_SRC}/dongle/stream_parser.cc"
  "${CP_SRC}/dongle/usb_device_source_android.cc"
  "${CP_SRC}/dongle/usb_link.cc"
  "${CP_SRC}/metadata/json.cc"
  "${CP_SRC}/platform/android_jni.cc"
  "${CP_SRC}/present/gl_adapter_android.cc"
  "${CP_SRC}/video/mediacodec_decoder.cc"
  "${CP_SRC}/video/video_margins.cc"
)

target_include_directories(carplay_native PRIVATE "${CP_SRC}")
set_target_properties(carplay_native PROPERTIES CXX_VISIBILITY_PRESET hidden)

# What Dart and Java call and nothing else, for the same reason as on Linux: see the
# version script.
set(CP_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/carplay_native.map")
target_link_options(carplay_native PRIVATE
  "-Wl,--version-script=${CP_VERSION_SCRIPT}"
  # 16 KB pages, which Google Play requires of native code from Android 15 devices on.
  "-Wl,-z,max-page-size=16384")
set_property(TARGET carplay_native APPEND PROPERTY LINK_DEPENDS "${CP_VERSION_SCRIPT}")

target_link_libraries(carplay_native PRIVATE
  usb-1.0
  aaudio
  android
  EGL
  GLESv3
  log
  mediandk
  nativewindow)
