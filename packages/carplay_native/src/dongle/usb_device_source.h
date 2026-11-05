// SPDX-License-Identifier: GPL-3.0-or-later
// Where the adapter comes from: the one part of the USB path that differs by platform.
//
// On Linux libusb lists the bus itself (usb_device_source.cc). On Android an app cannot
// read the bus; UsbManager hands it the adapter as a file descriptor once the user has
// allowed it, and libusb wraps that (usb_device_source_android.cc). Everything after the
// handle exists, claiming, endpoints, reads and writes, is UsbLink and shared.

#ifndef CARPLAY_NATIVE_DONGLE_USB_DEVICE_SOURCE_H_
#define CARPLAY_NATIVE_DONGLE_USB_DEVICE_SOURCE_H_

#include <string>

struct libusb_context;
struct libusb_device_handle;

namespace cp {
namespace dongle {

// Set before libusb_init. On Android, that libusb must not try to list the bus.
void PrepareUsbContext();

// Whether an adapter is attached. Opens nothing and asks for no permission.
bool AdapterAttached(libusb_context* context);

// Opens the first adapter. False, with `error` saying why, if there is none or it cannot
// be had yet (on Android: while the user has not answered the permission dialog).
bool OpenAdapter(libusb_context* context, libusb_device_handle** handle,
                 std::string* description, std::string* error);

// Closes a handle OpenAdapter returned and gives the device back.
void CloseAdapter(libusb_device_handle* handle);

}  // namespace dongle
}  // namespace cp

#endif  // CARPLAY_NATIVE_DONGLE_USB_DEVICE_SOURCE_H_
