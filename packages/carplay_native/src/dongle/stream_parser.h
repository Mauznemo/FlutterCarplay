// SPDX-License-Identifier: GPL-3.0-or-later
// Turns the adapter's bulk IN byte stream back into messages.
//
// The obvious reader, sixteen bytes for the header and then exactly the payload, is what
// the JavaScript hosts do, and it has two problems a native one can avoid. A request
// shorter than the packet the adapter sends is an overflow, not a short read. And the
// adapter never ends a transfer that is an exact multiple of 512 bytes with a zero
// length packet, so a reader waiting for the short packet that ends a large message
// waits until the adapter's watchdog gives up on it (docs/dongle-protocol.md, "The ZLP
// bug"). So the link reads in large fixed chunks, each a multiple of 512, and this
// stitches messages back together across them, whatever the boundaries.
//
// Not thread safe: it is fed from the one libusb event thread.

#ifndef CARPLAY_NATIVE_DONGLE_STREAM_PARSER_H_
#define CARPLAY_NATIVE_DONGLE_STREAM_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace cp {
namespace dongle {

class StreamParser {
 public:
  // One whole message. `payload` is valid only for the duration of the call.
  using MessageHandler =
      std::function<void(uint32_t type, const uint8_t* payload, size_t size)>;

  explicit StreamParser(MessageHandler on_message);

  // Appends `size` bytes and delivers every message they complete.
  void Feed(const uint8_t* data, size_t size);

  // Drops anything half received. Called when a link is opened, so a new adapter does
  // not start in the middle of the last one's message.
  void Reset();

  // Bytes skipped looking for a header since construction. Anything above zero means
  // the stream lost its framing at least once, which on a healthy link never happens.
  uint64_t skipped_bytes() const { return skipped_bytes_; }

 private:
  MessageHandler on_message_;
  std::vector<uint8_t> buffer_;
  // How much of the front of buffer_ has been consumed. Compacted lazily, so a run of
  // small messages inside one chunk is not a memmove each.
  size_t start_ = 0;
  uint64_t skipped_bytes_ = 0;
};

}  // namespace dongle
}  // namespace cp

#endif  // CARPLAY_NATIVE_DONGLE_STREAM_PARSER_H_
