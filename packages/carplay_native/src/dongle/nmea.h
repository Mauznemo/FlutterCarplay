// SPDX-License-Identifier: GPL-3.0-or-later
// The car's position as NMEA 0183, which is how the adapter takes it.
//
// The adapter forwards GPGGA and GPRMC sentences to the phone, which then uses the car's
// fix instead of its own (docs/dongle-protocol.md 3.11). Pure text, no state, so it is
// unit tested in native_test/.

#ifndef CARPLAY_NATIVE_DONGLE_NMEA_H_
#define CARPLAY_NATIVE_DONGLE_NMEA_H_

#include <cstdint>
#include <string>

namespace cp {
namespace dongle {

// One fix. NaN in an optional field means the car does not know it, and the field is
// left empty in the sentence rather than written as zero: an empty altitude and an
// altitude of sea level are different statements.
struct GnssFix {
  double latitude = 0;
  double longitude = 0;
  // Metres above sea level. Optional.
  double altitude = 0;
  // Metres per second. Optional.
  double speed = 0;
  // Degrees clockwise from true north. Optional.
  double heading = 0;
  // Horizontal accuracy in metres, reported as HDOP. Optional.
  double accuracy = 0;
  // UTC, milliseconds since the epoch.
  int64_t time_ms = 0;
};

// "$GPGGA,...*hh\r\n$GPRMC,...*hh\r\n" for `fix`.
std::string NmeaSentences(const GnssFix& fix);

// The checksum of one sentence body, the part between `$` and `*`: the XOR of its bytes.
uint8_t NmeaChecksum(const std::string& body);

}  // namespace dongle
}  // namespace cp

#endif  // CARPLAY_NATIVE_DONGLE_NMEA_H_
