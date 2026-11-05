// SPDX-License-Identifier: GPL-3.0-or-later
// The only file besides android_jni.cc that names a JNI type.
//
// On Android the core is loaded twice over: Java loads it to register the plugin, and
// Dart opens the same library for FFI. Everything Dart does goes through the C ABI in
// cp_core.h exactly as on Linux. JNI is only for what Java alone can reach: Flutter's
// texture registry, since a SurfaceProducer is a Java object and the Surface it hands
// out is what the present adapter draws into; and USB, since an app only reaches a USB
// device through UsbManager, which hands it over as a file descriptor once the user has
// allowed it. From android_auto, less its Bluetooth and Wi-Fi, which the adapter does
// for itself here.

#ifndef CARPLAY_NATIVE_PLATFORM_ANDROID_JNI_H_
#define CARPLAY_NATIVE_PLATFORM_ANDROID_JNI_H_

#if defined(__ANDROID__)

#include <cstdint>
#include <functional>
#include <string>

struct ANativeWindow;

namespace cp {
namespace android {

// What a surface's owner is told. Both are called on the main thread.
class SurfaceListener {
 public:
  virtual ~SurfaceListener() = default;

  // A surface to draw into, the first one or a replacement after a resize or a return
  // from the background. Ownership of the reference passes to the listener, which must
  // ANativeWindow_release it when done.
  virtual void OnSurfaceAvailable(ANativeWindow* window) = 0;

  // Flutter is taking the surface away. Must not return until nothing draws into it.
  virtual void OnSurfaceCleanup() = 0;
};

// Creates a Flutter texture backed by a SurfaceProducer `width` by `height` pixels and
// returns its id, or -1 when no engine is attached. `listener` hears about the surface,
// once before this returns and again whenever it changes, until ReleaseSurface. Call on
// the platform thread.
int64_t CreateSurface(SurfaceListener* listener, int32_t width, int32_t height);

// Asks for the texture to be `width` by `height`. Returns at once; the new surface
// arrives through OnSurfaceAvailable. Safe from any thread.
void ResizeSurface(SurfaceListener* listener, int32_t width, int32_t height);

// Releases the texture. The listener hears OnSurfaceCleanup first, and nothing at all
// once this has returned. Call on the platform thread.
void ReleaseSurface(SurfaceListener* listener);

// A Carlinkit adapter the app may now use: an open usbfs file descriptor and a name for
// the log. Called on the main thread.
//
// The descriptor stays open until ReleaseUsbDevice is called with it. Java owns it and
// must not close it under libusb, which polls it from its event thread.
using UsbDeviceHandler = std::function<void(int fd, std::string name)>;

// Starts watching for adapters on USB: everything already attached, then every arrival.
// An adapter the app has no permission for yet is asked about first, which shows the
// user a dialog. Replaces any previous handler.
void StartUsbDiscovery(UsbDeviceHandler handler);

// Stops watching and drops the handler. Devices already handed over stay open until
// they are released.
void StopUsbDiscovery();

// Offers every attached adapter again that is not currently handed over: one whose
// session ended while it stayed plugged in, which no arrival will ever announce.
void RescanUsb();

// Closes a descriptor StartUsbDiscovery handed over. Safe from any thread.
void ReleaseUsbDevice(int fd);

// Whether UsbManager lists a Carlinkit adapter. Asks for no permission. Safe from any
// thread.
bool UsbAdapterPresent();

}  // namespace android
}  // namespace cp

#endif  // defined(__ANDROID__)

#endif  // CARPLAY_NATIVE_PLATFORM_ANDROID_JNI_H_
