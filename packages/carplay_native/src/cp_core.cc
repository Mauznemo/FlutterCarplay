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
#include "present/gl_adapter.h"
#include "test_pattern.h"
#include "video/video_decoder.h"

namespace {

constexpr int32_t kDefaultDpi = 160;
constexpr int32_t kDefaultFps = 60;
constexpr int32_t kDefaultMediaDelayMs = 1000;
// The frame a view is never asked to exceed. The adapter has 128 MB of memory and a
// fixed H.264 buffer, and the captures that worked topped out at 2400x960; a larger
// view gets a picture of its shape at this size, stretched.
constexpr int32_t kMaxFrameWidth = 2560;
constexpr int32_t kMaxFrameHeight = 1440;
// And never smaller than this, which is the smallest CarPlay screen in the wild.
constexpr int32_t kMinFrameWidth = 640;
constexpr int32_t kMinFrameHeight = 320;
// Before any view has a size.
constexpr int32_t kFallbackWidth = 1280;
constexpr int32_t kFallbackHeight = 720;

int32_t Even(double value) {
  const auto rounded = static_cast<int32_t>(std::lround(value));
  return rounded - (rounded % 2);
}

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

  std::mutex view_mutex;
  double view_width = 0;
  double view_height = 0;

  cp::FrameRing ring;
  std::unique_ptr<cp::GlAdapter> gl;
  std::unique_ptr<cp::TestPattern> pattern;
  std::shared_ptr<cp::VideoDecoder> decoder;
  std::shared_ptr<cp::AudioOutput> audio;
  std::shared_ptr<cp::AudioInput> microphone;
  std::unique_ptr<cp::dongle::DongleSession> dongle;

  std::atomic<CpAudioCallback> audio_callback{nullptr};
  std::atomic<CpMetadataCallback> metadata_callback{nullptr};
  bool running = false;

  // The frame to ask for: the named one, or the view's size rounded to even, scaled
  // into the adapter's limits keeping its shape.
  void ResolveFrameSize() {
    if (named_size) {
      config.width = Even(config.width);
      config.height = Even(config.height);
      return;
    }
    double width = 0;
    double height = 0;
    {
      std::lock_guard<std::mutex> lock(view_mutex);
      width = view_width;
      height = view_height;
    }
    if (width < 1 || height < 1) {
      config.width = kFallbackWidth;
      config.height = kFallbackHeight;
      return;
    }
    const double shrink = std::min({1.0, kMaxFrameWidth / width, kMaxFrameHeight / height});
    width *= shrink;
    height *= shrink;
    const double grow =
        std::max({1.0, kMinFrameWidth / width, kMinFrameHeight / height});
    width = std::min(width * grow, static_cast<double>(kMaxFrameWidth));
    height = std::min(height * grow, static_cast<double>(kMaxFrameHeight));
    config.width = Even(width);
    config.height = Even(height);
  }

  int32_t PatternWidth() const { return config.width > 0 ? config.width : kFallbackWidth; }
  int32_t PatternHeight() const {
    return config.height > 0 ? config.height : kFallbackHeight;
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
      std::move(hooks), session->decoder, session->audio, session->microphone);
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
  session->audio->SetTap(nullptr);
  session->audio->Stop();
  session->microphone->Stop();
  session->audio_callback = nullptr;
  session->pattern->Stop();
  session->gl.reset();
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
  session->ResolveFrameSize();
  session->decoder->Start();
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
  std::lock_guard<std::mutex> lock(session->view_mutex);
  session->view_width = width;
  session->view_height = height;
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
