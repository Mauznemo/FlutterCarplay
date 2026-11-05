// SPDX-License-Identifier: GPL-3.0-or-later
// The frame the phone is asked to draw, chosen from the view it will be shown in.
//
// CarPlay draws whatever size the head unit names, unlike Android Auto's five 16:9
// sizes, so the best frame is the view's own in physical pixels: the phone lays out a
// screen exactly that big and it is drawn one to one. Two limits apply, both about the
// adapter rather than the phone: its memory (128 MB, a fixed H.264 buffer, captures that
// worked topped out at 2400x960), and that both sides must be even. Nothing here names
// the adapter, so a native backend can use it too.

#ifndef CARPLAY_NATIVE_FRAME_SIZE_H_
#define CARPLAY_NATIVE_FRAME_SIZE_H_

#include <cstdint>

namespace cp {

struct FrameSize {
  int32_t width = 0;
  int32_t height = 0;

  bool operator==(const FrameSize& other) const {
    return width == other.width && height == other.height;
  }
  bool operator!=(const FrameSize& other) const { return !(*this == other); }
};

// What is asked for before any view has a size.
constexpr FrameSize kFallbackFrame{1280, 720};
// A view larger than this gets a frame of its shape that fits inside it, stretched.
constexpr FrameSize kMaxFrame{2560, 1440};
// And smaller than this, one of its shape that covers it, shrunk.
constexpr FrameSize kMinFrame{640, 320};

// The frame for a `view_width` by `view_height` view in physical pixels: the view's own
// size, rounded to even and scaled into the limits above keeping its shape. The fallback
// for a view with no size.
FrameSize FrameSizeForView(double view_width, double view_height);

// A size the host app named, rounded down to even.
FrameSize EvenFrame(int32_t width, int32_t height);

}  // namespace cp

#endif  // CARPLAY_NATIVE_FRAME_SIZE_H_
