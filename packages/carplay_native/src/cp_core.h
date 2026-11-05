// SPDX-License-Identifier: GPL-3.0-or-later
// Flat C ABI for the CarPlay head unit core.
//
// This is the only surface Dart binds to. Everything is either a plain scalar, an
// opaque pointer, or a NUL terminated UTF-8 string, so ffigen can generate bindings
// with no hand written glue. The same rules as android_auto's aa_core.h:
//
//   - no C++ types, no structs returned by value, no callbacks with non trivial types
//   - every call is safe from the Dart main isolate and returns promptly
//   - anything the core wants to tell Dart goes out through a callback, never through
//     out parameters
//
// After changing this file, regenerate the Dart bindings:
//
//   cd packages/carplay_native && dart run ffigen --config ffigen.yaml

#ifndef CARPLAY_NATIVE_CP_CORE_H_
#define CARPLAY_NATIVE_CP_CORE_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// The plugin library hides symbols by default, so the ABI has to opt back in.
#define CP_EXPORT __attribute__((visibility("default")))

typedef struct CpSession CpSession;

// Lifecycle states, mirrored by CarPlayConnectionState in Dart. Keep the two in step:
// the integers cross the FFI boundary as-is.
typedef enum {
  CP_STATE_IDLE = 0,
  // Looking for the adapter on USB.
  CP_STATE_SEARCHING = 1,
  // The adapter is configured and no phone is connected to it.
  CP_STATE_WAITING_FOR_PHONE = 2,
  // A phone is connected. The picture follows a moment later, see
  // cp_session_video_active.
  CP_STATE_CONNECTED = 3,
  CP_STATE_ERROR = 4,
} CpState;

// What a touch report describes, mirrored by CarPlayTouchAction in Dart. Android's
// MotionEvent numbering, the same as android_auto's, so one view widget's arithmetic
// serves both: the gaps are deliberate.
typedef enum {
  CP_TOUCH_DOWN = 0,
  CP_TOUCH_UP = 1,
  CP_TOUCH_MOVED = 2,
  CP_TOUCH_POINTER_DOWN = 5,
  CP_TOUCH_POINTER_UP = 6,
} CpTouchAction;

// One finger, in projected video pixels measured from the texture's top left corner.
// `id` is a small reused index, 0 to 4.
typedef struct {
  int32_t id;
  int32_t x;
  int32_t y;
} CpTouchPoint;

// Which of the three PCM sinks a call is about, mirrored by AudioStream in
// audio/audio_output.h and CarPlayAudioStream in Dart.
typedef enum {
  CP_AUDIO_STREAM_MEDIA = 0,
  // Siri and phone calls.
  CP_AUDIO_STREAM_VOICE = 1,
  // Turn by turn prompts. Media ducks under it.
  CP_AUDIO_STREAM_NAVIGATION = 2,
} CpAudioStream;

// Which microphone the phone listens through, mirrored by CarPlayMicrophone in Dart.
typedef enum {
  // This machine's, sent to the adapter over USB. The only one the CPC200-CCPA has.
  CP_MICROPHONE_HEAD_UNIT = 0,
  CP_MICROPHONE_ADAPTER = 1,
  CP_MICROPHONE_PHONE = 2,
} CpMicrophone;

// What a metadata update carries, mirrored by CarPlayMetadata in Dart. Every update is
// one JSON object.
typedef enum {
  // The phone's own now playing JSON, a partial update: MediaSongName, MediaArtistName,
  // MediaAlbumName, MediaAPPName, MediaSongDuration, MediaSongPlayTime,
  // MediaPlayStatus. Keys that did not change are left out.
  CP_METADATA_NOW_PLAYING = 0,
  // {"image": base64}, the cover of the current track.
  CP_METADATA_ALBUM_ART = 1,
  // The phone's navigation JSON, a partial update: NaviStatus, NaviManeuverType,
  // NaviRemainDistance, NaviRoadName, NaviDestinationName and the rest.
  CP_METADATA_NAVIGATION = 2,
  // The phone's call status JSON: CallStatus, CallDirection, CallName, CallNumber.
  CP_METADATA_CALL = 3,
  // What the adapter said about itself, built here: softwareVersion, bluetoothName,
  // bluetoothAddress, wifiName, pin, pairedDevices, linkStatus, and its own JSON as
  // text in `details`. The whole picture every time, not a partial update.
  CP_METADATA_ADAPTER = 4,
  // The connected phone: phoneType, wireless, and the adapter's JSON about it as text
  // in `details`. `{}` when the phone has gone.
  CP_METADATA_PHONE = 5,
  // Something the phone asks the head unit to do: {"request": "hostUi"} when the
  // driver taps the car's icon in CarPlay, "hide", "videoFocus", "videoFocusReleased".
  CP_METADATA_REQUEST = 6,
} CpMetadata;

// How the head unit describes itself. Zero in fps, dpi and media_delay_ms means the
// default; the flags are 0 or 1, and the ones that say so take -1 for the default.
typedef struct {
  // The frame the phone draws, in pixels. Both zero to follow the view, see
  // cp_session_set_view_size. Rounded to even.
  int32_t width;
  int32_t height;
  // Ceiling on the frame rate. CarPlay sends fewer when nothing on screen moves.
  int32_t fps;
  // Density the phone lays out for. Zero for 160.
  int32_t dpi;
  // Shown on the phone and used as the adapter's Bluetooth and Wi-Fi name. Plain
  // letters, digits and spaces, at most 16; anything else is dropped. NULL for
  // "Flutter".
  const char* name;
  int32_t night_mode;
  int32_t right_hand_drive;
  // 1 for 5 GHz Wi-Fi, 0 for 2.4 GHz, -1 for the default (5 GHz). The adapter applies
  // a change on its next reboot.
  int32_t wifi_band;
  // The adapter's own audio buffer, in milliseconds. Zero for 1000; it will not go
  // below 500.
  int32_t media_delay_ms;
  // A CpMicrophone.
  int32_t microphone;
  int32_t audio_over_bluetooth;
  // Whether the adapter pairs and reconnects phones without a cable. -1 for the
  // default (yes).
  int32_t wireless;
  // -1 for the default (fast charging on the phone port).
  int32_t fast_charge;
  // The adapter's DashboardInfo bits, -1 for the default 7: 1 now playing, 2 vehicle
  // status, 4 route guidance.
  int32_t dashboard_info;
  // The icon CarPlay shows for going back to the car's own interface, as PNG, and its
  // label. NULL for the adapter's. Copied, so the caller may free it after create.
  const uint8_t* oem_icon_png;
  int32_t oem_icon_png_size;
  const char* oem_icon_label;
  // Whether a view that changes size while running gets a frame of its new size. It
  // takes a new Open, which disconnects the phone for a few seconds, so a host app that
  // resizes often may prefer the old frame fitted into the new view. -1 for the default
  // (yes).
  int32_t follow_view_resize;
  // Pixels of the frame, per side, CarPlay keeps its controls out of while its wallpaper
  // fills the whole frame: a round bezel, a host app's own buttons over the corner. Each
  // is capped at a quarter of the frame. All zero for none.
  int32_t safe_left;
  int32_t safe_top;
  int32_t safe_right;
  int32_t safe_bottom;
  // 1 when the car will give the phone its position through cp_session_set_location.
  // The phone then stops using its own receiver, so set it only for a car that has one.
  int32_t gps;
  // A second screen for the instrument cluster, in pixels, which CarPlay fills with a
  // stream of its own into a second texture. Zero width for none.
  int32_t cluster_width;
  int32_t cluster_height;
  // Zero for 30.
  int32_t cluster_fps;
} CpConfig;

// State changes and news. `message` may be NULL; otherwise it is heap allocated and
// owned by the receiver, which frees it with cp_string_free.
typedef void (*CpEventCallback)(int32_t state, char* message);

// A copy of PCM on its way to the speakers, before volume. `data` is owned by the
// receiver, which frees it with cp_audio_buffer_free.
typedef void (*CpAudioCallback)(int32_t stream, uint8_t* data, int32_t size,
                                int32_t sample_rate, int32_t channels);

// One metadata update. `json` is owned by the receiver, freed with cp_string_free.
typedef void (*CpMetadataCallback)(int32_t kind, char* json);

CP_EXPORT void cp_string_free(char* message);
CP_EXPORT void cp_audio_buffer_free(uint8_t* data);

// Every audio output and input the machine has, one per line: name, tab, description,
// tab, 1 if it is the default. Free with cp_string_free.
CP_EXPORT char* cp_audio_devices(void);
CP_EXPORT char* cp_microphone_devices(void);

// Whether a Carlinkit adapter is on USB. Claims nothing, so it works beside a running
// session and without one.
CP_EXPORT int32_t cp_adapter_present(void);

CP_EXPORT CpSession* cp_session_create(const CpConfig* config, CpEventCallback on_event);
CP_EXPORT void cp_session_destroy(CpSession* session);

// Registers the texture and starts looking for the adapter. Must be called on the
// platform thread. 0 on success.
CP_EXPORT int32_t cp_session_start(CpSession* session);
// Says goodbye to the phone and the adapter and lets go of the USB device.
CP_EXPORT int32_t cp_session_stop(CpSession* session);

// The Flutter texture id, or -1 before the first start.
CP_EXPORT int64_t cp_session_texture_id(CpSession* session);
// Whether the texture holds a live picture: from the first decoded frame of a phone's
// stream until that phone goes, or the test pattern.
CP_EXPORT int32_t cp_session_video_active(CpSession* session);
// The size of the decoded picture, 0 while there is none.
CP_EXPORT int32_t cp_session_video_width(CpSession* session);
CP_EXPORT int32_t cp_session_video_height(CpSession* session);
// "VA-API", "software" or "none". Free with cp_string_free.
CP_EXPORT char* cp_session_video_backend(CpSession* session);

// The view's size in physical pixels. With no width and height in the config, the
// frame the phone is asked for is this, rounded to even and capped (frame_size.h). A
// view that changes size while running gets a new frame once it has held still for a
// second, which reconnects the phone, unless follow_view_resize is 0.
CP_EXPORT void cp_session_set_view_size(CpSession* session, double width, double height);
// The size the texture is drawn at, in physical pixels, so a picture drawn smaller is
// averaged down by the converter rather than sampled down by Flutter.
CP_EXPORT void cp_session_set_display_size(CpSession* session, int32_t width,
                                           int32_t height);

// Touch, in projected video pixels. -1 when no phone is connected.
CP_EXPORT int32_t cp_session_send_touch(CpSession* session, int32_t action,
                                        int32_t action_index, const CpTouchPoint* points,
                                        int32_t count);
// One Command id to the adapter: keys, Siri, media control. See CarPlayKey in Dart for
// the ids. -1 while no adapter is open.
CP_EXPORT int32_t cp_session_send_command(CpSession* session, int32_t command);
CP_EXPORT int32_t cp_session_request_key_frame(CpSession* session);
CP_EXPORT int32_t cp_session_set_night_mode(CpSession* session, int32_t night);

// The car's position. NaN for an altitude, speed, heading or accuracy the car does not
// know; `time_ms` is UTC milliseconds since the epoch, 0 for now. Sent while the phone
// asks for it, and kept for the next phone. -1 unless the config's gps was 1.
CP_EXPORT int32_t cp_session_set_location(CpSession* session, double latitude,
                                          double longitude, double altitude, double speed,
                                          double heading, double accuracy, int64_t time_ms);

CP_EXPORT int32_t cp_session_set_audio_volume(CpSession* session, int32_t stream,
                                              double volume);
CP_EXPORT double cp_session_audio_volume(CpSession* session, int32_t stream);
CP_EXPORT int32_t cp_session_set_audio_muted(CpSession* session, int32_t stream,
                                             int32_t muted);
CP_EXPORT int32_t cp_session_audio_muted(CpSession* session, int32_t stream);
// A device name from cp_audio_devices, or NULL or empty for the default.
CP_EXPORT int32_t cp_session_set_audio_device(CpSession* session, const char* device);
CP_EXPORT char* cp_session_audio_device(CpSession* session);
CP_EXPORT int32_t cp_session_set_audio_output_enabled(CpSession* session, int32_t enabled);
CP_EXPORT int32_t cp_session_audio_output_enabled(CpSession* session);
// NULL to stop. The tap runs on the audio writer threads.
CP_EXPORT int32_t cp_session_set_audio_callback(CpSession* session,
                                                CpAudioCallback on_audio);
CP_EXPORT char* cp_session_audio_backend(CpSession* session);
CP_EXPORT int64_t cp_session_audio_underruns(CpSession* session, int32_t stream);
CP_EXPORT int64_t cp_session_audio_dropped(CpSession* session, int32_t stream);
// Microseconds of audio queued in the audio server.
CP_EXPORT int64_t cp_session_audio_latency(CpSession* session, int32_t stream);

// Whether this machine's microphone is open, which it is only while the phone asks
// for it: Siri, or a call.
CP_EXPORT int32_t cp_session_microphone_active(CpSession* session);
CP_EXPORT double cp_session_microphone_level(CpSession* session);
CP_EXPORT int64_t cp_session_microphone_bytes(CpSession* session);
CP_EXPORT int32_t cp_session_set_microphone_device(CpSession* session, const char* device);
CP_EXPORT char* cp_session_microphone_device(CpSession* session);
CP_EXPORT char* cp_session_microphone_backend(CpSession* session);

// NULL to stop.
CP_EXPORT int32_t cp_session_set_metadata_callback(CpSession* session,
                                                   CpMetadataCallback on_metadata);
// The CP_METADATA_ADAPTER JSON as it stands. Free with cp_string_free.
CP_EXPORT char* cp_session_adapter_info(CpSession* session);

// The instrument cluster's texture, the same shape of calls as the main one. -1 and 0
// when the config named no cluster screen.
CP_EXPORT int64_t cp_session_cluster_texture_id(CpSession* session);
CP_EXPORT int32_t cp_session_cluster_video_active(CpSession* session);
CP_EXPORT int32_t cp_session_cluster_video_width(CpSession* session);
CP_EXPORT int32_t cp_session_cluster_video_height(CpSession* session);
CP_EXPORT void cp_session_set_cluster_display_size(CpSession* session, int32_t width,
                                                   int32_t height);

// Drives the texture from a generated pattern, for laying out an overlay with no
// adapter.
CP_EXPORT int32_t cp_session_start_test_pattern(CpSession* session);
CP_EXPORT int32_t cp_session_stop_test_pattern(CpSession* session);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // CARPLAY_NATIVE_CP_CORE_H_
