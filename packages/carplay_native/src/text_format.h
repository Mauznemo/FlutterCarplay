// SPDX-License-Identifier: GPL-3.0-or-later
// Numbers as text, without the locale.
//
// The Flutter app runs with the user's locale, and printf family formatting follows it,
// so on a German machine 52.5 comes out as "52,5". In NMEA, whose fields are comma
// separated, and in JSON that is not a number at all. Found on the author's machine with
// the GPS sentences. std::to_chars never consults the locale, so every number that goes
// into a wire format goes through here.

#ifndef CARPLAY_NATIVE_TEXT_FORMAT_H_
#define CARPLAY_NATIVE_TEXT_FORMAT_H_

#include <charconv>
#include <string>

namespace cp {

// `value` with exactly `decimals` digits after a point, padded with zeros on the left to
// at least `width` characters in all.
inline std::string FixedDecimal(double value, int decimals, size_t width = 0) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                    std::chars_format::fixed, decimals);
  std::string text(buffer, result.ptr);
  if (text.size() < width) {
    const bool negative = !text.empty() && text[0] == '-';
    text.insert(negative ? 1 : 0, width - text.size(), '0');
  }
  return text;
}

// The shortest text that reads back as `value`, as JSON writes a number.
inline std::string ShortestDecimal(double value) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

}  // namespace cp

#endif  // CARPLAY_NATIVE_TEXT_FORMAT_H_
