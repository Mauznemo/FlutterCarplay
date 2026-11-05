// SPDX-License-Identifier: GPL-3.0-or-later
// The USB connection to one Carlinkit adapter: claim it, keep reads pending, write.
//
// Two rules from the adapter's firmware decide the shape of this file
// (docs/dongle-protocol.md, sections 1.4 and 3.8):
//
//   - **A read must always be pending.** The adapter resets the link when it cannot
//     hand the host data for 4.5 seconds. So reads are asynchronous, several are in
//     flight at once, and each is resubmitted from its own completion before anything
//     else happens.
//   - **Reads are fixed chunks, never "until the short packet".** The adapter does not
//     end a 512 byte aligned transfer with a zero length packet. StreamParser puts the
//     messages back together.
//
// Writes go through a thread of their own, because a synchronous libusb write may not
// be issued from the event thread the reads complete on, and message handlers run on
// that thread.
//
// libusb is initialised once for the process and never shut down, the same decision
// android_auto made in its usb_context.h for the same reason: a completion delivered
// after libusb_exit is a crash in a destructor nobody can pin down, and a head unit
// owns its USB bus for its whole life anyway.

#ifndef CARPLAY_NATIVE_DONGLE_USB_LINK_H_
#define CARPLAY_NATIVE_DONGLE_USB_LINK_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "stream_parser.h"

struct libusb_context;
struct libusb_device_handle;
struct libusb_transfer;

namespace cp {
namespace dongle {

// The process wide libusb context, with its event thread running. nullptr, with
// `error` filled in, if libusb will not start; a later call tries again.
libusb_context* SharedUsbContext(std::string* error);

// Whether an adapter is on the bus, open or not. Asks for nothing and claims nothing.
bool AdapterPresent();

class UsbLink {
 public:
  // Called on the libusb event thread, once per message. Must not block: every moment
  // spent here is a moment no read is pending.
  using MessageHandler = StreamParser::MessageHandler;
  // Called at most once per Open, from the event thread or the writer thread, when the
  // link fails underneath: the adapter was unplugged or rebooted, or stopped answering.
  // Must not call Close, which waits for those threads.
  using FailedHandler = std::function<void(const std::string& reason)>;

  UsbLink(MessageHandler on_message, FailedHandler on_failed);
  ~UsbLink();

  UsbLink(const UsbLink&) = delete;
  UsbLink& operator=(const UsbLink&) = delete;

  // Finds the first adapter on the bus, claims its interface and starts reading and
  // writing. False, with `error` saying why, if there is none or it cannot be had.
  bool Open(std::string* error);

  // Stops reading and writing and lets go of the adapter. Idempotent. Waits for the
  // reads in flight to come back cancelled, so no handler runs after it returns. Not
  // from a handler.
  void Close();

  // Queues one whole message for writing. False if the link is not open or the queue
  // is full, which only happens when the adapter has stopped taking writes.
  bool Send(std::vector<uint8_t> message);

  bool open() const { return open_.load(); }

  // "1314:1520 on bus 1, device 7", for logs. Empty while closed.
  std::string description() const;

  uint64_t bytes_read() const { return bytes_read_.load(); }
  uint64_t bytes_written() const { return bytes_written_.load(); }
  uint64_t skipped_bytes() const;

 private:
  static void OnTransferComplete(libusb_transfer* transfer);
  void HandleTransfer(libusb_transfer* transfer);
  void RunWriter();
  void Fail(const std::string& reason);

  MessageHandler on_message_;
  FailedHandler on_failed_;
  StreamParser parser_;

  libusb_device_handle* handle_ = nullptr;
  uint8_t endpoint_in_ = 0;
  uint8_t endpoint_out_ = 0;
  mutable std::mutex description_mutex_;
  std::string description_;

  std::vector<libusb_transfer*> transfers_;
  std::vector<std::vector<uint8_t>> buffers_;
  std::mutex transfer_mutex_;
  std::condition_variable transfer_cv_;
  int in_flight_ = 0;
  // Consecutive failed reads, reset by every good one.
  int read_errors_ = 0;

  std::thread writer_;
  std::mutex write_mutex_;
  std::condition_variable write_cv_;
  std::deque<std::vector<uint8_t>> write_queue_;
  bool writer_quit_ = false;

  std::atomic<bool> open_{false};
  std::atomic<bool> closing_{false};
  std::atomic<bool> failed_{false};
  std::atomic<uint64_t> bytes_read_{0};
  std::atomic<uint64_t> bytes_written_{0};
};

}  // namespace dongle
}  // namespace cp

#endif  // CARPLAY_NATIVE_DONGLE_USB_LINK_H_
