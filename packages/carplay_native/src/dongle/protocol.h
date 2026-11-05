// SPDX-License-Identifier: GPL-3.0-or-later
// The Carlinkit adapter's USB protocol, as bytes. No libusb, no threads, no state.
//
// Everything the head unit says to the adapter is built here, and everything the
// adapter says is taken apart here, so the rest of the native core never touches a byte
// offset. That also makes this the one file that can be tested without the hardware,
// which until an adapter is on the desk is the only kind of test there is. See
// docs/dongle-protocol.md for where each number comes from; none of them are measured
// on this project's own hardware yet.
//
// The wire format is little endian throughout. A message is a 16 byte header (magic,
// payload length, type, and the type's bitwise complement as a check) followed by the
// payload.

#ifndef CARPLAY_NATIVE_DONGLE_PROTOCOL_H_
#define CARPLAY_NATIVE_DONGLE_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cp {
namespace dongle {

constexpr uint32_t kMagic = 0x55AA55AA;
constexpr size_t kHeaderSize = 16;
// What the firmware itself refuses beyond, and so what a header claiming more is: noise.
constexpr uint32_t kMaxPayload = 1024 * 1024;

// The adapter's USB identity. The product id is a firmware setting, and units in the
// wild carry either.
constexpr uint16_t kVendorId = 0x1314;
constexpr uint16_t kProductIds[] = {0x1520, 0x1521};

// Message types. Only the ones this code sends or reads are named; the rest are logged
// by number.
enum class Type : uint32_t {
  kOpen = 0x01,
  kPlugged = 0x02,
  kPhase = 0x03,
  kUnplugged = 0x04,
  kTouch = 0x05,
  kVideoData = 0x06,
  kAudioData = 0x07,
  kCommand = 0x08,
  kBluetoothAddress = 0x0A,
  kCarPlayModeChange = 0x0B,
  // Inbound it carries the Bluetooth PIN. Outbound, empty, it asks for a keyframe.
  kBluetoothPin = 0x0C,
  kBluetoothDeviceName = 0x0D,
  kWifiDeviceName = 0x0E,
  kDisconnectPhone = 0x0F,
  kBluetoothPairedList = 0x12,
  kManufacturerInfo = 0x14,
  kCloseDongle = 0x15,
  kMultiTouch = 0x17,
  kHiCarLink = 0x18,
  kBoxSettings = 0x19,
  kPeerBluetoothConnecting = 0x23,
  kPeerBluetoothConnected = 0x24,
  kPeerBluetoothDisconnected = 0x25,
  kGnssData = 0x29,
  kMediaData = 0x2A,
  kPinCode = 0x2B,
  kClusterVideoData = 0x2C,
  kSendFile = 0x99,
  kHeartbeat = 0xAA,
  kUpdateProgress = 0xB1,
  kUpdateState = 0xBB,
  kSoftwareVersion = 0xCC,
};

// The ids a Command (0x08) message carries, both directions. Grouped by who sends them.
enum class Command : uint32_t {
  // From the adapter.
  kStartRecordMic = 1,
  kStopRecordMic = 2,
  kRequestHostUi = 3,
  kHideProjection = 14,
  kStartGnssReport = 18,
  kStopGnssReport = 19,
  kRequestVideoFocus = 500,
  kReleaseVideoFocus = 501,
  kRequestNaviFocus = 506,
  kReleaseNaviFocus = 507,

  // To the adapter, configuring it.
  kSiriButtonDown = 5,
  kSiriButtonUp = 6,
  kUseCarMic = 7,
  kUseBoxMic = 8,
  kRequestKeyFrame = 12,
  kStartNightMode = 16,
  kStopNightMode = 17,
  kUsePhoneMic = 21,
  kUseBluetoothAudio = 22,
  kUseUsbAudio = 23,
  kUse24GhzWifi = 24,
  kUse5GhzWifi = 25,

  // To the adapter, forwarded to the phone as keys.
  kButtonLeft = 100,
  kButtonRight = 101,
  kButtonUp = 102,
  kButtonDown = 103,
  kButtonSelectDown = 104,
  kButtonSelectUp = 105,
  kButtonBack = 106,
  kKnobLeft = 111,
  kKnobRight = 112,
  kHome = 200,
  kPlay = 201,
  kPause = 202,
  kPlayPause = 203,
  kNext = 204,
  kPrevious = 205,
  kAnswerCall = 300,
  kHangUpCall = 301,

  // Wireless pairing and connection status. From the adapter these are news, never a
  // reason to end the session: 1010 arrives every twelve seconds while idle.
  kSupportWifi = 1000,
  kSupportAutoConnect = 1001,
  kStartAutoConnect = 1002,
  kScanningDevices = 1003,
  kDeviceFound = 1004,
  kDeviceNotFound = 1005,
  kDeviceConnectFailed = 1006,
  kBluetoothConnected = 1007,
  kBluetoothDisconnected = 1008,
  kWifiConnected = 1009,
  kWifiDisconnected = 1010,
  kBluetoothPairStart = 1011,
  kWifiPair = 1012,
};

// What Plugged says the phone is.
enum class PhoneType : uint32_t {
  kUnknown = 0,
  kAndroidMirror = 1,
  kCarPlay = 3,
  kIphoneMirror = 4,
  kAndroidAuto = 5,
  kHiCar = 6,
  // Older firmware's way of saying wireless CarPlay. Newer firmware sends kCarPlay with
  // the wireless flag set instead.
  kCarPlayWireless = 8,
};

// The phoneWorkMode field of Open: which projection the adapter offers an iPhone.
enum class WorkMode : uint32_t {
  kCarPlay = 2,
  kAndroidAuto = 4,
};

// Single byte commands carried inside an AudioData message.
enum class AudioCommand : uint8_t {
  kOutputStart = 1,
  kOutputStop = 2,
  kInputConfig = 3,
  kPhoneCallStart = 4,
  kPhoneCallStop = 5,
  kNaviStart = 6,
  kNaviStop = 7,
  kSiriStart = 8,
  kSiriStop = 9,
  kMediaStart = 10,
  kMediaStop = 11,
  kAlertStart = 12,
  kAlertStop = 13,
  kIncomingCall = 14,
  kTurnByTurnStart = 15,
  kNaviComplete = 16,
};

// Which of the adapter's audio streams a buffer belongs to.
enum class AudioType : uint32_t {
  // Media, alerts, ringtone, Siri's voice and call audio.
  kMain = 1,
  // Navigation prompts and notification sounds, the stream media ducks under.
  kNavigation = 2,
  // Outbound only: the microphone.
  kMicrophone = 3,
};

// The subtypes of MediaData.
enum class MediaSubtype : uint32_t {
  kNowPlaying = 1,
  kAlbumArtAlt = 2,
  kAlbumArt = 3,
  kCallStatus = 100,
  kNavigation = 200,
  kNavigationIcon = 201,
};

// The decodeType numbers, which name a PCM format. Zero sample rate for one the table
// does not know.
struct PcmSpec {
  int32_t sample_rate = 0;
  int32_t channels = 0;
};
PcmSpec PcmSpecForDecodeType(uint32_t decode_type);
// The reverse, for the microphone: 5 for 16 kHz mono, 3 for 8 kHz mono, 0 otherwise.
uint32_t DecodeTypeForMicrophone(int32_t sample_rate);

// ---- the header ----

enum class HeaderStatus {
  kOk,
  kBadMagic,
  kBadTypeCheck,
  kTooLong,
};

struct Header {
  uint32_t length = 0;
  uint32_t type = 0;
};

// Reads the 16 bytes at `data`. `header` is filled in whatever the status, for logging.
HeaderStatus ParseHeader(const uint8_t* data, Header* header);

// ---- outbound ----

// A whole message, header and payload, ready for one bulk write.
std::vector<uint8_t> Encode(Type type, const uint8_t* payload = nullptr, size_t size = 0);
std::vector<uint8_t> Encode(Type type, const std::vector<uint8_t>& payload);

struct OpenParams {
  uint32_t width = 1280;
  uint32_t height = 720;
  uint32_t fps = 30;
  // 5 is the mode in which the adapter answers keyframe requests. Every host sends it.
  uint32_t format = 5;
  // Meaning unknown, and the same in every implementation.
  uint32_t packet_max = 49152;
  uint32_t box_version = 2;
  WorkMode work_mode = WorkMode::kCarPlay;
};
std::vector<uint8_t> EncodeOpen(const OpenParams& params);
std::vector<uint8_t> EncodeCommand(Command command);
std::vector<uint8_t> EncodeCommand(uint32_t command);
std::vector<uint8_t> EncodeHeartbeat();

// Writes `content` to `path` on the adapter's file system. There is no whitelist on the
// adapter's side, and some paths start a firmware update, so only the fixed paths in
// dongle_session.cc are ever passed here.
std::vector<uint8_t> EncodeSendFile(const std::string& path, const uint8_t* content,
                                    size_t size);
std::vector<uint8_t> EncodeSendFile(const std::string& path, uint32_t value);
std::vector<uint8_t> EncodeSendFile(const std::string& path, const std::string& text);
// Several little endian u32s, for the view and safe area files.
std::vector<uint8_t> EncodeSendFile(const std::string& path,
                                    const std::vector<uint32_t>& values);

// Settings the adapter keeps in its own configuration, as JSON.
std::vector<uint8_t> EncodeBoxSettings(const std::string& json);

// One touch point, in the video frame normalised to 0..1.
struct TouchPoint {
  float x = 0;
  float y = 0;
  // 0 up, 1 down, 2 move. Every finger down is in every report: the one that changed
  // carries down or up, the rest move.
  uint32_t action = 2;
  // A small reused slot, 0 to 4, not a Flutter pointer id.
  uint32_t id = 0;
};
enum class TouchAction : uint32_t { kUp = 0, kDown = 1, kMove = 2 };
std::vector<uint8_t> EncodeMultiTouch(const std::vector<TouchPoint>& points);

// One buffer of microphone PCM, s16le mono at the rate `decode_type` names.
std::vector<uint8_t> EncodeMicrophone(uint32_t decode_type, const uint8_t* pcm,
                                      size_t size);

// Text to keep in a name field: printable ASCII only, at most 16 characters. The
// adapter passes some of these to a shell unescaped, so anything else is dropped rather
// than trusted.
std::string SanitizeName(const std::string& name);

// ---- inbound ----

// The 20 bytes in front of the H.264 in VideoData and ClusterVideoData.
struct VideoHeader {
  uint32_t width = 0;
  uint32_t height = 0;
  // An encoder generation number rather than flags. Not the codec: the stream is
  // always H.264.
  uint32_t encoder_state = 0;
  uint32_t pts_ms = 0;
  uint32_t flags = 0;
};
constexpr size_t kVideoHeaderSize = 20;
// False if the payload is too short. The Annex B stream starts at kVideoHeaderSize.
bool ParseVideo(const uint8_t* payload, size_t size, VideoHeader* header);

struct AudioPacket {
  enum class Kind { kPcm, kCommand, kVolume, kEmpty };
  Kind kind = Kind::kEmpty;
  uint32_t decode_type = 0;
  float volume = 0;
  uint32_t audio_type = 0;
  // kCommand only.
  uint8_t command = 0;
  // kVolume only: how long the change to `volume` takes, in seconds.
  float duration = 0;
  // kPcm only, pointing into the payload passed in.
  const uint8_t* pcm = nullptr;
  size_t pcm_size = 0;
};
// False if the payload is shorter than the 12 byte audio header. What follows the
// header is told apart by length alone: 1 byte is a command, 4 a volume ramp, none is
// empty, anything else is PCM.
bool ParseAudio(const uint8_t* payload, size_t size, AudioPacket* packet);

struct Plugged {
  PhoneType phone_type = PhoneType::kUnknown;
  bool has_wifi_flag = false;
  bool wireless = false;
};
bool ParsePlugged(const uint8_t* payload, size_t size, Plugged* plugged);

// A payload that is one little endian u32: Phase, Command, MediaData's subtype.
bool ReadU32(const uint8_t* payload, size_t size, uint32_t* value);
uint32_t ReadU32At(const uint8_t* data);
float ReadF32At(const uint8_t* data);

// Text with its trailing NULs and whitespace cut off.
std::string ReadString(const uint8_t* payload, size_t size);

// "Open", "VideoData", or "0x2D" for one with no name here. For logs.
std::string TypeName(uint32_t type);

}  // namespace dongle
}  // namespace cp

#endif  // CARPLAY_NATIVE_DONGLE_PROTOCOL_H_
