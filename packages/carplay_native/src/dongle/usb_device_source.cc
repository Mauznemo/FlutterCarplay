// SPDX-License-Identifier: GPL-3.0-or-later
// The Linux device source: libusb lists the bus. See usb_device_source.h.
#include "usb_device_source.h"

#include <libusb.h>

#include <cstdio>

#include "protocol.h"

namespace cp {
namespace dongle {

namespace {

bool IsAdapter(const libusb_device_descriptor& descriptor) {
  if (descriptor.idVendor != kVendorId) {
    return false;
  }
  for (uint16_t product : kProductIds) {
    if (descriptor.idProduct == product) {
      return true;
    }
  }
  return false;
}

libusb_device* FindAdapter(libusb_device** list, ssize_t count) {
  for (ssize_t i = 0; i < count; ++i) {
    libusb_device_descriptor descriptor{};
    if (libusb_get_device_descriptor(list[i], &descriptor) == 0 && IsAdapter(descriptor)) {
      return list[i];
    }
  }
  return nullptr;
}

std::string Describe(libusb_device* device) {
  libusb_device_descriptor descriptor{};
  libusb_get_device_descriptor(device, &descriptor);
  char text[96];
  std::snprintf(text, sizeof(text), "%04x:%04x on bus %u, device %u", descriptor.idVendor,
                descriptor.idProduct, libusb_get_bus_number(device),
                libusb_get_device_address(device));
  return text;
}

std::string UsbError(int code) { return libusb_strerror(static_cast<libusb_error>(code)); }

}  // namespace

void PrepareUsbContext() {}

bool AdapterAttached(libusb_context* context) {
  libusb_device** list = nullptr;
  const ssize_t count = libusb_get_device_list(context, &list);
  if (count < 0) {
    return false;
  }
  const bool present = FindAdapter(list, count) != nullptr;
  libusb_free_device_list(list, 1);
  return present;
}

bool OpenAdapter(libusb_context* context, libusb_device_handle** handle,
                 std::string* description, std::string* error) {
  libusb_device** list = nullptr;
  const ssize_t count = libusb_get_device_list(context, &list);
  if (count < 0) {
    *error = "Could not list USB devices: " + UsbError(static_cast<int>(count));
    return false;
  }
  libusb_device* device = FindAdapter(list, count);
  if (device == nullptr) {
    libusb_free_device_list(list, 1);
    *error = "No Carlinkit adapter on USB.";
    return false;
  }
  *description = Describe(device);
  const int result = libusb_open(device, handle);
  libusb_free_device_list(list, 1);
  if (result != 0) {
    *error = "Could not open the adapter (" + *description + "): " + UsbError(result);
    if (result == LIBUSB_ERROR_ACCESS) {
      *error += ". A normal user needs a udev rule for it, see tools/setup-dev-machine.sh "
                "--udev.";
    }
    return false;
  }
  return true;
}

void CloseAdapter(libusb_device_handle* handle) { libusb_close(handle); }

}  // namespace dongle
}  // namespace cp
