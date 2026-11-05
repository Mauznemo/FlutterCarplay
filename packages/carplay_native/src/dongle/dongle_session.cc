// SPDX-License-Identifier: GPL-3.0-or-later
#include "dongle_session.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <utility>

#include "../audio/audio_input.h"
#include "../audio/audio_output.h"
#include "../log.h"
#include "../metadata/json.h"
#include "../video/video_decoder.h"

namespace cp {
namespace dongle {

namespace {

// How often to look for an adapter that is not there yet.
constexpr int64_t kOpenRetryUs = 1000000;
// The firmware's own timer. Its watchdog fires at fifteen seconds without one, and
// trouble has been seen from ten, so two is the number every host uses.
constexpr int64_t kHeartbeatUs = 2000000;
// Between the messages of the start up sequence. Sending them all at once works for
// some hosts; the ones that were written against a capture space them like this.
constexpr int64_t kSequenceGapUs = 120000;
// After clearing a stale session, before configuring the adapter again.
constexpr int64_t kStaleSessionClearUs = 200000;
// After the sequence, before asking the adapter to reconnect the last phone.
constexpr int64_t kAutoConnectDelayUs = 1000000;
// With no phone by then, the adapter is put into pairing mode.
constexpr int64_t kPairDelayUs = 15000000;
// A connected CarPlay phone that has sent no picture by this long after Plugged is
// asked for a keyframe, and again every kKeyframeWatchUs until it does.
constexpr int64_t kFirstVideoGraceUs = 2000000;
constexpr int64_t kKeyframeWatchUs = 1000000;
// Each request restarts the phone's encoder, so they are rationed.
constexpr int64_t kKeyframeMinGapUs = 500000;
// How long a view must hold one size before the adapter is reconfigured for it. Each
// reconfiguration drops the phone, so a window being dragged to a new size must not
// cause one per frame.
constexpr int64_t kResizeSettleUs = 1000000;
// Touch movement is sent at most this often. The picture that comes back is at most
// 60 fps, so more samples cannot be seen, and a desktop mouse produces hundreds.
constexpr int64_t kMoveMinGapUs = 16000;

// Fixed paths on the adapter. Nothing outside this list is ever written: some paths
// start a firmware update, and there is no whitelist on the adapter's side.
constexpr const char* kDpiFile = "/tmp/screen_dpi";
constexpr const char* kAndroidWorkModeFile = "/etc/android_work_mode";
constexpr const char* kNightModeFile = "/tmp/night_mode";
constexpr const char* kHandDriveFile = "/tmp/hand_drive_mode";
constexpr const char* kChargeModeFile = "/tmp/charge_mode";
constexpr const char* kBoxNameFile = "/etc/box_name";
constexpr const char* kAirPlayConfFile = "/etc/airplay.conf";
constexpr const char* kOemIconFile = "/etc/oem_icon.png";

int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

const char* PhaseMeaning(uint32_t phase) {
  switch (phase) {
    case 0:
      return "the phone session ended";
    case 4:
      return "waiting for the phone to join the adapter's Wi-Fi";
    case 5:
      return "waiting for AirPlay";
    case 7:
      return "the phone is connecting";
    case 8:
      return "the phone is streaming";
    case 13:
      return "AirPlay negotiation failed, usually a view or safe area larger than the "
             "frame size";
    case 1001:
      return "the adapter reports a Bluetooth hardware error";
    case 1002:
      return "the adapter reports a Wi-Fi hardware error";
    default:
      return "no known meaning";
  }
}

const char* LinkStatusName(uint32_t id) {
  switch (static_cast<Command>(id)) {
    case Command::kSupportWifi:
      return "wifiSupported";
    case Command::kSupportAutoConnect:
      return "autoConnectSupported";
    case Command::kScanningDevices:
      return "scanning";
    case Command::kDeviceFound:
      return "deviceFound";
    case Command::kDeviceNotFound:
      return "deviceNotFound";
    case Command::kDeviceConnectFailed:
      return "connectFailed";
    case Command::kBluetoothConnected:
      return "bluetoothConnected";
    case Command::kBluetoothDisconnected:
      return "bluetoothDisconnected";
    case Command::kWifiConnected:
      return "wifiConnected";
    case Command::kWifiDisconnected:
      return "wifiDisconnected";
    case Command::kBluetoothPairStart:
      return "pairing";
    default:
      return nullptr;
  }
}

bool LooksLikeImage(const uint8_t* data, size_t size) {
  if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    return true;
  }
  return size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
}

}  // namespace

DongleSession::DongleSession(Hooks hooks, std::shared_ptr<VideoDecoder> decoder,
                             std::shared_ptr<AudioOutput> audio,
                             std::shared_ptr<AudioInput> microphone)
    : hooks_(std::move(hooks)),
      decoder_(std::move(decoder)),
      audio_(std::move(audio)),
      microphone_(std::move(microphone)),
      link_(
          [this](uint32_t type, const uint8_t* payload, size_t size) {
            OnMessage(type, payload, size);
          },
          [this](const std::string& reason) {
            std::lock_guard<std::mutex> lock(mutex_);
            link_failed_ = true;
            failure_ = reason;
            cv_.notify_all();
          }) {}

DongleSession::~DongleSession() { Stop(); }

void DongleSession::Start(const SessionConfig& config) {
  if (running_.exchange(true)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = false;
    link_failed_ = false;
    scheduled_.clear();
    tasks_.clear();
  }
  next_open_us_ = 0;
  last_open_error_.clear();
  EmitState(SessionState::kSearching, "Looking for a Carlinkit adapter on USB.");
  thread_ = std::thread([this]() { Run(); });
}

void DongleSession::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
  EmitState(SessionState::kIdle);
}

void DongleSession::Run() {
  for (;;) {
    std::vector<std::function<void()>> tasks;
    bool failed = false;
    std::string failure;
    int32_t resize_width = 0;
    int32_t resize_height = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
        return quit_ || link_failed_ || !tasks_.empty();
      });
      if (quit_) {
        break;
      }
      tasks.swap(tasks_);
      failed = link_failed_;
      failure = failure_;
      link_failed_ = false;
      if (resize_due_us_ != 0 && NowUs() >= resize_due_us_) {
        resize_due_us_ = 0;
        resize_width = resize_width_;
        resize_height = resize_height_;
      }
    }

    if (resize_width > 0) {
      {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_.width = resize_width;
        config_.height = resize_height;
      }
      if (link_.open()) {
        EmitNews("The view changed size, asking the phone for " +
                 std::to_string(resize_width) + "x" + std::to_string(resize_height) + ".");
        PhoneGone("the view changed size");
        OnLinkOpened(NowUs());
      }
    }

    for (auto& task : tasks) {
      task();
    }

    const int64_t now = NowUs();
    if (failed && link_.open()) {
      CloseLink(failure, false);
      EmitState(SessionState::kSearching, failure + " Looking for it again.");
      next_open_us_ = now + kOpenRetryUs;
    }

    if (!link_.open()) {
      if (now < next_open_us_) {
        continue;
      }
      next_open_us_ = now + kOpenRetryUs;
      // Reset before the link opens rather than after: reads start the moment it does,
      // and they would land in state that still belongs to the last adapter.
      routes_ = {};
      reconnecting_ = false;
      phase13_retried_ = false;
      phone_connected_ = false;
      got_video_ = false;
      {
        std::lock_guard<std::mutex> lock(info_mutex_);
        software_version_.clear();
        box_json_.clear();
        phone_json_.clear();
        link_status_.clear();
      }
      std::string error;
      if (link_.Open(&error)) {
        last_open_error_.clear();
        OnLinkOpened(now);
      } else if (error != last_open_error_) {
        // Once per distinct reason rather than once a second: a head unit started with
        // no adapter plugged in would otherwise log the same line forever.
        last_open_error_ = error;
        CP_LOG(info) << "[Dongle] " << error;
        EmitNews(error);
      }
      continue;
    }

    if (now >= next_heartbeat_us_) {
      link_.Send(EncodeHeartbeat());
      next_heartbeat_us_ = now + kHeartbeatUs;
    }

    for (;;) {
      std::vector<uint8_t> message;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scheduled_.empty() || scheduled_.front().due_us > now) {
          break;
        }
        message = std::move(scheduled_.front().message);
        scheduled_.pop_front();
      }
      link_.Send(std::move(message));
    }

    if (auto_connect_due_us_ != 0 && now >= auto_connect_due_us_) {
      auto_connect_due_us_ = 0;
      if (!phone_connected_.load()) {
        link_.Send(EncodeCommand(Command::kStartAutoConnect));
      }
    }
    if (pair_due_us_ != 0 && now >= pair_due_us_) {
      pair_due_us_ = 0;
      // Not while the adapter is reconnecting a phone it knows: pairing mode aborts that.
      if (!phone_connected_.load() && !reconnecting_.load()) {
        CP_LOG(info) << "[Dongle] no phone yet, putting the adapter into pairing mode";
        link_.Send(EncodeCommand(Command::kWifiPair));
      }
    }
    // A CarPlay phone that connected but has not shown a picture. The stream normally
    // starts with a keyframe on its own; this is for when it did not.
    if (phone_connected_.load() && carplay_phone_.load() && !got_video_.load() &&
        now - plugged_at_us_.load() > kFirstVideoGraceUs && now >= next_keyframe_watch_us_) {
      next_keyframe_watch_us_ = now + kKeyframeWatchUs;
      RequestKeyFrame();
    }
  }

  // Leaving: goodbye to the phone and the adapter, then everything the phone had open.
  if (link_.open()) {
    CloseLink("The session was stopped.", true);
  }
}

void DongleSession::OnLinkOpened(int64_t now) {
  SessionConfig config;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config = config_;
  }
  // The heartbeat starts before the configuration rather than after it: the adapter's
  // watchdog counts from the moment the link is up.
  next_heartbeat_us_ = now;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scheduled_.clear();
  }

  // The adapter keeps a phone session across a host that went away without saying
  // goodbye, and then ignores the next Open: no Plugged, no keyframe, a black screen.
  // So every link starts by ending whatever was there, and gives it a moment.
  link_.Send(Encode(Type::kDisconnectPhone));
  link_.Send(Encode(Type::kCloseDongle));
  {
    // The pause: a heartbeat scheduled kStaleSessionClearUs out, which the sequence
    // below queues up behind. Harmless twice, and it keeps the timing in one queue.
    std::lock_guard<std::mutex> lock(mutex_);
    scheduled_.push_back({now + kStaleSessionClearUs, EncodeHeartbeat()});
  }

  const std::string name = SanitizeName(config.box_name).empty()
                               ? std::string("Flutter")
                               : SanitizeName(config.box_name);

  // The order is the one docs/dongle-protocol.md section 4.2 settles on. Open resets the
  // view area to full screen, so anything about the view area has to follow it.
  Schedule(EncodeSendFile(kDpiFile, static_cast<uint32_t>(config.dpi)));
  // 0: this head unit speaks CarPlay through the adapter. The adapter resets this on
  // every disconnect, so it goes out on every start.
  Schedule(EncodeSendFile(kAndroidWorkModeFile, 0u));
  OpenParams open;
  open.width = static_cast<uint32_t>(config.width);
  open.height = static_cast<uint32_t>(config.height);
  open.fps = static_cast<uint32_t>(config.fps);
  open.work_mode = WorkMode::kCarPlay;
  Schedule(EncodeOpen(open));
  Schedule(EncodeBoxSettings(BoxSettingsJson()));
  Schedule(EncodeSendFile(kNightModeFile, config.night_mode ? 1u : 0u));
  Schedule(EncodeSendFile(kHandDriveFile, config.right_hand_drive ? 1u : 0u));
  Schedule(EncodeSendFile(kChargeModeFile, config.fast_charge ? 1u : 0u));
  Schedule(EncodeSendFile(kBoxNameFile, name));
  if (!config.oem_icon_png.empty()) {
    Schedule(EncodeSendFile(kOemIconFile, config.oem_icon_png.data(),
                            config.oem_icon_png.size()));
    std::string conf = "oemIconVisible = 1\nname = " + name +
                       "\nmodel = Magic-Car-Link-1.00\noemIconPath = " + kOemIconFile + "\n";
    const std::string label = SanitizeName(config.oem_icon_label);
    if (!label.empty()) {
      conf += "oemIconLabel = " + label + "\n";
    }
    Schedule(EncodeSendFile(kAirPlayConfFile, conf));
  }
  switch (config.microphone) {
    case MicSource::kHeadUnit:
      Schedule(EncodeCommand(Command::kUseCarMic));
      break;
    case MicSource::kAdapter:
      Schedule(EncodeCommand(Command::kUseBoxMic));
      break;
    case MicSource::kPhone:
      Schedule(EncodeCommand(Command::kUsePhoneMic));
      break;
  }
  // Takes effect after the adapter's next reboot, not now. Sent anyway so the setting
  // the host app chose is the one the adapter keeps.
  Schedule(EncodeCommand(config.wifi_5ghz ? Command::kUse5GhzWifi : Command::kUse24GhzWifi));
  Schedule(EncodeCommand(config.audio_over_bluetooth ? Command::kUseBluetoothAudio
                                                     : Command::kUseUsbAudio));
  if (config.wireless) {
    Schedule(EncodeCommand(Command::kSupportWifi));
  }

  int64_t sequence_end = now;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scheduled_.empty()) {
      sequence_end = scheduled_.back().due_us;
    }
  }
  auto_connect_due_us_ = config.wireless ? sequence_end + kAutoConnectDelayUs : 0;
  pair_due_us_ = config.wireless ? sequence_end + kPairDelayUs : 0;

  EmitState(SessionState::kWaitingForPhone,
            "Adapter found (" + link_.description() + "), waiting for a phone.");
}

void DongleSession::Schedule(std::vector<uint8_t> message) {
  std::lock_guard<std::mutex> lock(mutex_);
  const int64_t base = scheduled_.empty() ? NowUs() : scheduled_.back().due_us;
  scheduled_.push_back({base + kSequenceGapUs, std::move(message)});
}

void DongleSession::Post(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(task));
  }
  cv_.notify_all();
}

std::string DongleSession::BoxSettingsJson() const {
  SessionConfig config;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config = config_;
  }
  const std::string name = SanitizeName(config.box_name);
  const int channel = config.wifi_5ghz ? 36 : 1;
  Json json;
  json.AddInt("mediaDelay", std::max(500, config.media_delay_ms))
      .AddInt("syncTime", static_cast<int64_t>(std::time(nullptr)))
      .AddInt("mediaSound", 1)
      // Both spellings: one host sends the first and works, the firmware's own table
      // only lists the second.
      .AddInt("WiFiChannel", channel)
      .AddInt("wifiChannel", channel)
      .AddInt("autoConn", config.wireless ? 1 : 0)
      .AddInt("drivePosition", config.right_hand_drive ? 1 : 0)
      // 0 is "follow the night mode commands". The adapter's default is 2, forced
      // night, which would make SetNightMode do nothing.
      .AddInt("DayNightMode", 0)
      .AddInt("DashboardInfo", config.dashboard_info)
      .AddInt("ScreenDPI", config.dpi)
      .AddInt("UseBTPhone", 0)
      .AddInt("gps", 0)
      .AddInt("GNSSCapability", 0)
      .AddInt("fps", config.fps);
  if (!name.empty()) {
    json.AddString("boxName", name).AddString("btName", name).AddString("wifiName", name);
  }
  return json.Done();
}

void DongleSession::CloseLink(const std::string& reason, bool goodbye) {
  if (goodbye) {
    // Phone first, then the adapter, then a moment for the writer to get both out
    // before the link closes under it.
    link_.Send(Encode(Type::kDisconnectPhone));
    link_.Send(Encode(Type::kCloseDongle));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  link_.Close();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scheduled_.clear();
  }
  auto_connect_due_us_ = 0;
  pair_due_us_ = 0;
  PhoneGone(reason);
}

void DongleSession::PhoneGone(const std::string& why) {
  const bool was_connected = phone_connected_.exchange(false);
  carplay_phone_ = false;
  got_video_ = false;
  StopMicrophone();
  // Flushed whether or not a phone was connected: a link that died mid stream may
  // have left half a GOP queued.
  if (decoder_) {
    decoder_->Flush();
  }
  if (audio_) {
    for (int i = 0; i < kAudioStreamCount; ++i) {
      audio_->EndStream(static_cast<AudioStream>(i));
    }
  }
  {
    std::lock_guard<std::mutex> lock(info_mutex_);
    phone_json_.clear();
    plugged_ = Plugged{};
  }
  if (was_connected) {
    EmitMetadata(MetadataKind::kPhone, "{}");
    if (link_.open()) {
      EmitState(SessionState::kWaitingForPhone, "The phone disconnected (" + why + ").");
    }
  }
}

void DongleSession::StartMicrophone(uint32_t decode_type) {
  if (!microphone_ || !link_.open()) {
    return;
  }
  if (mic_running_ && mic_running_type_ == decode_type) {
    return;
  }
  const PcmSpec spec = PcmSpecForDecodeType(decode_type);
  if (spec.sample_rate != 8000 && spec.sample_rate != 16000) {
    CP_LOG(info) << "[Microphone] the adapter asked for decode type " << decode_type
                 << ", which is not one it accepts back. Using 16 kHz.";
    decode_type = 5;
  }
  const PcmSpec use = PcmSpecForDecodeType(decode_type);
  mic_running_ = true;
  mic_running_type_ = decode_type;
  microphone_->Start(PcmFormat{use.sample_rate, 1, 16},
                     [this, decode_type](const uint8_t* data, size_t size) {
                       link_.Send(EncodeMicrophone(decode_type, data, size));
                     });
}

void DongleSession::StopMicrophone() {
  if (!mic_running_) {
    return;
  }
  mic_running_ = false;
  mic_running_type_ = 0;
  if (microphone_) {
    microphone_->Stop();
  }
}

void DongleSession::OnMessage(uint32_t type, const uint8_t* payload, size_t size) {
  switch (static_cast<Type>(type)) {
    case Type::kVideoData: {
      VideoHeader header;
      if (!ParseVideo(payload, size, &header) || size == kVideoHeaderSize || !decoder_) {
        return;
      }
      // Seen against the fake adapter after a host restart: a frame still in flight for
      // the previous host arrives before Plugged. It belongs to no phone of ours, and
      // without its SPS it only makes the decoder complain.
      if (!phone_connected_.load()) {
        CP_LOG(debug) << "[Dongle] video before any phone, dropped";
        return;
      }
      if (!got_video_.exchange(true)) {
        CP_LOG(info) << "[Dongle] first video, " << header.width << "x" << header.height;
      }
      decoder_->Submit(payload + kVideoHeaderSize, size - kVideoHeaderSize, NowUs());
      return;
    }
    case Type::kAudioData:
      OnAudio(payload, size);
      return;
    case Type::kHeartbeat:
      return;
    case Type::kOpen:
      CP_LOG(info) << "[Dongle] the adapter answered Open";
      return;
    case Type::kPlugged: {
      Plugged plugged;
      if (ParsePlugged(payload, size, &plugged)) {
        OnPlugged(plugged);
      }
      return;
    }
    case Type::kUnplugged:
      // Event thread state, so reset here rather than in PhoneGone: the next phone's
      // first buffer reopens each stream it uses.
      routes_ = {};
      Post([this]() { PhoneGone("unplugged"); });
      return;
    case Type::kPhase: {
      uint32_t phase = 0;
      if (!ReadU32(payload, size, &phase)) {
        return;
      }
      CP_LOG(info) << "[Dongle] phase " << phase << ", " << PhaseMeaning(phase);
      if (phase == 0) {
        routes_ = {};
        Post([this]() { PhoneGone("the phone session ended"); });
      } else if (phase == 13) {
        EmitNews(std::string("CarPlay could not start: ") + PhaseMeaning(phase) + ".");
        // Other hosts found the phone attaches cleanly on a second attempt after the
        // adapter is told to drop everything and is configured again. Once per link, so
        // a configuration the phone really rejects does not loop.
        if (!phase13_retried_.exchange(true)) {
          Post([this]() {
            if (link_.open()) {
              PhoneGone("CarPlay negotiation failed");
              OnLinkOpened(NowUs());
            }
          });
        }
      } else {
        EmitNews(std::string("Phase ") + std::to_string(phase) + ": " +
                 PhaseMeaning(phase) + ".");
      }
      return;
    }
    case Type::kCommand: {
      uint32_t id = 0;
      if (ReadU32(payload, size, &id)) {
        OnCommand(id);
      }
      return;
    }
    case Type::kMediaData:
      OnMediaData(payload, size);
      return;
    case Type::kBoxSettings:
      OnBoxSettings(ReadString(payload, size));
      return;
    case Type::kSoftwareVersion: {
      std::lock_guard<std::mutex> lock(info_mutex_);
      software_version_ = ReadString(payload, size);
      CP_LOG(info) << "[Dongle] firmware " << software_version_;
      break;
    }
    case Type::kBluetoothAddress: {
      std::lock_guard<std::mutex> lock(info_mutex_);
      bluetooth_address_ = ReadString(payload, size);
      break;
    }
    case Type::kBluetoothDeviceName: {
      std::lock_guard<std::mutex> lock(info_mutex_);
      bluetooth_name_ = ReadString(payload, size);
      break;
    }
    case Type::kWifiDeviceName: {
      std::lock_guard<std::mutex> lock(info_mutex_);
      wifi_name_ = ReadString(payload, size);
      break;
    }
    case Type::kBluetoothPin:
    case Type::kPinCode: {
      std::lock_guard<std::mutex> lock(info_mutex_);
      pin_ = ReadString(payload, size);
      break;
    }
    case Type::kBluetoothPairedList: {
      std::lock_guard<std::mutex> lock(info_mutex_);
      paired_devices_ = ReadString(payload, size);
      break;
    }
    case Type::kClusterVideoData:
      if (!warned_cluster_video_) {
        warned_cluster_video_ = true;
        CP_LOG(info) << "[Dongle] cluster video arrived, which is not shown yet";
      }
      return;
    case Type::kUpdateProgress:
    case Type::kUpdateState: {
      uint32_t value = 0;
      ReadU32(payload, size, &value);
      // Nothing here starts an update, so one in progress was started by something
      // else, and is worth a line in the log before the adapter reboots under us.
      EmitNews("The adapter reports a firmware update (" + TypeName(type) + " " +
               std::to_string(value) + ").");
      return;
    }
    default:
      CP_LOG(debug) << "[Dongle] " << TypeName(type) << ", " << size << " byte(s), ignored";
      return;
  }
  // The info messages fall through to here.
  EmitAdapterInfo();
}

void DongleSession::OnPlugged(const Plugged& plugged) {
  const bool carplay = plugged.phone_type == PhoneType::kCarPlay ||
                       plugged.phone_type == PhoneType::kCarPlayWireless;
  {
    std::lock_guard<std::mutex> lock(info_mutex_);
    plugged_ = plugged;
  }
  carplay_phone_ = carplay;
  got_video_ = false;
  plugged_at_us_ = NowUs();
  phone_connected_ = true;
  CP_LOG(info) << "[Dongle] phone plugged, type " << static_cast<uint32_t>(plugged.phone_type)
               << (plugged.wireless ? ", wireless" : ", wired");
  Json json;
  json.AddInt("phoneType", static_cast<int64_t>(plugged.phone_type))
      .AddBool("wireless", plugged.wireless);
  EmitMetadata(MetadataKind::kPhone, json.Done());
  if (!carplay) {
    EmitNews("The adapter connected a phone that is not CarPlay (type " +
             std::to_string(static_cast<uint32_t>(plugged.phone_type)) +
             "). Only CarPlay is supported through the adapter.");
  }
  EmitState(SessionState::kConnected, plugged.wireless ? "An iPhone connected over Wi-Fi."
                                                       : "An iPhone connected over USB.");
}

void DongleSession::OnAudio(const uint8_t* payload, size_t size) {
  AudioPacket packet;
  if (!ParseAudio(payload, size, &packet)) {
    return;
  }
  switch (packet.kind) {
    case AudioPacket::Kind::kCommand:
      OnAudioCommand(packet);
      return;
    case AudioPacket::Kind::kVolume:
      // CarPlay's own ducking request. The head unit ducks media under the navigation
      // stream by itself, see audio_output.h, so this is only recorded.
      CP_LOG(debug) << "[Audio] phone asks for volume " << packet.volume << " over "
                    << packet.duration << " s on stream " << packet.audio_type;
      return;
    case AudioPacket::Kind::kEmpty:
      return;
    case AudioPacket::Kind::kPcm:
      break;
  }
  if (!audio_) {
    return;
  }
  const PcmSpec spec = PcmSpecForDecodeType(packet.decode_type);
  if (spec.sample_rate == 0) {
    CP_LOG(debug) << "[Audio] unknown decode type " << packet.decode_type << ", dropped";
    return;
  }
  AudioStream stream = AudioStream::kMedia;
  if (packet.audio_type == static_cast<uint32_t>(AudioType::kNavigation)) {
    stream = AudioStream::kNavigation;
  } else if (spec.channels == 1 && spec.sample_rate <= 24000) {
    stream = AudioStream::kVoice;
  }
  if (stream != AudioStream::kNavigation) {
    main_stream_ = static_cast<int32_t>(stream);
  }
  const PcmFormat format{spec.sample_rate, spec.channels, 16};
  StreamRoute& route = routes_[static_cast<size_t>(stream)];
  if (!route.begun || route.format != format) {
    route.begun = true;
    route.format = format;
    audio_->BeginStream(stream, format);
  }
  audio_->Submit(stream, packet.pcm, packet.pcm_size);
}

void DongleSession::OnAudioCommand(const AudioPacket& packet) {
  const auto command = static_cast<AudioCommand>(packet.command);
  const bool navigation = packet.audio_type == static_cast<uint32_t>(AudioType::kNavigation);
  auto end = [this](AudioStream stream) {
    StreamRoute& route = routes_[static_cast<size_t>(stream)];
    if (route.begun && audio_) {
      route.begun = false;
      audio_->EndStream(stream);
    }
  };
  CP_LOG(debug) << "[Audio] command " << static_cast<int>(packet.command) << " on stream "
                << packet.audio_type;
  switch (command) {
    case AudioCommand::kOutputStop:
    case AudioCommand::kMediaStop:
      end(navigation ? AudioStream::kNavigation : static_cast<AudioStream>(main_stream_));
      return;
    case AudioCommand::kNaviStop:
    case AudioCommand::kNaviComplete:
      end(AudioStream::kNavigation);
      return;
    case AudioCommand::kInputConfig: {
      // The format the microphone has to be in from now on.
      mic_decode_type_ = packet.decode_type;
      const uint32_t type = mic_decode_type_;
      Post([this, type]() {
        if (mic_running_ && mic_running_type_ != type) {
          StopMicrophone();
          StartMicrophone(type);
        }
      });
      return;
    }
    case AudioCommand::kPhoneCallStart:
    case AudioCommand::kSiriStart: {
      if (command == AudioCommand::kPhoneCallStart) {
        call_active_ = true;
      } else {
        siri_active_ = true;
      }
      const uint32_t type = mic_decode_type_;
      Post([this, type]() { StartMicrophone(type); });
      return;
    }
    case AudioCommand::kPhoneCallStop:
    case AudioCommand::kSiriStop: {
      if (command == AudioCommand::kPhoneCallStop) {
        call_active_ = false;
      } else {
        siri_active_ = false;
      }
      // A call can start a moment before Siri hands over to it, so the microphone stays
      // open while either still wants it.
      if (!call_active_ && !siri_active_) {
        Post([this]() { StopMicrophone(); });
        end(AudioStream::kVoice);
      }
      return;
    }
    default:
      return;
  }
}

void DongleSession::OnCommand(uint32_t id) {
  switch (static_cast<Command>(id)) {
    case Command::kStartRecordMic: {
      const uint32_t type = mic_decode_type_;
      Post([this, type]() { StartMicrophone(type); });
      return;
    }
    case Command::kStopRecordMic:
      Post([this]() { StopMicrophone(); });
      return;
    case Command::kRequestHostUi:
      EmitRequest("hostUi");
      return;
    case Command::kHideProjection:
      EmitRequest("hide");
      return;
    case Command::kRequestVideoFocus:
      EmitRequest("videoFocus");
      return;
    case Command::kReleaseVideoFocus:
      EmitRequest("videoFocusReleased");
      return;
    case Command::kRequestNaviFocus:
    case Command::kReleaseNaviFocus:
    case Command::kStartGnssReport:
    case Command::kStopGnssReport:
      CP_LOG(debug) << "[Dongle] command " << id;
      return;
    default:
      break;
  }
  switch (static_cast<Command>(id)) {
    case Command::kScanningDevices:
    case Command::kDeviceFound:
    case Command::kBluetoothConnected:
      reconnecting_ = true;
      break;
    default:
      break;
  }
  if (const char* status = LinkStatusName(id)) {
    {
      std::lock_guard<std::mutex> lock(info_mutex_);
      link_status_ = status;
    }
    CP_LOG(debug) << "[Dongle] link status " << status;
    EmitAdapterInfo();
    return;
  }
  CP_LOG(debug) << "[Dongle] command " << id << ", ignored";
}

void DongleSession::OnMediaData(const uint8_t* payload, size_t size) {
  uint32_t subtype = 0;
  if (!ReadU32(payload, size, &subtype)) {
    return;
  }
  const uint8_t* data = payload + 4;
  const size_t length = size - 4;
  switch (static_cast<MediaSubtype>(subtype)) {
    case MediaSubtype::kNowPlaying:
      EmitMetadata(MetadataKind::kNowPlaying, ReadString(data, length));
      return;
    case MediaSubtype::kAlbumArt:
    case MediaSubtype::kAlbumArtAlt: {
      // Raw JPEG on the firmware that was captured, base64 text on some others. Either
      // way it crosses as base64, encoded once, here.
      const std::string image = LooksLikeImage(data, length) ? Base64(data, length)
                                                             : ReadString(data, length);
      Json json;
      json.AddString("image", image);
      EmitMetadata(MetadataKind::kAlbumArt, json.Done());
      return;
    }
    case MediaSubtype::kCallStatus:
      EmitMetadata(MetadataKind::kCall, ReadString(data, length));
      return;
    case MediaSubtype::kNavigation:
      EmitMetadata(MetadataKind::kNavigation, ReadString(data, length));
      return;
    default:
      CP_LOG(debug) << "[Dongle] media data subtype " << subtype << ", ignored";
      return;
  }
}

void DongleSession::OnBoxSettings(const std::string& json) {
  // The same message type carries two things: the adapter describing itself at start
  // up, and the phone's model and OS once a session forms. Told apart by their keys.
  const bool phone = json.find("\"MDModel\"") != std::string::npos ||
                     json.find("\"MDLinkType\"") != std::string::npos;
  {
    std::lock_guard<std::mutex> lock(info_mutex_);
    if (phone) {
      phone_json_ = json;
    } else {
      box_json_ = json;
    }
  }
  if (phone) {
    Plugged plugged;
    {
      std::lock_guard<std::mutex> lock(info_mutex_);
      plugged = plugged_;
    }
    Json out;
    out.AddInt("phoneType", static_cast<int64_t>(plugged.phone_type))
        .AddBool("wireless", plugged.wireless)
        .AddString("details", json);
    EmitMetadata(MetadataKind::kPhone, out.Done());
  } else {
    EmitAdapterInfo();
  }
}

bool DongleSession::SendTouch(int32_t action, int32_t action_index, const TouchInput* points,
                              int32_t count, int32_t frame_width, int32_t frame_height) {
  if (!phone_connected_.load() || points == nullptr || count <= 0 || frame_width <= 0 ||
      frame_height <= 0) {
    return false;
  }
  if (action == 2) {
    const int64_t now = NowUs();
    if (now - last_move_us_.load() < kMoveMinGapUs) {
      return true;
    }
    last_move_us_ = now;
  }
  // The adapter takes at most five points.
  const int32_t used = std::min<int32_t>(count, 5);
  std::vector<TouchPoint> out;
  out.reserve(static_cast<size_t>(used));
  for (int32_t i = 0; i < used; ++i) {
    TouchPoint point;
    point.x = std::clamp(static_cast<float>(points[i].x) / static_cast<float>(frame_width),
                         0.0f, 1.0f);
    point.y = std::clamp(static_cast<float>(points[i].y) / static_cast<float>(frame_height),
                         0.0f, 1.0f);
    point.id = static_cast<uint32_t>(std::clamp(points[i].id, 0, 4));
    point.action = static_cast<uint32_t>(TouchAction::kMove);
    if (i == action_index) {
      if (action == 0 || action == 5) {
        point.action = static_cast<uint32_t>(TouchAction::kDown);
      } else if (action == 1 || action == 6) {
        point.action = static_cast<uint32_t>(TouchAction::kUp);
      }
    }
    out.push_back(point);
  }
  return link_.Send(EncodeMultiTouch(out));
}

bool DongleSession::SendCommand(uint32_t id) {
  if (!link_.open()) {
    return false;
  }
  return link_.Send(EncodeCommand(id));
}

void DongleSession::SetNightMode(bool night) {
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.night_mode = night;
  }
  if (link_.open()) {
    link_.Send(EncodeCommand(night ? Command::kStartNightMode : Command::kStopNightMode));
  }
}

void DongleSession::RequestFrameSize(int32_t width, int32_t height) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    resize_width_ = width;
    resize_height_ = height;
    resize_due_us_ = NowUs() + kResizeSettleUs;
  }
  cv_.notify_all();
}

void DongleSession::RequestKeyFrame() {
  if (!phone_connected_.load() || !carplay_phone_.load()) {
    return;
  }
  const int64_t now = NowUs();
  int64_t last = last_keyframe_request_us_.load();
  if (now - last < kKeyframeMinGapUs) {
    return;
  }
  if (!last_keyframe_request_us_.compare_exchange_strong(last, now)) {
    return;
  }
  CP_LOG(debug) << "[Dongle] asking the phone for a keyframe";
  link_.Send(EncodeCommand(Command::kRequestKeyFrame));
}

std::string DongleSession::AdapterInfo() const {
  std::lock_guard<std::mutex> lock(info_mutex_);
  Json json;
  json.AddBool("open", link_.open());
  if (!link_.description().empty()) {
    json.AddString("usb", link_.description());
  }
  if (!software_version_.empty()) {
    json.AddString("softwareVersion", software_version_);
  }
  if (!bluetooth_name_.empty()) {
    json.AddString("bluetoothName", bluetooth_name_);
  }
  if (!bluetooth_address_.empty()) {
    json.AddString("bluetoothAddress", bluetooth_address_);
  }
  if (!wifi_name_.empty()) {
    json.AddString("wifiName", wifi_name_);
  }
  if (!pin_.empty()) {
    json.AddString("pin", pin_);
  }
  if (!paired_devices_.empty()) {
    json.AddString("pairedDevices", paired_devices_);
  }
  if (!link_status_.empty()) {
    json.AddString("linkStatus", link_status_);
  }
  // As text rather than spliced in: it is the adapter's JSON, and one malformed byte
  // in it should cost the details, not the whole update.
  if (!box_json_.empty()) {
    json.AddString("details", box_json_);
  }
  return json.Done();
}

void DongleSession::EmitState(SessionState state, const std::string& message) {
  if (hooks_.state) {
    hooks_.state(state, message);
  }
}

void DongleSession::EmitNews(const std::string& message) {
  if (hooks_.news) {
    hooks_.news(message);
  }
}

void DongleSession::EmitMetadata(MetadataKind kind, const std::string& json) {
  if (hooks_.metadata && !json.empty()) {
    hooks_.metadata(kind, json);
  }
}

void DongleSession::EmitAdapterInfo() { EmitMetadata(MetadataKind::kAdapter, AdapterInfo()); }

void DongleSession::EmitRequest(const char* name) {
  CP_LOG(info) << "[Dongle] the phone asks: " << name;
  Json json;
  json.AddString("request", name);
  EmitMetadata(MetadataKind::kRequest, json.Done());
}

}  // namespace dongle
}  // namespace cp
