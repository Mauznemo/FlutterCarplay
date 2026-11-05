// SPDX-License-Identifier: GPL-3.0-or-later
// Picks the platform's H.264 decoder.
//
// Each platform's decoder declares a class named VideoDecoder with the same public
// interface, and this header includes the one for the platform being built.

#ifndef CARPLAY_NATIVE_VIDEO_VIDEO_DECODER_H_
#define CARPLAY_NATIVE_VIDEO_VIDEO_DECODER_H_

#if defined(__ANDROID__)
#include "mediacodec_decoder.h"
#else
#include "libav_decoder.h"
#endif

#endif  // CARPLAY_NATIVE_VIDEO_VIDEO_DECODER_H_
