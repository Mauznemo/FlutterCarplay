// SPDX-License-Identifier: GPL-3.0-or-later
import 'dart:math' as math;
import 'dart:ui' show Size;

/// The frame a real head unit asks the phone for, for a view of [physical] pixels.
///
/// `carplay_native`'s `src/frame_size.cc` ported to Dart, so a layout tried on a Mac is
/// the layout the car gets. **Change one and change the other.**
Size simulatedFrameForView(Size physical) {
  const fallback = Size(1280, 720);
  const max = Size(2560, 1440);
  const min = Size(640, 320);
  if (!(physical.width >= 1) || !(physical.height >= 1)) {
    return fallback;
  }
  var width = physical.width;
  var height = physical.height;
  final shrink = [1.0, max.width / width, max.height / height].reduce(math.min);
  width *= shrink;
  height *= shrink;
  final grow = [1.0, min.width / width, min.height / height].reduce(math.max);
  width = math.min(width * grow, max.width);
  height = math.min(height * grow, max.height);
  int even(double value) {
    final rounded = value.round();
    return rounded - rounded % 2;
  }

  return Size(even(width).toDouble(), even(height).toDouble());
}
