// SPDX-License-Identifier: GPL-3.0-or-later
// Tests of src/dongle/stream_parser.cc: messages split across chunks at every possible
// boundary, several in one chunk, and garbage in front of a header.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "dongle/protocol.h"
#include "dongle/stream_parser.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                   #condition);                                             \
      ++failures;                                                           \
    }                                                                       \
  } while (false)

using namespace cp::dongle;

struct Seen {
  uint32_t type;
  std::vector<uint8_t> payload;
};

std::vector<uint8_t> Stream() {
  std::vector<uint8_t> stream;
  const std::vector<uint8_t> heartbeat = EncodeHeartbeat();
  std::vector<uint8_t> big(143868, 0x5A);  // album art sized, the ZLP trigger
  big[0] = 1;
  const std::vector<uint8_t> media = Encode(Type::kMediaData, big);
  const std::vector<uint8_t> command = EncodeCommand(Command::kRequestKeyFrame);
  stream.insert(stream.end(), heartbeat.begin(), heartbeat.end());
  stream.insert(stream.end(), media.begin(), media.end());
  stream.insert(stream.end(), command.begin(), command.end());
  return stream;
}

void CheckSeen(const std::vector<Seen>& seen) {
  CHECK(seen.size() == 3);
  if (seen.size() != 3) {
    return;
  }
  CHECK(seen[0].type == 0xAA && seen[0].payload.empty());
  CHECK(seen[1].type == 0x2A && seen[1].payload.size() == 143868);
  CHECK(seen[1].payload[0] == 1 && seen[1].payload[143867] == 0x5A);
  CHECK(seen[2].type == 0x08 && seen[2].payload.size() == 4 && seen[2].payload[0] == 12);
}

void TestChunkSizes() {
  const std::vector<uint8_t> stream = Stream();
  for (size_t chunk : {1u, 7u, 16u, 512u, 16384u, 65536u, 1000000u}) {
    std::vector<Seen> seen;
    StreamParser parser([&](uint32_t type, const uint8_t* payload, size_t size) {
      seen.push_back({type, std::vector<uint8_t>(payload, payload + size)});
    });
    for (size_t offset = 0; offset < stream.size(); offset += chunk) {
      const size_t size = std::min(chunk, stream.size() - offset);
      parser.Feed(stream.data() + offset, size);
    }
    CheckSeen(seen);
    CHECK(parser.skipped_bytes() == 0);
  }
}

void TestGarbage() {
  std::vector<uint8_t> stream = {0x01, 0x02, 0xAA, 0x55, 0x00};
  const std::vector<uint8_t> rest = Stream();
  stream.insert(stream.end(), rest.begin(), rest.end());
  std::vector<Seen> seen;
  StreamParser parser([&](uint32_t type, const uint8_t* payload, size_t size) {
    seen.push_back({type, std::vector<uint8_t>(payload, payload + size)});
  });
  parser.Feed(stream.data(), stream.size());
  CheckSeen(seen);
  CHECK(parser.skipped_bytes() == 5);
}

void TestReset() {
  const std::vector<uint8_t> stream = Stream();
  std::vector<Seen> seen;
  StreamParser parser([&](uint32_t type, const uint8_t* payload, size_t size) {
    seen.push_back({type, std::vector<uint8_t>(payload, payload + size)});
  });
  // Half of the large message, then a new link.
  parser.Feed(stream.data(), 5000);
  parser.Reset();
  seen.clear();
  parser.Feed(stream.data(), stream.size());
  CheckSeen(seen);
}

}  // namespace

int main() {
  TestChunkSizes();
  TestGarbage();
  TestReset();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
