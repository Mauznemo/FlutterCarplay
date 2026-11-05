// SPDX-License-Identifier: GPL-3.0-or-later
// A head unit session over a Carlinkit adapter: find it, configure it, keep it alive,
// and route what it sends to the decoder, the speakers and Dart.
//
// The adapter is the CarPlay receiver. It does the iPhone's authentication, Wi-Fi and
// Bluetooth itself and hands the host H.264 and PCM over USB, so this class is the
// whole of the "protocol" on the host's side: a start up sequence, a heartbeat, and a
// dispatcher. The other backend this plugin is planned to grow, native CarPlay, would
// sit beside this file and feed the same decoder and audio objects. See PLAN.md.
//
// Threads, and what each may do:
//
//   control thread (owned)  opens the link, writes the start up sequence and the
//                           heartbeat, runs the timers, and does anything that may
//                           block: opening and closing the microphone, tearing down.
//   libusb event thread     delivers every inbound message to OnMessage. Must never
//                           block, so anything slow is posted to the control thread.
//   Flutter platform thread Start, Stop and the input calls. Input only queues a write.
//
// Session state goes out through Hooks::state, and everything that is news without
// being a lifecycle change through Hooks::news, the same split android_auto makes.

#ifndef CARPLAY_NATIVE_DONGLE_DONGLE_SESSION_H_
#define CARPLAY_NATIVE_DONGLE_DONGLE_SESSION_H_

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../audio/pcm_sink.h"
#include "nmea.h"
#include "protocol.h"
#include "usb_link.h"

namespace cp {

class AudioInput;
class AudioOutput;
class VideoDecoder;

namespace dongle {

// Which microphone the phone listens through.
enum class MicSource : int32_t {
  // The head unit's, captured here and sent over USB. The CPC200-CCPA has no
  // microphone of its own, so this is the only one that works on it.
  kHeadUnit = 0,
  kAdapter = 1,
  kPhone = 2,
};

// What the adapter is told at start up. Resolved by the caller: no zeros meaning
// "choose", every field is the value to send.
struct SessionConfig {
  int32_t width = 1280;
  int32_t height = 720;
  int32_t fps = 60;
  int32_t dpi = 160;
  std::string box_name = "Flutter";
  bool night_mode = false;
  bool right_hand_drive = false;
  bool wifi_5ghz = true;
  int32_t media_delay_ms = 1000;
  MicSource microphone = MicSource::kHeadUnit;
  // Phone audio to the car's own Bluetooth instead of over USB to this machine.
  bool audio_over_bluetooth = false;
  // Whether the adapter may pair and reconnect phones over Bluetooth and Wi-Fi.
  bool wireless = true;
  bool fast_charge = true;
  // The adapter's DashboardInfo bits: 1 now playing, 2 vehicle status, 4 route
  // guidance. Navigation metadata needs the 4.
  int32_t dashboard_info = 7;
  // The "back to the car" icon CarPlay shows, as PNG. Empty for none, in which case
  // CarPlay shows the adapter's own.
  std::vector<uint8_t> oem_icon_png;
  std::string oem_icon_label;
  // Pixels of the frame, per side, CarPlay keeps its controls out of while its wallpaper
  // still fills the whole frame. All zero for none.
  int32_t safe_left = 0;
  int32_t safe_top = 0;
  int32_t safe_right = 0;
  int32_t safe_bottom = 0;
  // Whether the car has a position to give the phone, through SetLocation.
  bool gps = false;
};

// One finger as Dart reports it: projected video pixels.
struct TouchInput {
  int32_t id = 0;
  int32_t x = 0;
  int32_t y = 0;
};

// Lifecycle values, the same integers as CpState in cp_core.h.
enum class SessionState : int32_t {
  kIdle = 0,
  kSearching = 1,
  kWaitingForPhone = 2,
  kConnected = 3,
  kError = 4,
};

// Which update a metadata hook carries, the same integers as CpMetadata in cp_core.h.
enum class MetadataKind : int32_t {
  kNowPlaying = 0,
  kAlbumArt = 1,
  kNavigation = 2,
  kCall = 3,
  kAdapter = 4,
  kPhone = 5,
  kRequest = 6,
};

class DongleSession {
 public:
  struct Hooks {
    std::function<void(SessionState state, const std::string& message)> state;
    std::function<void(const std::string& message)> news;
    std::function<void(MetadataKind kind, const std::string& json)> metadata;
  };

  DongleSession(Hooks hooks, std::shared_ptr<VideoDecoder> decoder,
                std::shared_ptr<AudioOutput> audio, std::shared_ptr<AudioInput> microphone);
  ~DongleSession();

  DongleSession(const DongleSession&) = delete;
  DongleSession& operator=(const DongleSession&) = delete;

  // Starts looking for an adapter, and configures it once found. Idempotent.
  void Start(const SessionConfig& config);

  // Says goodbye to the phone and the adapter and lets go of it. Idempotent. Returns
  // once the control thread has finished, so no hook runs after it.
  void Stop();

  bool running() const { return running_.load(); }
  bool phone_connected() const { return phone_connected_.load(); }

  // Touch, in projected video pixels. `frame_width` and `frame_height` are what the
  // pixels are measured against, the decoded picture's size. `action` and
  // `action_index` follow Android's MotionEvent, as the Dart view reports them: 0 down,
  // 1 up, 2 move, 5 another finger down, 6 one of several up. False when no phone is
  // connected, since a touch nobody receives is better refused than queued.
  bool SendTouch(int32_t action, int32_t action_index, const TouchInput* points,
                 int32_t count, int32_t frame_width, int32_t frame_height);

  // One Command message, which is how keys, Siri and media control reach the phone.
  bool SendCommand(uint32_t id);

  // Applies at once to a connected phone, and to every later one.
  void SetNightMode(bool night);

  // The car's position. Sent to the phone while it has asked for one (the adapter's
  // StartGNSSReport), at once and then once a second, and kept for the next phone.
  // Ignored unless SessionConfig::gps was set when the adapter was configured.
  void SetLocation(const GnssFix& fix);

  // Asks for a new frame size. Applied once no new size has been asked for in
  // kResizeSettleUs, by configuring the adapter again from Open, which drops a connected
  // phone and lets it reconnect into the new size. A wired iPhone is back within seconds.
  void RequestFrameSize(int32_t width, int32_t height);

  // Asks the phone for a keyframe, at most twice a second. CarPlay sends one when a
  // stream starts and then none for minutes, so this is what recovers a decoder that
  // lost its place. Ignored while no CarPlay phone is connected.
  void RequestKeyFrame();

  // What the adapter has said about itself, as the JSON the metadata hook sends.
  std::string AdapterInfo() const;

 private:
  struct Scheduled {
    int64_t due_us = 0;
    std::vector<uint8_t> message;
  };

  void Run();
  void OnLinkOpened(int64_t now_us);
  void CloseLink(const std::string& reason, bool goodbye);
  void Schedule(std::vector<uint8_t> message);
  void Post(std::function<void()> task);

  // libusb event thread.
  void OnMessage(uint32_t type, const uint8_t* payload, size_t size);
  void OnAudio(const uint8_t* payload, size_t size);
  void OnAudioCommand(const AudioPacket& packet);
  void OnCommand(uint32_t id);
  void OnMediaData(const uint8_t* payload, size_t size);
  void OnBoxSettings(const std::string& json);
  void OnPlugged(const Plugged& plugged);

  // Control thread.
  void PhoneGone(const std::string& why);
  void StartMicrophone(uint32_t decode_type);
  void StopMicrophone();

  void EmitState(SessionState state, const std::string& message = {});
  void EmitNews(const std::string& message);
  void EmitMetadata(MetadataKind kind, const std::string& json);
  void EmitAdapterInfo();
  void EmitRequest(const char* name);
  // Sends the latest fix if the phone wants one and the last went out at least
  // `min_gap_us` ago.
  void SendFix(int64_t min_gap_us);
  std::string BoxSettingsJson() const;

  Hooks hooks_;
  std::shared_ptr<VideoDecoder> decoder_;
  std::shared_ptr<AudioOutput> audio_;
  std::shared_ptr<AudioInput> microphone_;
  UsbLink link_;

  // Written by Start on the platform thread before the control thread exists, read by
  // the control thread afterwards. SetNightMode changes one field under the mutex.
  mutable std::mutex config_mutex_;
  SessionConfig config_;

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool quit_ = false;
  std::deque<Scheduled> scheduled_;
  std::vector<std::function<void()>> tasks_;
  std::string failure_;
  bool link_failed_ = false;

  std::atomic<bool> running_{false};
  std::atomic<bool> phone_connected_{false};
  std::atomic<bool> carplay_phone_{false};
  std::atomic<bool> got_video_{false};
  std::atomic<int64_t> plugged_at_us_{0};
  std::atomic<int64_t> last_keyframe_request_us_{0};
  std::atomic<int64_t> last_move_us_{0};
  // The adapter said it is looking for or reaching a phone it knows, so pairing mode
  // would get in its way.
  std::atomic<bool> reconnecting_{false};
  std::atomic<bool> phase13_retried_{false};
  // The phone asked for the car's position and has not said stop.
  std::atomic<bool> gnss_reporting_{false};
  std::mutex fix_mutex_;
  GnssFix fix_;
  bool has_fix_ = false;
  int64_t last_fix_sent_us_ = 0;

  // libusb event thread only.
  struct StreamRoute {
    bool begun = false;
    PcmFormat format;
  };
  std::array<StreamRoute, 3> routes_{};
  // Which of media and voice the main stream fed last, for its stop command.
  int32_t main_stream_ = 0;
  uint32_t mic_decode_type_ = 5;
  bool call_active_ = false;
  bool siri_active_ = false;
  bool warned_cluster_video_ = false;

  // Control thread only.
  bool mic_running_ = false;
  uint32_t mic_running_type_ = 0;
  int64_t next_open_us_ = 0;
  int64_t next_heartbeat_us_ = 0;
  int64_t auto_connect_due_us_ = 0;
  int64_t pair_due_us_ = 0;
  int64_t next_keyframe_watch_us_ = 0;
  // Written under mutex_ by RequestFrameSize, read by the control thread.
  int64_t resize_due_us_ = 0;
  int32_t resize_width_ = 0;
  int32_t resize_height_ = 0;
  std::string last_open_error_;

  // What the adapter said about itself. Written on the event thread, read anywhere.
  mutable std::mutex info_mutex_;
  std::string software_version_;
  std::string bluetooth_name_;
  std::string bluetooth_address_;
  std::string wifi_name_;
  std::string pin_;
  std::string paired_devices_;
  std::string box_json_;
  std::string phone_json_;
  std::string link_status_;
  Plugged plugged_;
};

}  // namespace dongle
}  // namespace cp

#endif  // CARPLAY_NATIVE_DONGLE_DONGLE_SESSION_H_
