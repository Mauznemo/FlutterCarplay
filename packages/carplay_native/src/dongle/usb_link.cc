// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_link.h"

#include <libusb.h>

#include <chrono>
#include <cstdio>
#include <utility>

#include "../log.h"
#include "protocol.h"
#include "usb_device_source.h"

namespace cp {
namespace dongle {

namespace {

// Several reads in flight, so one is always pending while another's completion is
// being handled. Each is a multiple of the 512 byte packet size, which is what makes a
// transfer that the adapter never terminates with a short packet complete anyway.
constexpr int kReadTransfers = 4;
constexpr int kReadSize = 64 * 1024;
// A good read resets this. A link that fails more than this many in a row has gone.
constexpr int kMaxReadErrors = 5;
// A write the adapter has not taken within this has failed. The adapter's own write
// timeout towards the host is 4.5 seconds, so a second is generous.
constexpr unsigned kWriteTimeoutMs = 1000;
// Enough for every message of the start up sequence and then some. A queue this deep
// means writes are not going out at all.
constexpr size_t kMaxQueuedWrites = 256;

std::mutex g_context_mutex;
libusb_context* g_context = nullptr;

std::string UsbError(int code) {
  return libusb_strerror(static_cast<libusb_error>(code));
}

const char* TransferStatusName(int status) {
  switch (status) {
    case LIBUSB_TRANSFER_COMPLETED:
      return "completed";
    case LIBUSB_TRANSFER_ERROR:
      return "error";
    case LIBUSB_TRANSFER_TIMED_OUT:
      return "timed out";
    case LIBUSB_TRANSFER_CANCELLED:
      return "cancelled";
    case LIBUSB_TRANSFER_STALL:
      return "stall";
    case LIBUSB_TRANSFER_NO_DEVICE:
      return "no device";
    case LIBUSB_TRANSFER_OVERFLOW:
      return "overflow";
    default:
      return "unknown";
  }
}

}  // namespace

libusb_context* SharedUsbContext(std::string* error) {
  std::lock_guard<std::mutex> lock(g_context_mutex);
  if (g_context != nullptr) {
    return g_context;
  }
  libusb_context* context = nullptr;
  PrepareUsbContext();
  const int result = libusb_init(&context);
  if (result != 0) {
    if (error != nullptr) {
      *error = "libusb_init failed: " + UsbError(result);
    }
    return nullptr;
  }
  g_context = context;
  // One pump for the life of the process, never joined. A cancelled transfer still
  // needs someone to deliver its completion, and stopping the pump is how a teardown
  // ends up waiting for one forever.
  std::thread([context]() {
    for (;;) {
      libusb_handle_events(context);
    }
  }).detach();
  return g_context;
}

bool AdapterPresent() {
  libusb_context* context = SharedUsbContext(nullptr);
  return context != nullptr && AdapterAttached(context);
}

UsbLink::UsbLink(MessageHandler on_message, FailedHandler on_failed)
    : on_message_(std::move(on_message)),
      on_failed_(std::move(on_failed)),
      parser_([this](uint32_t type, const uint8_t* payload, size_t size) {
        on_message_(type, payload, size);
      }) {}

UsbLink::~UsbLink() { Close(); }

bool UsbLink::Open(std::string* error) {
  if (open_.load()) {
    return true;
  }
  libusb_context* context = SharedUsbContext(error);
  if (context == nullptr) {
    return false;
  }

  libusb_device_handle* handle = nullptr;
  std::string description;
  if (!OpenAdapter(context, &handle, &description, error)) {
    return false;
  }
  int result = 0;
  // Nothing binds the vendor interface on a stock kernel, but a machine that once ran
  // something else might have a driver on it. Detached while claimed, put back after.
  libusb_set_auto_detach_kernel_driver(handle, 1);

  int configuration = 0;
  if (libusb_get_configuration(handle, &configuration) == 0 && configuration != 1) {
    libusb_set_configuration(handle, 1);
  }
  result = libusb_claim_interface(handle, 0);
  if (result != 0) {
    CloseAdapter(handle);
    *error = "Could not claim the adapter's interface (" + description +
             "): " + UsbError(result) + ". Is another head unit app running?";
    return false;
  }

  // The first bulk endpoint each way on interface 0. Every host looks them up rather
  // than hardcoding 0x83 and 0x02, because other units and older firmware differ.
  uint8_t in = 0;
  uint8_t out = 0;
  libusb_config_descriptor* config = nullptr;
  if (libusb_get_active_config_descriptor(libusb_get_device(handle), &config) == 0 &&
      config != nullptr) {
    if (config->bNumInterfaces > 0 && config->interface[0].num_altsetting > 0) {
      const libusb_interface_descriptor& setting = config->interface[0].altsetting[0];
      for (uint8_t i = 0; i < setting.bNumEndpoints; ++i) {
        const libusb_endpoint_descriptor& endpoint = setting.endpoint[i];
        if ((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
            LIBUSB_TRANSFER_TYPE_BULK) {
          continue;
        }
        const bool is_in = (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                           LIBUSB_ENDPOINT_IN;
        if (is_in && in == 0) {
          in = endpoint.bEndpointAddress;
        } else if (!is_in && out == 0) {
          out = endpoint.bEndpointAddress;
        }
      }
    }
    libusb_free_config_descriptor(config);
  }
  if (in == 0 || out == 0) {
    libusb_release_interface(handle, 0);
    CloseAdapter(handle);
    *error = "The adapter (" + description + ") has no bulk endpoint pair on interface 0.";
    return false;
  }
  // A previous host may have left either endpoint halted.
  libusb_clear_halt(handle, in);
  libusb_clear_halt(handle, out);

  handle_ = handle;
  endpoint_in_ = in;
  endpoint_out_ = out;
  {
    std::lock_guard<std::mutex> lock(description_mutex_);
    description_ = description;
  }
  parser_.Reset();
  read_errors_ = 0;
  closing_ = false;
  failed_ = false;

  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.clear();
    writer_quit_ = false;
  }
  writer_ = std::thread([this]() { RunWriter(); });

  buffers_.assign(kReadTransfers, std::vector<uint8_t>(kReadSize));
  transfers_.clear();
  for (int i = 0; i < kReadTransfers; ++i) {
    libusb_transfer* transfer = libusb_alloc_transfer(0);
    // Timeout 0: a read waits as long as it has to. Silence is normal, there is no idle
    // keepalive from the adapter's side.
    libusb_fill_bulk_transfer(transfer, handle_, endpoint_in_, buffers_[i].data(),
                              kReadSize, &UsbLink::OnTransferComplete, this, 0);
    transfers_.push_back(transfer);
  }
  open_ = true;
  for (libusb_transfer* transfer : transfers_) {
    std::lock_guard<std::mutex> lock(transfer_mutex_);
    if (libusb_submit_transfer(transfer) == 0) {
      ++in_flight_;
    }
  }
  CP_LOG(info) << "[Dongle] opened " << description << ", bulk in 0x" << std::hex
               << static_cast<int>(endpoint_in_) << ", out 0x"
               << static_cast<int>(endpoint_out_) << std::dec;
  return true;
}

void UsbLink::OnTransferComplete(libusb_transfer* transfer) {
  static_cast<UsbLink*>(transfer->user_data)->HandleTransfer(transfer);
}

void UsbLink::HandleTransfer(libusb_transfer* transfer) {
  const int status = transfer->status;
  if (status == LIBUSB_TRANSFER_COMPLETED) {
    read_errors_ = 0;
    if (transfer->actual_length > 0) {
      bytes_read_ += static_cast<uint64_t>(transfer->actual_length);
      parser_.Feed(transfer->buffer, static_cast<size_t>(transfer->actual_length));
    }
  } else if (status != LIBUSB_TRANSFER_CANCELLED) {
    ++read_errors_;
    CP_LOG(info) << "[Dongle] read " << TransferStatusName(status) << " on endpoint 0x"
                 << std::hex << static_cast<int>(endpoint_in_) << std::dec;
    if (status == LIBUSB_TRANSFER_NO_DEVICE) {
      Fail("The adapter left the bus.");
    } else if (read_errors_ >= kMaxReadErrors) {
      // A halted endpoint fails every read until cleared, and clearing is synchronous,
      // which libusb forbids inside a completion. So it ends up here, and the session
      // reopens the link, which clears both endpoints.
      Fail(std::string("Reads from the adapter keep failing (") +
           TransferStatusName(status) + ").");
    }
  }

  int resubmit_error = 0;
  {
    std::lock_guard<std::mutex> lock(transfer_mutex_);
    const bool resubmit = !closing_.load() && !failed_.load() &&
                          status != LIBUSB_TRANSFER_CANCELLED &&
                          status != LIBUSB_TRANSFER_NO_DEVICE;
    if (resubmit) {
      resubmit_error = libusb_submit_transfer(transfer);
      if (resubmit_error == 0) {
        return;
      }
    }
    --in_flight_;
    transfer_cv_.notify_all();
  }
  // A read that cannot be put back means one fewer pending, and once all of them have
  // gone this way nothing else notices until the next write fails. Seen when the device
  // vanished with every read completing as a plain error rather than "no device".
  // Outside the lock, because the handler takes the session's.
  if (resubmit_error != 0) {
    Fail("A read could not be resubmitted: " + UsbError(resubmit_error) + ".");
  }
}

void UsbLink::RunWriter() {
  int timeouts = 0;
  for (;;) {
    std::vector<uint8_t> message;
    {
      std::unique_lock<std::mutex> lock(write_mutex_);
      write_cv_.wait(lock, [this]() { return writer_quit_ || !write_queue_.empty(); });
      if (writer_quit_) {
        return;
      }
      message = std::move(write_queue_.front());
      write_queue_.pop_front();
    }
    size_t offset = 0;
    while (offset < message.size()) {
      int written = 0;
      const int result = libusb_bulk_transfer(
          handle_, endpoint_out_, message.data() + offset,
          static_cast<int>(message.size() - offset), &written, kWriteTimeoutMs);
      offset += static_cast<size_t>(written);
      bytes_written_ += static_cast<uint64_t>(written);
      if (result == 0) {
        timeouts = 0;
        continue;
      }
      if (result == LIBUSB_ERROR_TIMEOUT && ++timeouts < 3) {
        CP_LOG(info) << "[Dongle] write timed out, retrying";
        continue;
      }
      Fail("Writing to the adapter failed: " + UsbError(result) + ".");
      return;
    }
  }
}

bool UsbLink::Send(std::vector<uint8_t> message) {
  if (!open_.load() || failed_.load()) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (write_queue_.size() >= kMaxQueuedWrites) {
      return false;
    }
    write_queue_.push_back(std::move(message));
  }
  write_cv_.notify_one();
  return true;
}

void UsbLink::Fail(const std::string& reason) {
  bool expected = false;
  if (!failed_.compare_exchange_strong(expected, true)) {
    return;
  }
  if (closing_.load()) {
    return;
  }
  CP_LOG(info) << "[Dongle] link failed: " << reason;
  if (on_failed_) {
    on_failed_(reason);
  }
}

void UsbLink::Close() {
  if (!open_.exchange(false)) {
    return;
  }
  closing_ = true;

  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    writer_quit_ = true;
  }
  write_cv_.notify_one();
  if (writer_.joinable()) {
    writer_.join();
  }

  {
    std::unique_lock<std::mutex> lock(transfer_mutex_);
    for (libusb_transfer* transfer : transfers_) {
      libusb_cancel_transfer(transfer);
    }
    // The pump delivers the cancellations. Bounded, because a pump that never delivers
    // them means a libusb that is already broken, and hanging the platform thread on
    // top of that helps nobody.
    if (!transfer_cv_.wait_for(lock, std::chrono::seconds(2),
                               [this]() { return in_flight_ <= 0; })) {
      CP_LOG(info) << "[Dongle] " << in_flight_ << " read(s) never came back cancelled";
    }
  }
  if (in_flight_ <= 0) {
    for (libusb_transfer* transfer : transfers_) {
      libusb_free_transfer(transfer);
    }
    buffers_.clear();
  } else {
    // Leaked, buffers and all: freeing memory libusb may still write into is worse
    // than a few hundred kilobytes.
    new std::vector<std::vector<uint8_t>>(std::move(buffers_));
    buffers_.clear();
  }
  transfers_.clear();
  in_flight_ = 0;

  libusb_release_interface(handle_, 0);
  CloseAdapter(handle_);
  handle_ = nullptr;
  std::lock_guard<std::mutex> lock(description_mutex_);
  CP_LOG(info) << "[Dongle] closed " << description_;
  description_.clear();
}

std::string UsbLink::description() const {
  std::lock_guard<std::mutex> lock(description_mutex_);
  return description_;
}

uint64_t UsbLink::skipped_bytes() const { return parser_.skipped_bytes(); }

}  // namespace dongle
}  // namespace cp
