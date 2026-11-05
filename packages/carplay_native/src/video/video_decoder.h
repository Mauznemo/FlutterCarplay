// SPDX-License-Identifier: GPL-3.0-or-later
// Picks the platform's H.264 decoder.
//
// Each platform's decoder declares a class named VideoDecoder with the same public
// interface, and this header includes the one for the platform being built. android_auto
// has a MediaCodec one for Android beside libav_decoder.h; it comes across with the
// Android port, see PLAN.md.

#ifndef CARPLAY_NATIVE_VIDEO_VIDEO_DECODER_H_
#define CARPLAY_NATIVE_VIDEO_VIDEO_DECODER_H_

#include "libav_decoder.h"

#endif  // CARPLAY_NATIVE_VIDEO_VIDEO_DECODER_H_
