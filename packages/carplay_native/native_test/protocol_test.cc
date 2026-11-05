// SPDX-License-Identifier: GPL-3.0-or-later
// Byte level tests of src/dongle/protocol.cc, the only part of the dongle path that can
// be checked without an adapter. The expected bytes are the published examples and
// captures in docs/dongle-protocol.md, so a mismatch here is a mismatch with what other
// hosts put on the wire.
//
// No test framework: one executable, a check macro, and a non zero exit on failure.
// Built and run by tools/native-test.sh and by CI.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dongle/protocol.h"

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

std::vector<uint8_t> Bytes(std::initializer_list<int> values) {
  std::vector<uint8_t> out;
  for (int value : values) {
    out.push_back(static_cast<uint8_t>(value));
  }
  return out;
}

void Append(std::vector<uint8_t>& out, const std::vector<uint8_t>& more) {
  out.insert(out.end(), more.begin(), more.end());
}

void Append(std::vector<uint8_t>& out, const std::string& text) {
  out.insert(out.end(), text.begin(), text.end());
}

using namespace cp::dongle;

void TestHeartbeat() {
  // The capture in usb_protocol.md.
  CHECK(EncodeHeartbeat() == Bytes({0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0xAA,
                                    0x00, 0x00, 0x00, 0x55, 0xFF, 0xFF, 0xFF}));
}

void TestKeyFrameRequest() {
  CHECK(Encode(Type::kBluetoothPin) ==
        Bytes({0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00,
               0xF3, 0xFF, 0xFF, 0xFF}));
  CHECK(EncodeCommand(Command::kRequestKeyFrame) ==
        Bytes({0xAA, 0x55, 0xAA, 0x55, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
               0xF7, 0xFF, 0xFF, 0xFF, 0x0C, 0x00, 0x00, 0x00}));
}

void TestSendFileDpi() {
  // The /tmp/screen_dpi = 160 example: name length counts the NUL.
  std::vector<uint8_t> expected = Bytes({0xAA, 0x55, 0xAA, 0x55, 0x1C, 0x00, 0x00, 0x00,
                                         0x99, 0x00, 0x00, 0x00, 0x66, 0xFF, 0xFF, 0xFF,
                                         0x10, 0x00, 0x00, 0x00});
  Append(expected, std::string("/tmp/screen_dpi"));
  Append(expected, Bytes({0x00, 0x04, 0x00, 0x00, 0x00, 0xA0, 0x00, 0x00, 0x00}));
  CHECK(EncodeSendFile("/tmp/screen_dpi", 160u) == expected);
}

void TestOpen() {
  OpenParams params;
  params.width = 1920;
  params.height = 720;
  params.fps = 60;
  const std::vector<uint8_t> message = EncodeOpen(params);
  CHECK(message.size() == kHeaderSize + 28);
  Header header;
  CHECK(ParseHeader(message.data(), &header) == HeaderStatus::kOk);
  CHECK(header.type == 0x01);
  CHECK(header.length == 28);
  const uint8_t* payload = message.data() + kHeaderSize;
  CHECK(ReadU32At(payload) == 1920);
  CHECK(ReadU32At(payload + 4) == 720);
  CHECK(ReadU32At(payload + 8) == 60);
  CHECK(ReadU32At(payload + 12) == 5);
  CHECK(ReadU32At(payload + 16) == 49152);
  CHECK(ReadU32At(payload + 20) == 2);
  CHECK(ReadU32At(payload + 24) == 2);
}

void TestHeaderRejects() {
  std::vector<uint8_t> message = EncodeHeartbeat();
  Header header;
  CHECK(ParseHeader(message.data(), &header) == HeaderStatus::kOk);

  std::vector<uint8_t> bad_magic = message;
  bad_magic[0] = 0x00;
  CHECK(ParseHeader(bad_magic.data(), &header) == HeaderStatus::kBadMagic);

  std::vector<uint8_t> bad_check = message;
  bad_check[12] = 0x00;
  CHECK(ParseHeader(bad_check.data(), &header) == HeaderStatus::kBadTypeCheck);

  std::vector<uint8_t> too_long = message;
  too_long[6] = 0x20;  // 2 MiB
  CHECK(ParseHeader(too_long.data(), &header) == HeaderStatus::kTooLong);
}

void TestMultiTouch() {
  const std::vector<uint8_t> message =
      EncodeMultiTouch({{0.5f, 0.25f, 1, 0}, {1.0f, 0.0f, 2, 1}});
  Header header;
  CHECK(ParseHeader(message.data(), &header) == HeaderStatus::kOk);
  CHECK(header.type == 0x17);
  CHECK(header.length == 32);
  const uint8_t* payload = message.data() + kHeaderSize;
  CHECK(ReadF32At(payload) == 0.5f);
  CHECK(ReadF32At(payload + 4) == 0.25f);
  CHECK(ReadU32At(payload + 8) == 1);
  CHECK(ReadU32At(payload + 12) == 0);
  CHECK(ReadF32At(payload + 16) == 1.0f);
  CHECK(ReadU32At(payload + 24) == 2);
  CHECK(ReadU32At(payload + 28) == 1);
}

void TestMicrophone() {
  const std::vector<uint8_t> pcm(640, 0x11);
  const std::vector<uint8_t> message = EncodeMicrophone(5, pcm.data(), pcm.size());
  Header header;
  CHECK(ParseHeader(message.data(), &header) == HeaderStatus::kOk);
  CHECK(header.type == 0x07);
  CHECK(header.length == 652);
  const uint8_t* payload = message.data() + kHeaderSize;
  CHECK(ReadU32At(payload) == 5);
  CHECK(ReadF32At(payload + 4) == 0.0f);
  CHECK(ReadU32At(payload + 8) == 3);
  CHECK(payload[12] == 0x11);
  CHECK(DecodeTypeForMicrophone(16000) == 5);
  CHECK(DecodeTypeForMicrophone(8000) == 3);
  CHECK(DecodeTypeForMicrophone(44100) == 0);
}

void TestAudioParsing() {
  // A command: navigation prompt starting on the navigation stream.
  std::vector<uint8_t> command = Bytes({4, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 6});
  AudioPacket packet;
  CHECK(ParseAudio(command.data(), command.size(), &packet));
  CHECK(packet.kind == AudioPacket::Kind::kCommand);
  CHECK(packet.command == static_cast<uint8_t>(AudioCommand::kNaviStart));
  CHECK(packet.audio_type == 2);

  // A volume ramp: 0.2 over half a second.
  std::vector<uint8_t> ramp = Bytes({4, 0, 0, 0});
  float volume = 0.2f;
  float duration = 0.5f;
  uint8_t bytes[4];
  std::memcpy(bytes, &volume, 4);
  ramp.insert(ramp.end(), bytes, bytes + 4);
  Append(ramp, Bytes({1, 0, 0, 0}));
  std::memcpy(bytes, &duration, 4);
  ramp.insert(ramp.end(), bytes, bytes + 4);
  CHECK(ParseAudio(ramp.data(), ramp.size(), &packet));
  CHECK(packet.kind == AudioPacket::Kind::kVolume);
  CHECK(std::fabs(packet.volume - 0.2f) < 1e-6f);
  CHECK(std::fabs(packet.duration - 0.5f) < 1e-6f);

  // 60 ms of 48 kHz stereo, the media packet size the captures show.
  std::vector<uint8_t> pcm = Bytes({4, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0});
  pcm.resize(12 + 11520, 0x7F);
  CHECK(ParseAudio(pcm.data(), pcm.size(), &packet));
  CHECK(packet.kind == AudioPacket::Kind::kPcm);
  CHECK(packet.pcm_size == 11520);
  CHECK(packet.pcm == pcm.data() + 12);
  const PcmSpec spec = PcmSpecForDecodeType(packet.decode_type);
  CHECK(spec.sample_rate == 48000 && spec.channels == 2);

  std::vector<uint8_t> empty = Bytes({4, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0});
  CHECK(ParseAudio(empty.data(), empty.size(), &packet));
  CHECK(packet.kind == AudioPacket::Kind::kEmpty);
  CHECK(!ParseAudio(empty.data(), 11, &packet));
}

void TestVideoParsing() {
  std::vector<uint8_t> video = Bytes({0x80, 0x07, 0, 0, 0xD0, 0x02, 0, 0, 3, 0, 0, 0,
                                      0x10, 0x27, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x67});
  VideoHeader header;
  CHECK(ParseVideo(video.data(), video.size(), &header));
  CHECK(header.width == 1920);
  CHECK(header.height == 720);
  CHECK(header.encoder_state == 3);
  CHECK(header.pts_ms == 10000);
  CHECK(video[kVideoHeaderSize + 4] == 0x67);
  CHECK(!ParseVideo(video.data(), 19, &header));
}

void TestPlugged() {
  Plugged plugged;
  std::vector<uint8_t> wired = Bytes({3, 0, 0, 0});
  CHECK(ParsePlugged(wired.data(), wired.size(), &plugged));
  CHECK(plugged.phone_type == PhoneType::kCarPlay);
  CHECK(!plugged.has_wifi_flag);
  CHECK(!plugged.wireless);

  std::vector<uint8_t> wireless = Bytes({3, 0, 0, 0, 1, 0, 0, 0});
  CHECK(ParsePlugged(wireless.data(), wireless.size(), &plugged));
  CHECK(plugged.wireless);

  std::vector<uint8_t> legacy = Bytes({8, 0, 0, 0});
  CHECK(ParsePlugged(legacy.data(), legacy.size(), &plugged));
  CHECK(plugged.wireless);
}

void TestStrings() {
  const std::string version = "2025.02.25.1521CAY";
  std::vector<uint8_t> padded(version.begin(), version.end());
  padded.resize(32, 0);
  CHECK(ReadString(padded.data(), padded.size()) == version);
  CHECK(SanitizeName("My \"Car\"; rm -rf /") == "My Car rm -rf");
  CHECK(SanitizeName("Flutter Head Unit 2026") == "Flutter Head Uni");
  CHECK(SanitizeName("   ") == "");
}

void TestNames() {
  CHECK(TypeName(0x06) == "VideoData");
  CHECK(TypeName(0x2D) == "0x2D");
}

}  // namespace

int main() {
  TestHeartbeat();
  TestKeyFrameRequest();
  TestSendFileDpi();
  TestOpen();
  TestHeaderRejects();
  TestMultiTouch();
  TestMicrophone();
  TestAudioParsing();
  TestVideoParsing();
  TestPlugged();
  TestStrings();
  TestNames();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
