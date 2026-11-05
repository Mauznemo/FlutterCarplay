// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_size.h"

#include <algorithm>
#include <cmath>

namespace cp {

namespace {

int32_t Even(double value) {
  const auto rounded = static_cast<int32_t>(std::lround(value));
  return rounded - (rounded % 2);
}

}  // namespace

FrameSize FrameSizeForView(double view_width, double view_height) {
  if (!(view_width >= 1) || !(view_height >= 1)) {
    return kFallbackFrame;
  }
  double width = view_width;
  double height = view_height;
  const double shrink = std::min({1.0, kMaxFrame.width / width, kMaxFrame.height / height});
  width *= shrink;
  height *= shrink;
  const double grow = std::max({1.0, kMinFrame.width / width, kMinFrame.height / height});
  // A shape too extreme for both limits at once keeps the maximum and gives up the shape.
  width = std::min(width * grow, static_cast<double>(kMaxFrame.width));
  height = std::min(height * grow, static_cast<double>(kMaxFrame.height));
  return FrameSize{Even(width), Even(height)};
}

FrameSize EvenFrame(int32_t width, int32_t height) {
  return FrameSize{width - (width % 2), height - (height % 2)};
}

}  // namespace cp
