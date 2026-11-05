// SPDX-License-Identifier: GPL-3.0-or-later
#include "protocol.h"

#include <cstdio>
#include <cstring>

namespace cp {
namespace dongle {

namespace {

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 24));
}

void PutF32(std::vector<uint8_t>& out, float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float is not 32 bits");
  std::memcpy(&bits, &value, sizeof(bits));
  PutU32(out, bits);
}

void PutBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
  if (data != nullptr && size > 0) {
    out.insert(out.end(), data, data + size);
  }
}

}  // namespace

PcmSpec PcmSpecForDecodeType(uint32_t decode_type) {
  switch (decode_type) {
    case 1:
    case 2:
      return {44100, 2};
    case 3:
      return {8000, 1};
    case 4:
      return {48000, 2};
    case 5:
      return {16000, 1};
    case 6:
      return {24000, 1};
    case 7:
      return {16000, 2};
    // Only one implementation lists it, and nothing has been seen to send it.
    case 8:
      return {48000, 1};
    default:
      return {};
  }
}

uint32_t DecodeTypeForMicrophone(int32_t sample_rate) {
  switch (sample_rate) {
    case 16000:
      return 5;
    case 8000:
      return 3;
    default:
      return 0;
  }
}

uint32_t ReadU32At(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

float ReadF32At(const uint8_t* data) {
  const uint32_t bits = ReadU32At(data);
  float value = 0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

HeaderStatus ParseHeader(const uint8_t* data, Header* header) {
  const uint32_t magic = ReadU32At(data);
  header->length = ReadU32At(data + 4);
  header->type = ReadU32At(data + 8);
  const uint32_t check = ReadU32At(data + 12);
  if (magic != kMagic) {
    return HeaderStatus::kBadMagic;
  }
  if (check != (header->type ^ 0xFFFFFFFFu)) {
    return HeaderStatus::kBadTypeCheck;
  }
  if (header->length > kMaxPayload) {
    return HeaderStatus::kTooLong;
  }
  return HeaderStatus::kOk;
}

std::vector<uint8_t> Encode(Type type, const uint8_t* payload, size_t size) {
  std::vector<uint8_t> out;
  out.reserve(kHeaderSize + size);
  const auto id = static_cast<uint32_t>(type);
  PutU32(out, kMagic);
  PutU32(out, static_cast<uint32_t>(size));
  PutU32(out, id);
  PutU32(out, id ^ 0xFFFFFFFFu);
  PutBytes(out, payload, size);
  return out;
}

std::vector<uint8_t> Encode(Type type, const std::vector<uint8_t>& payload) {
  return Encode(type, payload.data(), payload.size());
}

std::vector<uint8_t> EncodeOpen(const OpenParams& params) {
  std::vector<uint8_t> payload;
  payload.reserve(28);
  PutU32(payload, params.width);
  PutU32(payload, params.height);
  PutU32(payload, params.fps);
  PutU32(payload, params.format);
  PutU32(payload, params.packet_max);
  PutU32(payload, params.box_version);
  PutU32(payload, static_cast<uint32_t>(params.work_mode));
  return Encode(Type::kOpen, payload);
}

std::vector<uint8_t> EncodeCommand(uint32_t command) {
  std::vector<uint8_t> payload;
  PutU32(payload, command);
  return Encode(Type::kCommand, payload);
}

std::vector<uint8_t> EncodeCommand(Command command) {
  return EncodeCommand(static_cast<uint32_t>(command));
}

std::vector<uint8_t> EncodeHeartbeat() { return Encode(Type::kHeartbeat); }

std::vector<uint8_t> EncodeSendFile(const std::string& path, const uint8_t* content,
                                    size_t size) {
  std::vector<uint8_t> payload;
  payload.reserve(8 + path.size() + 1 + size);
  // The length counts the terminating NUL, which is sent.
  PutU32(payload, static_cast<uint32_t>(path.size() + 1));
  PutBytes(payload, reinterpret_cast<const uint8_t*>(path.data()), path.size());
  payload.push_back(0);
  PutU32(payload, static_cast<uint32_t>(size));
  PutBytes(payload, content, size);
  return Encode(Type::kSendFile, payload);
}

std::vector<uint8_t> EncodeSendFile(const std::string& path, uint32_t value) {
  std::vector<uint8_t> content;
  PutU32(content, value);
  return EncodeSendFile(path, content.data(), content.size());
}

std::vector<uint8_t> EncodeSendFile(const std::string& path, const std::string& text) {
  return EncodeSendFile(path, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::vector<uint8_t> EncodeSendFile(const std::string& path,
                                    const std::vector<uint32_t>& values) {
  std::vector<uint8_t> content;
  for (uint32_t value : values) {
    PutU32(content, value);
  }
  return EncodeSendFile(path, content.data(), content.size());
}

std::vector<uint8_t> EncodeBoxSettings(const std::string& json) {
  return Encode(Type::kBoxSettings, reinterpret_cast<const uint8_t*>(json.data()),
                json.size());
}

std::vector<uint8_t> EncodeMultiTouch(const std::vector<TouchPoint>& points) {
  std::vector<uint8_t> payload;
  payload.reserve(points.size() * 16);
  for (const TouchPoint& point : points) {
    PutF32(payload, point.x);
    PutF32(payload, point.y);
    PutU32(payload, point.action);
    PutU32(payload, point.id);
  }
  return Encode(Type::kMultiTouch, payload);
}

std::vector<uint8_t> EncodeMicrophone(uint32_t decode_type, const uint8_t* pcm,
                                      size_t size) {
  std::vector<uint8_t> payload;
  payload.reserve(12 + size);
  PutU32(payload, decode_type);
  PutF32(payload, 0.0f);
  PutU32(payload, static_cast<uint32_t>(AudioType::kMicrophone));
  PutBytes(payload, pcm, size);
  return Encode(Type::kAudioData, payload);
}

std::string SanitizeName(const std::string& name) {
  std::string out;
  for (char c : name) {
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    const bool digit = c >= '0' && c <= '9';
    if (letter || digit || c == ' ' || c == '-' || c == '_' || c == '.') {
      out.push_back(c);
    }
    if (out.size() == 16) {
      break;
    }
  }
  // A trailing space is legal and invisible, and a name made of nothing else is no
  // name at all.
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

bool ParseVideo(const uint8_t* payload, size_t size, VideoHeader* header) {
  if (size < kVideoHeaderSize) {
    return false;
  }
  header->width = ReadU32At(payload);
  header->height = ReadU32At(payload + 4);
  header->encoder_state = ReadU32At(payload + 8);
  header->pts_ms = ReadU32At(payload + 12);
  header->flags = ReadU32At(payload + 16);
  return true;
}

bool ParseAudio(const uint8_t* payload, size_t size, AudioPacket* packet) {
  if (size < 12) {
    return false;
  }
  packet->decode_type = ReadU32At(payload);
  packet->volume = ReadF32At(payload + 4);
  packet->audio_type = ReadU32At(payload + 8);
  const size_t rest = size - 12;
  if (rest == 0) {
    packet->kind = AudioPacket::Kind::kEmpty;
  } else if (rest == 1) {
    packet->kind = AudioPacket::Kind::kCommand;
    packet->command = payload[12];
  } else if (rest == 4) {
    packet->kind = AudioPacket::Kind::kVolume;
    packet->duration = ReadF32At(payload + 12);
  } else {
    packet->kind = AudioPacket::Kind::kPcm;
    packet->pcm = payload + 12;
    // Whole samples only. An odd length would shift every later sample by a byte.
    packet->pcm_size = rest & ~static_cast<size_t>(1);
  }
  return true;
}

bool ParsePlugged(const uint8_t* payload, size_t size, Plugged* plugged) {
  if (size < 4) {
    return false;
  }
  plugged->phone_type = static_cast<PhoneType>(ReadU32At(payload));
  plugged->has_wifi_flag = size >= 8;
  plugged->wireless = plugged->has_wifi_flag && ReadU32At(payload + 4) != 0;
  if (plugged->phone_type == PhoneType::kCarPlayWireless) {
    plugged->wireless = true;
  }
  return true;
}

bool ReadU32(const uint8_t* payload, size_t size, uint32_t* value) {
  if (size < 4) {
    return false;
  }
  *value = ReadU32At(payload);
  return true;
}

std::string ReadString(const uint8_t* payload, size_t size) {
  std::string text(reinterpret_cast<const char*>(payload), size);
  const size_t nul = text.find('\0');
  if (nul != std::string::npos) {
    text.resize(nul);
  }
  while (!text.empty() &&
         (text.back() == ' ' || text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

std::string TypeName(uint32_t type) {
  switch (static_cast<Type>(type)) {
    case Type::kOpen:
      return "Open";
    case Type::kPlugged:
      return "Plugged";
    case Type::kPhase:
      return "Phase";
    case Type::kUnplugged:
      return "Unplugged";
    case Type::kTouch:
      return "Touch";
    case Type::kVideoData:
      return "VideoData";
    case Type::kAudioData:
      return "AudioData";
    case Type::kCommand:
      return "Command";
    case Type::kBluetoothAddress:
      return "BluetoothAddress";
    case Type::kCarPlayModeChange:
      return "CarPlayModeChange";
    case Type::kBluetoothPin:
      return "BluetoothPin";
    case Type::kBluetoothDeviceName:
      return "BluetoothDeviceName";
    case Type::kWifiDeviceName:
      return "WifiDeviceName";
    case Type::kDisconnectPhone:
      return "DisconnectPhone";
    case Type::kBluetoothPairedList:
      return "BluetoothPairedList";
    case Type::kManufacturerInfo:
      return "ManufacturerInfo";
    case Type::kCloseDongle:
      return "CloseDongle";
    case Type::kMultiTouch:
      return "MultiTouch";
    case Type::kHiCarLink:
      return "HiCarLink";
    case Type::kBoxSettings:
      return "BoxSettings";
    case Type::kPeerBluetoothConnecting:
      return "PeerBluetoothConnecting";
    case Type::kPeerBluetoothConnected:
      return "PeerBluetoothConnected";
    case Type::kPeerBluetoothDisconnected:
      return "PeerBluetoothDisconnected";
    case Type::kGnssData:
      return "GnssData";
    case Type::kMediaData:
      return "MediaData";
    case Type::kPinCode:
      return "PinCode";
    case Type::kClusterVideoData:
      return "ClusterVideoData";
    case Type::kSendFile:
      return "SendFile";
    case Type::kHeartbeat:
      return "Heartbeat";
    case Type::kUpdateProgress:
      return "UpdateProgress";
    case Type::kUpdateState:
      return "UpdateState";
    case Type::kSoftwareVersion:
      return "SoftwareVersion";
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%02X", type);
  return buffer;
}

}  // namespace dongle
}  // namespace cp
