// SPDX-License-Identifier: GPL-3.0-or-later
// Getting news from native threads to the Dart isolate.
//
// News comes from native threads (the USB event thread, the session's control thread,
// the decoder), and none of those may call into Dart directly. The Dart side registers a NativeCallable.listener, whose function
// pointer is safe to invoke from any thread: the runtime marshals the call onto the
// isolate. So this is a thin, thread safe holder around that pointer rather than a
// queue of its own.

#ifndef CARPLAY_NATIVE_EVENT_BUS_H_
#define CARPLAY_NATIVE_EVENT_BUS_H_

#include <mutex>
#include <string>

#include "cp_core.h"

namespace cp {

class EventBus {
 public:
  explicit EventBus(CpEventCallback callback);

  // Reports a state change. `message` may be empty. The string is copied onto the heap
  // and ownership passes to Dart, which frees it with aa_string_free.
  void Emit(CpState state, const std::string& message = {});

  // Stops delivery. Called before the Dart side tears its listener down, so a late
  // event from a native thread cannot reach a dead callable.
  void Close();

  CpState last_state() const;

 private:
  mutable std::mutex mutex_;
  CpEventCallback callback_;
  CpState last_state_ = CP_STATE_IDLE;
  bool closed_ = false;
};

}  // namespace cp

#endif  // CARPLAY_NATIVE_EVENT_BUS_H_
