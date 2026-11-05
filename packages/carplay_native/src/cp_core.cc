// SPDX-License-Identifier: GPL-3.0-or-later
// The C ABI in cp_core.h, and the objects behind one CpSession.
//
// What lives here is the part every backend shares: the texture, the decoder, the
// speakers, the microphone and the event bus. What talks to the phone is a backend
// behind them, today the Carlinkit adapter in dongle/. The decoder, the audio and the
// microphone outlive every connection and every start, like android_auto's, because
// they belong to the head unit rather than to whichever phone is plugged in.

#include "cp_core.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "audio/audio_input.h"
#include "audio/audio_output.h"
#include "dongle/dongle_session.h"
#include "dongle/usb_link.h"
#include "event_bus.h"
#include "frame_ring.h"
#include "frame_size.h"
#include "present/gl_adapter.h"
#include "test_pattern.h"
#include "video/video_decoder.h"

namespace {

constexpr int32_t kDefaultDpi = 160;
constexpr int32_t kDefaultFps = 60;
constexpr int32_t kDefaultMediaDelayMs = 1000;

std::string CopyOrEmpty(const char* text) { return text == nullptr ? std::string() : text; }

char* Duplicate(const std::string& text) { return strdup(text.c_str()); }

std::string DeviceListing(const std::vector<cp::PcmDevice>& devices) {
  std::string listing;
  for (const cp::PcmDevice& device : devices) {
    listing += device.name;
    listing += '\t';
    listing += device.description;
    listing += '\t';
    listing += device.is_default ? '1' : '0';
    listing += '\n';
  }
  return listing;
}

bool ValidStream(int32_t stream) { return stream >= 0 && stream < cp::kAudioStreamCount; }

}  // namespace

struct CpSession {
  explicit CpSession(CpEventCallback on_event) : events(on_event) {}

  cp::EventBus events;
  cp::dongle::SessionConfig config;
  // Whether the host app named a frame size, or left it to follow the view.
  bool named_size = false;
  // Whether a view that changes size while running gets a new frame, which costs a
  // reconnect of the phone.
  bool follow_view_resize = true;

  std::mutex view_mutex;
  double view_width = 0;
  double view_height = 0;

  cp::FrameRing ring;
  std::unique_ptr<cp::GlAdapter> gl;
  std::unique_ptr<cp::TestPattern> pattern;
  std::shared_ptr<cp::VideoDecoder> decoder;
  // The instrument cluster's own pipeline, only when the config names a cluster screen.
  cp::FrameRing cluster_ring;
  std::unique_ptr<cp::GlAdapter> cluster_gl;
  std::shared_ptr<cp::VideoDecoder> cluster_decoder;
  std::shared_ptr<cp::AudioOutput> audio;
  std::shared_ptr<cp::AudioInput> microphone;
  std::unique_ptr<cp::dongle::DongleSession> dongle;

  std::atomic<CpAudioCallback> audio_callback{nullptr};
  std::atomic<CpMetadataCallback> metadata_callback{nullptr};
  bool running = false;

  // The frame to ask for: the named one, or the view's, see frame_size.h.
  cp::FrameSize FrameForView() {
    if (named_size) {
      return cp::EvenFrame(config.width, config.height);
    }
    std::lock_guard<std::mutex> lock(view_mutex);
    return cp::FrameSizeForView(view_width, view_height);
  }

  void ResolveFrameSize() {
    const cp::FrameSize frame = FrameForView();
    config.width = frame.width;
    config.height = frame.height;
  }

  int32_t PatternWidth() const {
    return config.width > 0 ? config.width : cp::kFallbackFrame.width;
  }
  int32_t PatternHeight() const {
    return config.height > 0 ? config.height : cp::kFallbackFrame.height;
  }
};

extern "C" {

void cp_string_free(char* message) { free(message); }

void cp_audio_buffer_free(uint8_t* data) { free(data); }

char* cp_audio_devices(void) { return Duplicate(DeviceListing(cp::ListPcmDevices())); }

char* cp_microphone_devices(void) {
  return Duplicate(DeviceListing(cp::ListPcmCaptureDevices()));
}

int32_t cp_adapter_present(void) { return cp::dongle::AdapterPresent() ? 1 : 0; }

CpSession* cp_session_create(const CpConfig* config, CpEventCallback on_event) {
  auto* session = new CpSession(on_event);
  cp::dongle::SessionConfig& resolved = session->config;
  if (config != nullptr) {
    session->named_size = config->width > 0 && config->height > 0;
    resolved.width = session->named_size ? config->width : 0;
    resolved.height = session->named_size ? config->height : 0;
    resolved.fps = config->fps > 0 ? std::clamp(config->fps, 20, 60) : kDefaultFps;
    resolved.dpi = config->dpi > 0 ? config->dpi : kDefaultDpi;
    const std::string name = CopyOrEmpty(config->name);
    if (!name.empty()) {
      resolved.box_name = name;
    }
    resolved.night_mode = config->night_mode != 0;
    resolved.right_hand_drive = config->right_hand_drive != 0;
    resolved.wifi_5ghz = config->wifi_band != 0;
    resolved.media_delay_ms =
        config->media_delay_ms > 0 ? config->media_delay_ms : kDefaultMediaDelayMs;
    switch (config->microphone) {
      case CP_MICROPHONE_ADAPTER:
        resolved.microphone = cp::dongle::MicSource::kAdapter;
        break;
      case CP_MICROPHONE_PHONE:
        resolved.microphone = cp::dongle::MicSource::kPhone;
        break;
      default:
        resolved.microphone = cp::dongle::MicSource::kHeadUnit;
        break;
    }
    resolved.audio_over_bluetooth = config->audio_over_bluetooth != 0;
    resolved.wireless = config->wireless != 0;
    resolved.fast_charge = config->fast_charge != 0;
    resolved.dashboard_info = config->dashboard_info >= 0 ? config->dashboard_info : 7;
    if (config->oem_icon_png != nullptr && config->oem_icon_png_size > 0) {
      resolved.oem_icon_png.assign(config->oem_icon_png,
                                   config->oem_icon_png + config->oem_icon_png_size);
    }
    resolved.oem_icon_label = CopyOrEmpty(config->oem_icon_label);
    session->follow_view_resize = config->follow_view_resize != 0;
    resolved.safe_left = std::max(0, config->safe_left);
    resolved.safe_top = std::max(0, config->safe_top);
    resolved.safe_right = std::max(0, config->safe_right);
    resolved.safe_bottom = std::max(0, config->safe_bottom);
    resolved.gps = config->gps != 0;
    if (config->cluster_width > 0 && config->cluster_height > 0) {
      resolved.cluster_width = config->cluster_width;
      resolved.cluster_height = config->cluster_height;
      resolved.cluster_fps =
          config->cluster_fps > 0 ? std::clamp(config->cluster_fps, 10, 60) : 30;
    }
  }

  session->ring.Configure(session->PatternWidth(), session->PatternHeight());
  session->gl = std::make_unique<cp::GlAdapter>(&session->ring);
  session->pattern = std::make_unique<cp::TestPattern>(
      &session->ring, [session]() { session->gl->NotifyFrameAvailable(); });

  session->decoder = std::make_shared<cp::VideoDecoder>(
      [session](const cp::Frame& frame, std::shared_ptr<void> keepalive) {
        // Real video supersedes the test pattern rather than fighting it for slots.
        if (session->pattern->running()) {
          session->pattern->Stop();
        }
        if (session->ring.PublishFrame(frame, std::move(keepalive))) {
          session->gl->NotifyFrameAvailable();
        }
      },
      [session](const std::string& message) {
        session->events.Emit(session->events.last_state(), message);
      },
      [session](bool live) {
        if (!live && !session->pattern->running()) {
          // A picture that has ended is taken off the texture as well as reported, for
          // a host app that draws the texture itself rather than through the view.
          session->ring.Reset();
          session->gl->Clear();
        }
        // The event is what makes the Dart side read cp_session_video_active again.
        session->events.Emit(session->events.last_state(),
                             live ? "The phone's picture is on screen."
                                  : "The phone's picture has ended.");
      });
  session->decoder->SetDmabufProbe([]() { return cp::GlAdapter::DmabufSupported(); });

  session->audio = std::make_shared<cp::AudioOutput>([session](const std::string& message) {
    session->events.Emit(session->events.last_state(), message);
  });
  session->microphone = std::make_shared<cp::AudioInput>([session](const std::string& message) {
    session->events.Emit(session->events.last_state(), message);
  });

  if (resolved.cluster_width > 0) {
    session->cluster_ring.Configure(resolved.cluster_width, resolved.cluster_height);
    session->cluster_gl = std::make_unique<cp::GlAdapter>(&session->cluster_ring);
    session->cluster_decoder = std::make_shared<cp::VideoDecoder>(
        [session](const cp::Frame& frame, std::shared_ptr<void> keepalive) {
          if (session->cluster_ring.PublishFrame(frame, std::move(keepalive))) {
            session->cluster_gl->NotifyFrameAvailable();
          }
        },
        [session](const std::string& message) {
          session->events.Emit(session->events.last_state(), "Cluster: " + message);
        },
        [session](bool live) {
          if (!live) {
            session->cluster_ring.Reset();
            session->cluster_gl->Clear();
          }
          session->events.Emit(session->events.last_state(),
                               live ? "The cluster picture is on screen."
                                    : "The cluster picture has ended.");
        });
    session->cluster_decoder->SetDmabufProbe(
        []() { return cp::GlAdapter::DmabufSupported(); });
  }

  cp::dongle::DongleSession::Hooks hooks;
  hooks.state = [session](cp::dongle::SessionState state, const std::string& message) {
    session->events.Emit(static_cast<CpState>(state), message);
  };
  hooks.news = [session](const std::string& message) {
    session->events.Emit(session->events.last_state(), message);
  };
  hooks.metadata = [session](cp::dongle::MetadataKind kind, const std::string& json) {
    // Read once: Dart can clear it from the platform thread while this runs.
    CpMetadataCallback callback = session->metadata_callback.load();
    if (callback != nullptr) {
      callback(static_cast<int32_t>(kind), strdup(json.c_str()));
    }
  };
  session->dongle = std::make_unique<cp::dongle::DongleSession>(
      std::move(hooks), session->decoder, session->cluster_decoder, session->audio,
      session->microphone);
  // CarPlay sends a keyframe when a stream starts and then hardly ever, so a decoder
  // that dropped its place asks for one rather than waiting.
  cp::dongle::DongleSession* dongle = session->dongle.get();
  session->decoder->SetKeyframeRequest([dongle]() { dongle->RequestKeyFrame(); });
  return session;
}

void cp_session_destroy(CpSession* session) {
  if (session == nullptr) {
    return;
  }
  cp_session_stop(session);
  // Close the bus before dropping anything else, so a straggling event cannot reach a
  // NativeCallable the Dart side is about to tear down.
  session->events.Close();
  session->metadata_callback = nullptr;
  session->decoder->SetKeyframeRequest(nullptr);
  session->dongle.reset();
  session->decoder->Stop();
  if (session->cluster_decoder) {
    session->cluster_decoder->Stop();
  }
  session->audio->SetTap(nullptr);
  session->audio->Stop();
  session->microphone->Stop();
  session->audio_callback = nullptr;
  session->pattern->Stop();
  session->gl.reset();
  session->cluster_gl.reset();
  delete session;
}

int32_t cp_session_start(CpSession* session) {
  if (session == nullptr) {
    return -1;
  }
  if (session->running) {
    return 0;
  }
  // Here rather than lazily on a producer thread: this call comes from Dart, which is
  // the platform thread, and that is where Flutter wants texture registration.
  if (!session->gl->Register()) {
    session->events.Emit(CP_STATE_ERROR,
                         "Could not register a Flutter texture. The plugin's GTK entry "
                         "point did not run.");
    return -2;
  }
  if (session->cluster_gl && !session->cluster_gl->Register()) {
    session->events.Emit(CP_STATE_ERROR, "Could not register the cluster's Flutter texture.");
    return -2;
  }
  session->ResolveFrameSize();
  session->decoder->Start();
  if (session->cluster_decoder) {
    session->cluster_decoder->Start();
  }
  session->audio->Start();
  session->running = true;
  session->events.Emit(session->events.last_state(),
                       "Asking the phone for " + std::to_string(session->config.width) + "x" +
                           std::to_string(session->config.height) + " at up to " +
                           std::to_string(session->config.fps) + " fps.");
  session->dongle->Start(session->config);
  return 0;
}

int32_t cp_session_stop(CpSession* session) {
  if (session == nullptr) {
    return -1;
  }
  if (!session->running) {
    return 0;
  }
  session->running = false;
  session->dongle->Stop();
  session->decoder->Flush();
  if (session->cluster_decoder) {
    session->cluster_decoder->Flush();
  }
  return 0;
}

int64_t cp_session_texture_id(CpSession* session) {
  if (session == nullptr || !session->gl) {
    return -1;
  }
  return session->gl->texture_id();
}

int32_t cp_session_video_active(CpSession* session) {
  if (session == nullptr) {
    return 0;
  }
  return session->decoder->live() || session->pattern->running() ? 1 : 0;
}

int32_t cp_session_video_width(CpSession* session) {
  if (session == nullptr) {
    return 0;
  }
  if (session->pattern->running() && !session->decoder->live()) {
    return session->PatternWidth();
  }
  return session->decoder->frame_width();
}

int32_t cp_session_video_height(CpSession* session) {
  if (session == nullptr) {
    return 0;
  }
  if (session->pattern->running() && !session->decoder->live()) {
    return session->PatternHeight();
  }
  return session->decoder->frame_height();
}

char* cp_session_video_backend(CpSession* session) {
  return Duplicate(session == nullptr ? std::string("none")
                                      : session->decoder->backend_name());
}

void cp_session_set_view_size(CpSession* session, double width, double height) {
  if (session == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(session->view_mutex);
    session->view_width = width;
    session->view_height = height;
  }
  // A running session started with the view's old size. The adapter only takes a new
  // one with a new Open, which drops the phone, so the session waits for the view to
  // hold still before asking. Called on every layout, so most calls change nothing.
  if (!session->running || session->named_size || !session->follow_view_resize) {
    return;
  }
  const cp::FrameSize frame = session->FrameForView();
  if (frame.width == session->config.width && frame.height == session->config.height) {
    return;
  }
  session->config.width = frame.width;
  session->config.height = frame.height;
  session->dongle->RequestFrameSize(frame.width, frame.height);
}

void cp_session_set_display_size(CpSession* session, int32_t width, int32_t height) {
  if (session == nullptr || !session->gl) {
    return;
  }
  session->gl->SetDisplaySize(width, height);
}

int32_t cp_session_send_touch(CpSession* session, int32_t action, int32_t action_index,
                              const CpTouchPoint* points, int32_t count) {
  if (session == nullptr || points == nullptr || count <= 0) {
    return -1;
  }
  // Positions are measured against the picture as decoded, which can differ from the
  // size asked for: the phone's first frames are sometimes a small boot screen.
  int32_t width = session->decoder->frame_width();
  int32_t height = session->decoder->frame_height();
  if (width <= 0 || height <= 0) {
    width = session->config.width;
    height = session->config.height;
  }
  static_assert(sizeof(CpTouchPoint) == sizeof(cp::dongle::TouchInput),
                "CpTouchPoint and TouchInput must stay the same shape");
  const auto* input = reinterpret_cast<const cp::dongle::TouchInput*>(points);
  return session->dongle->SendTouch(action, action_index, input, count, width, height) ? 0
                                                                                       : -1;
}

int32_t cp_session_send_command(CpSession* session, int32_t command) {
  if (session == nullptr || command < 0) {
    return -1;
  }
  return session->dongle->SendCommand(static_cast<uint32_t>(command)) ? 0 : -1;
}

int32_t cp_session_request_key_frame(CpSession* session) {
  if (session == nullptr) {
    return -1;
  }
  session->dongle->RequestKeyFrame();
  return 0;
}

int32_t cp_session_set_night_mode(CpSession* session, int32_t night) {
  if (session == nullptr) {
    return -1;
  }
  session->config.night_mode = night != 0;
  session->dongle->SetNightMode(night != 0);
  return 0;
}

int32_t cp_session_set_location(CpSession* session, double latitude, double longitude,
                                double altitude, double speed, double heading,
                                double accuracy, int64_t time_ms) {
  if (session == nullptr || !session->config.gps) {
    return -1;
  }
  cp::dongle::GnssFix fix;
  fix.latitude = latitude;
  fix.longitude = longitude;
  fix.altitude = altitude;
  fix.speed = speed;
  fix.heading = heading;
  fix.accuracy = accuracy;
  fix.time_ms = time_ms;
  session->dongle->SetLocation(fix);
  return 0;
}

int32_t cp_session_set_audio_volume(CpSession* session, int32_t stream, double volume) {
  if (session == nullptr || !ValidStream(stream)) {
    return -1;
  }
  session->audio->SetVolume(static_cast<cp::AudioStream>(stream), volume);
  return 0;
}

double cp_session_audio_volume(CpSession* session, int32_t stream) {
  if (session == nullptr || !ValidStream(stream)) {
    return 0.0;
  }
  return session->audio->Volume(static_cast<cp::AudioStream>(stream));
}

int32_t cp_session_set_audio_muted(CpSession* session, int32_t stream, int32_t muted) {
  if (session == nullptr || !ValidStream(stream)) {
    return -1;
  }
  session->audio->SetMuted(static_cast<cp::AudioStream>(stream), muted != 0);
  return 0;
}

int32_t cp_session_audio_muted(CpSession* session, int32_t stream) {
  if (session == nullptr || !ValidStream(stream)) {
    return 0;
  }
  return session->audio->Muted(static_cast<cp::AudioStream>(stream)) ? 1 : 0;
}

int32_t cp_session_set_audio_device(CpSession* session, const char* device) {
  if (session == nullptr) {
    return -1;
  }
  session->audio->SetDevice(CopyOrEmpty(device));
  return 0;
}

char* cp_session_audio_device(CpSession* session) {
  return Duplicate(session == nullptr ? std::string() : session->audio->device());
}

int32_t cp_session_set_audio_output_enabled(CpSession* session, int32_t enabled) {
  if (session == nullptr) {
    return -1;
  }
  session->audio->SetOutputEnabled(enabled != 0);
  return 0;
}

int32_t cp_session_audio_output_enabled(CpSession* session) {
  if (session == nullptr) {
    return 0;
  }
  return session->audio->output_enabled() ? 1 : 0;
}

int32_t cp_session_set_audio_callback(CpSession* session, CpAudioCallback on_audio) {
  if (session == nullptr) {
    return -1;
  }
  session->audio_callback = on_audio;
  if (on_audio == nullptr) {
    session->audio->SetTap(nullptr);
    return 0;
  }
  session->audio->SetTap([session](cp::AudioStream stream, const cp::PcmFormat& format,
                                   const uint8_t* data, size_t size) {
    CpAudioCallback callback = session->audio_callback.load();
    if (callback == nullptr || size == 0) {
      return;
    }
    // Copied: the callback is a NativeCallable.listener, delivered after this returns.
    auto* copy = static_cast<uint8_t*>(malloc(size));
    if (copy == nullptr) {
      return;
    }
    memcpy(copy, data, size);
    callback(static_cast<int32_t>(stream), copy, static_cast<int32_t>(size),
             format.sample_rate, format.channels);
  });
  return 0;
}

char* cp_session_audio_backend(CpSession* session) {
  return Duplicate(session == nullptr ? std::string("none") : session->audio->backend_name());
}

int64_t cp_session_audio_underruns(CpSession* session, int32_t stream) {
  if (session == nullptr || !ValidStream(stream)) {
    return 0;
  }
  return static_cast<int64_t>(session->audio->Underruns(static_cast<cp::AudioStream>(stream)));
}

int64_t cp_session_audio_dropped(CpSession* session, int32_t stream) {
  if (session == nullptr || !ValidStream(stream)) {
    return 0;
  }
  return static_cast<int64_t>(session->audio->Dropped(static_cast<cp::AudioStream>(stream)));
}

int64_t cp_session_audio_latency(CpSession* session, int32_t stream) {
  if (session == nullptr || !ValidStream(stream)) {
    return 0;
  }
  return session->audio->LatencyMicros(static_cast<cp::AudioStream>(stream));
}

int32_t cp_session_microphone_active(CpSession* session) {
  if (session == nullptr) {
    return 0;
  }
  return session->microphone->active() ? 1 : 0;
}

double cp_session_microphone_level(CpSession* session) {
  if (session == nullptr) {
    return 0.0;
  }
  return session->microphone->level();
}

int64_t cp_session_microphone_bytes(CpSession* session) {
  if (session == nullptr) {
    return 0;
  }
  return static_cast<int64_t>(session->microphone->bytes_captured());
}

int32_t cp_session_set_microphone_device(CpSession* session, const char* device) {
  if (session == nullptr) {
    return -1;
  }
  session->microphone->SetDevice(CopyOrEmpty(device));
  return 0;
}

char* cp_session_microphone_device(CpSession* session) {
  return Duplicate(session == nullptr ? std::string() : session->microphone->device());
}

char* cp_session_microphone_backend(CpSession* session) {
  return Duplicate(session == nullptr ? std::string("none")
                                      : session->microphone->backend_name());
}

int32_t cp_session_set_metadata_callback(CpSession* session, CpMetadataCallback on_metadata) {
  if (session == nullptr) {
    return -1;
  }
  session->metadata_callback = on_metadata;
  return 0;
}

char* cp_session_adapter_info(CpSession* session) {
  return Duplicate(session == nullptr ? std::string("{}") : session->dongle->AdapterInfo());
}

int64_t cp_session_cluster_texture_id(CpSession* session) {
  if (session == nullptr || !session->cluster_gl) {
    return -1;
  }
  return session->cluster_gl->texture_id();
}

int32_t cp_session_cluster_video_active(CpSession* session) {
  if (session == nullptr || !session->cluster_decoder) {
    return 0;
  }
  return session->cluster_decoder->live() ? 1 : 0;
}

int32_t cp_session_cluster_video_width(CpSession* session) {
  if (session == nullptr || !session->cluster_decoder) {
    return 0;
  }
  return session->cluster_decoder->frame_width();
}

int32_t cp_session_cluster_video_height(CpSession* session) {
  if (session == nullptr || !session->cluster_decoder) {
    return 0;
  }
  return session->cluster_decoder->frame_height();
}

void cp_session_set_cluster_display_size(CpSession* session, int32_t width,
                                         int32_t height) {
  if (session == nullptr || !session->cluster_gl) {
    return;
  }
  session->cluster_gl->SetDisplaySize(width, height);
}

int32_t cp_session_start_test_pattern(CpSession* session) {
  if (session == nullptr) {
    return -1;
  }
  if (!session->gl->Register()) {
    return -2;
  }
  // The size a phone would be asked for now, so a pattern shows what the projection
  // will look like in this view. A running session keeps the size it started with.
  if (!session->running) {
    session->ResolveFrameSize();
  }
  session->pattern->Start(session->PatternWidth(), session->PatternHeight(),
                          session->config.fps > 0 ? session->config.fps : kDefaultFps);
  session->events.Emit(session->events.last_state(), "Test pattern started.");
  return 0;
}

int32_t cp_session_stop_test_pattern(CpSession* session) {
  if (session == nullptr) {
    return -1;
  }
  if (!session->pattern->running()) {
    return 0;
  }
  session->pattern->Stop();
  // Only when the pattern is all there is: a phone whose video superseded it owns the
  // texture now.
  if (!session->decoder->live()) {
    session->ring.Reset();
    session->gl->Clear();
  }
  session->events.Emit(session->events.last_state(), "Test pattern stopped.");
  return 0;
}

}  // extern "C"
