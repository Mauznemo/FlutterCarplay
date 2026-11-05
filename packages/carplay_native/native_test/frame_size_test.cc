// SPDX-License-Identifier: GPL-3.0-or-later
// Tests of src/frame_size.cc: which frame a view gets.

#include <cstdio>

#include "frame_size.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                   #condition);                                             \
      ++failures;                                                           \
    }                                                                       \
  } while (false)

using cp::FrameSize;
using cp::FrameSizeForView;

bool Is(FrameSize size, int width, int height) {
  return size.width == width && size.height == height;
}

}  // namespace

int main() {
  // The view's own size, one to one: the example's view under its status bar.
  CHECK(Is(FrameSizeForView(1280, 632), 1280, 632));
  // Rounded to even.
  CHECK(Is(FrameSizeForView(1281.4, 633.6), 1280, 634));
  // No size yet.
  CHECK(Is(FrameSizeForView(0, 0), 1280, 720));
  CHECK(Is(FrameSizeForView(1280, 0), 1280, 720));
  // A 4K screen gets a frame of its shape inside the limit.
  CHECK(Is(FrameSizeForView(3840, 2160), 2560, 1440));
  // A wide cluster style strip keeps its shape.
  CHECK(Is(FrameSizeForView(5120, 1080), 2560, 540));
  // A tiny view gets a frame of its shape that covers it.
  CHECK(Is(FrameSizeForView(320, 180), 640, 360));
  // A shape no frame inside both limits can have keeps the limits.
  const FrameSize extreme = FrameSizeForView(4000, 100);
  CHECK(extreme.width == 2560 && extreme.height <= 1440 && extreme.height % 2 == 0);
  CHECK(Is(cp::EvenFrame(1921, 1081), 1920, 1080));
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
