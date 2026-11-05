# Architecture

## Layer cake

```
┌──────────────────────────────────────────────────────────────────────┐
│  Host app (the Flutter infotainment system)                          │
│                                                                      │
│    Stack(children: [                                                 │
│      CarPlayView(),            // the projected iPhone screen        │
│      MyStatusBar(),            // ordinary Flutter widgets on top    │
│      MyNowPlayingCard(),       // fed by metadata, not pixels        │
│    ])                                                                │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ package:carplay
┌───────────────────────────────▼──────────────────────────────────────┐
│  carplay                      (app facing, pure Dart)                │
│    CarPlayView, CarPlayController                                    │
│                                                                      │
│  carplay_platform_interface   (pure Dart)                            │
│    CarPlayPlatform, config, events, metadata models                  │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ implements
┌───────────────────────────────▼──────────────────────────────────────┐
│  carplay_native                (Linux)                               │
│                                                                      │
│  Dart:   CarPlayNative ── dart:ffi ──┐     NativeCallable.listener    │
│  ────────────────────────────────────│──────────────▲──────────────  │
│  Native (C++):                       ▼              │                │
│                                                                      │
│    carplay_native_plugin.cc   GTK entry point, captures the          │
│                               FlTextureRegistrar                     │
│    cp_core.h / cp_core.cc     flat C ABI, owns everything below      │
│                                                                      │
│    shared by every backend (copied from android_auto):               │
│      video/libav_decoder      H.264 ─> VA-API or CPU ─> FrameRing    │
│      present/gl_adapter       dmabuf ─> EGLImage ─> FlTextureGL      │
│      audio/audio_output       three streams ─> PulseAudio, ducking   │
│      audio/audio_input        microphone, only while asked for       │
│      event_bus                native ─> Dart                         │
│                                                                      │
│    backend: dongle/           the Carlinkit adapter over USB         │
│      usb_link                 libusb, reads always pending, writer   │
│      stream_parser            byte stream ─> messages                │
│      protocol                 every byte offset, no state            │
│      dongle_session           start up, heartbeat, dispatch          │
│                                                                      │
│    backend: native CarPlay    not written, see PLAN.md               │
└──────────────────────────────────────────────────────────────────────┘
```

## The backend seam

The phone side is the only part that differs between the ways this plugin can get CarPlay. The
Carlinkit adapter is the CarPlay receiver and hands over H.264 and PCM; a native backend would be
the receiver itself and produce the same two things from AirPlay. Either way the rest of the
pipeline is identical, so it lives once, in `cp_core.cc`, and a backend is given:

- the `VideoDecoder`, to `Submit` Annex B access units to and `Flush` when a picture ends
- the `AudioOutput`, to `BeginStream`, `Submit` and `EndStream` PCM on one of three streams
- the `AudioInput`, to `Start` when the phone asks for the microphone and `Stop` when it lets go
- hooks for state changes, news and metadata, which go to Dart through the event bus

`DongleSession` is the first backend. A second one is a directory beside `dongle/` with a class of
the same shape, and `cp_core.cc` choosing between them; nothing above the C ABI changes.

## Video, and why it is the same as android_auto's

The video path is android_auto's, copied file for file: Impeller on Linux runs its OpenGLES
backend, its Vulkan backend is coming, and so the decoder hands over a **dmabuf**, never a GL
texture. The reasoning is in android_auto's `docs/architecture.md`, "Staying ahead of the
renderer", and holds here unchanged:

```
H.264 ─> decoder ─> dmabuf fd + DRM format modifier + stride  ┐ API agnostic
   ─────────────────────────────────────────────────────────  ┤
   gl_adapter   dmabuf ─> EGLImage ─> FlTextureGL  (today)    │ the only API
   vk_adapter   dmabuf ─> VkImage  (not written)              ┘ specific code
```

- `FlTextureGL` and every GL type are named in `present/gl_adapter.cc` and nowhere else.
- The software fallback converts with libswscale and publishes RGBA through the same ring.
- When the texture is drawn smaller than the picture, the adapter renders at the drawn size and
  averages every source pixel, because Flutter's sampling of an external texture skips pixels.

What differs is what arrives. CarPlay's H.264 is High profile, which VA-API decodes without the
SPS rewrite Android Auto's Baseline needed. SPS and PPS come inline, in the same message as every
keyframe, and the first frames can be a small boot screen before the phone switches to the size
it was asked for, so the decoder has to follow a size change mid stream. And CarPlay sends a
keyframe when a stream starts and then hardly ever: minutes of P frames. A decoder that loses its
place (a queue overflow, a flush) would wait forever, so it calls `SetKeyframeRequest`, and the
session asks the phone for one, at most twice a second.

## Frame size: the view's own, not a list

Android Auto only knows five 16:9 frame sizes, which is why android_auto asks the phone for
margins and crops them off. CarPlay takes the size the head unit names, so there is nothing to
crop: the frame is the view's size in physical pixels, rounded to even.

```
CarPlayView layout ─> cp_session_set_view_size (physical pixels)
   ─> cp_session_start: ResolveFrameSize, unless the config names one
        even, at most 2560x1440 keeping the shape, at least 640x320
   ─> Open(width, height, fps) to the adapter
   ─> the phone lays out a screen exactly that big, drawn one to one
```

The size is settled when the session starts and holds until it is started again: changing it
means a new Open, which the adapter only takes after dropping the phone. So a view that changes
size mid session keeps the frame it started with, fitted by `CarPlayView.fit`. Following a resize
live is in `PLAN.md`.

## When there is a picture

The same rule as android_auto, for the same reason: the texture is registered by
`cp_session_start` and lives until the session is destroyed, so its id says nothing about whether
it holds anything, and neither does a connected phone, which the adapter reports seconds before
the first frame. `VideoDecoder::live()` does, exposed as `cp_session_video_active` and
`CarPlayController.hasVideo`. It goes false on every way a picture can end (the phone leaving, the
adapter leaving, a stop), and an end also empties the ring and puts the texture back to one
transparent pixel. `CarPlayView` shows its placeholder whenever it is false.

## Threads

| Thread | Owns | Must never |
|---|---|---|
| Flutter platform thread | FFI calls from Dart, texture registration | block on USB |
| Flutter raster thread | `populate()` in the GL adapter, GL context current | decode |
| libusb event thread (one, process lifetime) | read completions, `DongleSession::OnMessage` | block: while it does, no read is pending, and the adapter resets the link after 4.5 s without one |
| USB writer thread (per open link) | synchronous bulk writes, in order | be called into from the event thread synchronously |
| Session control thread | opening the link, the start up sequence, the heartbeat, timers, the microphone's start and stop, teardown | touch GL or Dart |
| Decoder thread | libavcodec, dmabuf export | touch Dart |
| Audio writer threads (one per stream) | one `PcmSink` each, blocking writes | run on the event thread |
| Capture thread (only while the phone has the microphone) | one `PcmSource`, blocking reads | outlive the phone's request |

Handoff rules:

- The event thread copies what it needs and returns: `VideoDecoder::Submit` and
  `AudioOutput::Submit` both copy into their own queues, and anything that may block is posted to
  the control thread with `DongleSession::Post`.
- Everything written to the adapter goes through `UsbLink::Send`, which queues for the writer
  thread, so a write from any thread is safe and keeps message order.
- Native to Dart goes through `NativeCallable.listener` callables only: events, metadata and the
  optional PCM tap.

## The C ABI

`packages/carplay_native/src/cp_core.h` is the definition, with a comment on every function. The
same rules as android_auto's: plain scalars, opaque pointers and C strings, anything richer as one
JSON string per update, and everything the core reports through callbacks. `CpState` and
`CarPlayConnectionState` cross FFI as integers, as do `CpAudioStream`, `CpMicrophone` and
`CpMetadata` with their Dart enums, so each pair keeps its order in step.

Metadata is mostly the phone's own JSON, passed through untouched: the adapter forwards what the
phone says about the track and the route as JSON already, so there is no native state to keep,
and `CarPlayNowPlaying.merge` and `CarPlayNavigation.merge` in Dart fold the partial updates
together. The adapter's and the phone's descriptions are built natively, with the adapter's own
JSON carried inside as text, so one malformed byte from the adapter costs a detail rather than
the whole update.

## Coordinate mapping

`CarPlayView` maps a pointer to projected video pixels with the same `applyBoxFit` arithmetic
android_auto's view uses, and the core divides by the decoded picture's size to get the 0 to 1
floats the adapter's MultiTouch message carries. Measured against the decoded size rather than
the requested one, because the phone's first frames can be a boot screen of another size.

## Package boundaries and licence

`carplay` and `carplay_platform_interface` are pure Dart; `carplay_native` holds every line of
native code. All three are GPL-3.0-or-later. Nothing here links aasdk or any other GPL library, so
the licence is a choice rather than an obligation: it keeps code moving freely to and from
android_auto, and makes LIVI's GPL code usable for the native backend. See `docs/research.md`.
