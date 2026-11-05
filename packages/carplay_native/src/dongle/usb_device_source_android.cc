// SPDX-License-Identifier: GPL-3.0-or-later
// The Android device source: adapters arrive from UsbManager as file descriptors, which
// libusb wraps. See usb_device_source.h and CarplayNativePlugin.java.
#include "usb_device_source.h"

#include <libusb.h>

#include <deque>
#include <map>
#include <mutex>
#include <utility>

#include "../log.h"
#include "../platform/android_jni.h"

namespace cp {
namespace dongle {

namespace {

struct Offered {
  int fd = -1;
  std::string name;
};

std::mutex g_mutex;
bool g_discovering = false;
// Descriptors Java has opened and handed over, waiting for OpenAdapter.
std::deque<Offered> g_offered;
// Which descriptor each open handle wraps, to give back when it closes.
std::map<libusb_device_handle*, int> g_open;

void StartDiscovery() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_discovering) {
      return;
    }
    g_discovering = true;
  }
  // Process lifetime, like the libusb context: the adapter comes and goes, the head
  // unit stays.
  android::StartUsbDiscovery([](int fd, std::string name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_offered.push_back({fd, std::move(name)});
  });
}

}  // namespace

void PrepareUsbContext() {
  // An app cannot read /dev/bus/usb, so libusb must not try: every device arrives as a
  // descriptor from UsbManager and is wrapped.
  libusb_set_option(nullptr, LIBUSB_OPTION_NO_DEVICE_DISCOVERY);
}

bool AdapterAttached(libusb_context*) { return android::UsbAdapterPresent(); }

bool OpenAdapter(libusb_context* context, libusb_device_handle** handle,
                 std::string* description, std::string* error) {
  StartDiscovery();
  Offered offered;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_offered.empty()) {
      offered = g_offered.front();
      g_offered.pop_front();
    }
  }
  if (offered.fd < 0) {
    // An adapter still attached from a session that ended is not announced again by an
    // arrival, so ask Java to offer what is attached and not in use.
    android::RescanUsb();
    *error = android::UsbAdapterPresent()
                 ? "Waiting for permission to use the Carlinkit adapter."
                 : "No Carlinkit adapter on USB.";
    return false;
  }
  const int result = libusb_wrap_sys_device(context, offered.fd, handle);
  if (result != 0) {
    android::ReleaseUsbDevice(offered.fd);
    *error = std::string("Could not use the adapter's descriptor: ") +
             libusb_strerror(static_cast<libusb_error>(result));
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_open[*handle] = offered.fd;
  }
  *description = offered.name;
  return true;
}

void CloseAdapter(libusb_device_handle* handle) {
  int fd = -1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_open.find(handle);
    if (it != g_open.end()) {
      fd = it->second;
      g_open.erase(it);
    }
  }
  libusb_close(handle);
  // Only now: Java must not close the descriptor while libusb still polls it.
  if (fd >= 0) {
    android::ReleaseUsbDevice(fd);
  }
}

}  // namespace dongle
}  // namespace cp
