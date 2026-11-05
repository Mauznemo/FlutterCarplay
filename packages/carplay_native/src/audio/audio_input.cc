// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_input.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../log.h"

namespace cp {
namespace {

// How much audio one buffer carries.
//
// 20 ms, which is what the adapter's own host app sends and what its echo canceller is
// tuned for: 640 bytes at 16 kHz mono, 320 at 8 kHz. android_auto used 32 ms; the
// latency difference is not audible, the canceller's frame size is the reason.
constexpr int64_t kBufferMicros = 20000;

// What shows up in a mixer's recording tab. The only place a person can see that this
// machine is listening, so it says who is listening rather than naming the process.
constexpr const char* kStreamLabel = "CarPlay microphone";

}  // namespace

PcmFormat MicrophoneFormat() {
  // Siri and CarPlay calls. The adapter names the rate it wants in its INPUT_CONFIG
  // audio command, and the session passes that on when it differs; this is the format
  // used when the adapter asks for the microphone before saying.
  return PcmFormat{16000, 1, 16};
}

AudioInput::AudioInput(LogHandler log) : log_(std::move(log)) {}

AudioInput::~AudioInput() { Stop(); }

void AudioInput::Start(const PcmFormat& format, BufferHandler on_buffer) {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  // A restart rather than a second thread. The phone can ask twice without closing in
  // between, and two threads on one microphone would interleave their buffers into
  // something no recogniser can read.
  StopLocked();
  if (!on_buffer) {
    return;
  }
  quit_ = false;
  active_ = true;
  thread_ = std::thread([this, format, on_buffer = std::move(on_buffer)]() {
    Run(format, on_buffer);
  });
}

void AudioInput::Stop() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  StopLocked();
}

void AudioInput::StopLocked() {
  quit_ = true;
  if (thread_.joinable()) {
    // The capture thread can be inside a blocking read, which returns once the server
    // has kBufferMicros of audio: a thirtieth of a second, not a wait a user notices.
    thread_.join();
  }
  thread_ = std::thread();
  active_ = false;
  level_ = 0.0;
}

void AudioInput::SetDevice(const std::string& device) {
  {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    if (device_ == device) {
      return;
    }
    device_ = device;
  }
  // Deliberately not reopening anything. This class only ever opens a device when the
  // phone has asked for one, so a change made while the microphone is live applies the
  // next time the phone asks rather than taking the device away mid sentence.
  Log(device.empty() ? "The microphone will use the system default input."
                     : "The microphone will use " + device + ".");
}

std::string AudioInput::device() const {
  std::lock_guard<std::mutex> lock(settings_mutex_);
  return device_;
}

std::string AudioInput::backend_name() const {
  std::lock_guard<std::mutex> lock(settings_mutex_);
  return backend_;
}

void AudioInput::Run(PcmFormat format, BufferHandler on_buffer) {
  std::string device;
  {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    device = device_;
  }

  std::unique_ptr<PcmSource> source = CreatePcmSource();
  std::string error;
  if (!source->Open(format, device, kStreamLabel, &error)) {
    Log("Could not open the microphone: " + error +
        ". The phone is listening and will hear nothing.");
    source = CreateSilentPcmSource();
    source->Open(format, device, kStreamLabel, nullptr);
  }
  {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    backend_ = source->backend_name();
  }
  CP_LOG(info) << "[Microphone] capturing at " << format.sample_rate << " Hz, "
                  << format.channels << " channel(s), "
                  << (device.empty() ? "default device" : device) << ", backend "
                  << source->backend_name();

  // Sized from the format rather than fixed, and rounded down to a whole frame: a read
  // that ends mid frame would hand the phone half a sample and shift every one after it.
  const int32_t frame_bytes = std::max(1, format.channels * (format.bits / 8));
  size_t buffer_bytes = static_cast<size_t>(
      static_cast<int64_t>(format.bytes_per_second()) * kBufferMicros / 1000000);
  buffer_bytes -= buffer_bytes % static_cast<size_t>(frame_bytes);
  buffer_bytes = std::max(buffer_bytes, static_cast<size_t>(frame_bytes));
  std::vector<uint8_t> buffer(buffer_bytes);

  uint64_t buffers = 0;
  while (!quit_.load()) {
    if (!source->Read(buffer.data(), buffer.size())) {
      // Not reopened here. The phone asked for a microphone and this machine no longer
      // has one, so saying so and stopping is honest; the next request opens it again.
      Log("The microphone stopped working, so nothing more will be captured.");
      break;
    }
    if (quit_.load()) {
      // Captured while the user was pressing stop. Sending it would put a fragment of
      // the room on the wire after the head unit said it had finished listening.
      break;
    }

    // Peak rather than RMS: this drives an indicator, where what matters is that the
    // needle moves when someone speaks, and a peak moves on the first syllable where an
    // average is still catching up.
    const auto* samples = reinterpret_cast<const int16_t*>(buffer.data());
    const size_t count = buffer.size() / sizeof(int16_t);
    int32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
      peak = std::max(peak, std::abs(static_cast<int32_t>(samples[i])));
    }
    level_ = static_cast<double>(peak) / 32768.0;

    bytes_captured_ += buffer.size();
    on_buffer(buffer.data(), buffer.size());

    if (++buffers == 1) {
      CP_LOG(debug) << "[Microphone] first buffer captured, " << buffer.size()
                       << " bytes";
    }
  }

  source->Close();
  active_ = false;
  level_ = 0.0;
  CP_LOG(info) << "[Microphone] stopped after " << buffers << " buffer(s)";
}

void AudioInput::Log(const std::string& message) {
  if (log_) {
    log_(message);
  }
}

}  // namespace cp
