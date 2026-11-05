// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream_parser.h"

#include <cstring>
#include <utility>

#include "protocol.h"

namespace cp {
namespace dongle {

StreamParser::StreamParser(MessageHandler on_message) : on_message_(std::move(on_message)) {}

void StreamParser::Reset() {
  buffer_.clear();
  start_ = 0;
}

void StreamParser::Feed(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return;
  }
  buffer_.insert(buffer_.end(), data, data + size);

  for (;;) {
    const size_t available = buffer_.size() - start_;
    if (available < kHeaderSize) {
      break;
    }
    Header header;
    const HeaderStatus status = ParseHeader(buffer_.data() + start_, &header);
    if (status != HeaderStatus::kOk) {
      // Lost the framing. Step one byte and look again rather than throwing the chunk
      // away: the next header is usually a few hundred bytes on, and the chunk may well
      // hold it.
      ++start_;
      ++skipped_bytes_;
      continue;
    }
    if (available < kHeaderSize + header.length) {
      break;
    }
    const uint8_t* payload = buffer_.data() + start_ + kHeaderSize;
    start_ += kHeaderSize + header.length;
    on_message_(header.type, payload, header.length);
  }

  // Compact once the consumed front is most of the buffer, so it neither grows without
  // bound nor moves a large unfinished message on every chunk.
  if (start_ == buffer_.size()) {
    buffer_.clear();
    start_ = 0;
  } else if (start_ > 0 && start_ >= buffer_.size() / 2) {
    std::memmove(buffer_.data(), buffer_.data() + start_, buffer_.size() - start_);
    buffer_.resize(buffer_.size() - start_);
    start_ = 0;
  }
}

}  // namespace dongle
}  // namespace cp
