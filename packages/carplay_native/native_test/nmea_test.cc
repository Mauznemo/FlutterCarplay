// SPDX-License-Identifier: GPL-3.0-or-later
// Tests of src/dongle/nmea.cc against hand checked sentences.

#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>

#include "dongle/nmea.h"

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

using namespace cp::dongle;

}  // namespace

int main() {
  // The classic example sentence body from the NMEA documentation has checksum 47.
  CHECK(NmeaChecksum("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,") ==
        0x47);

  GnssFix fix;
  fix.latitude = 52.520008;   // Berlin
  fix.longitude = 13.404954;
  fix.altitude = 34.0;
  fix.speed = 13.9;           // 50 km/h
  fix.heading = 90.0;
  fix.accuracy = 5.0;
  fix.time_ms = 1762344000000 + 250;  // 2025-11-05 12:00:00.25 UTC
  const std::string text = NmeaSentences(fix);
  std::printf("%s", text.c_str());
  CHECK(text.find("$GPGGA,120000.25,5231.2005,N,01324.2972,E,1,08,1.0,34.0,M,0.0,M,,*") == 0);
  CHECK(text.find("$GPRMC,120000.25,A,5231.2005,N,01324.2972,E,27.0,90.0,051125,,,A*") !=
        std::string::npos);
  CHECK(text.size() > 4 && text.substr(text.size() - 2) == "\r\n");

  // Every sentence's checksum matches its body.
  size_t start = 0;
  int sentences = 0;
  while ((start = text.find('$', start)) != std::string::npos) {
    const size_t star = text.find('*', start);
    const std::string body = text.substr(start + 1, star - start - 1);
    unsigned written = 0;
    std::sscanf(text.c_str() + star + 1, "%2X", &written);
    CHECK(written == NmeaChecksum(body));
    ++sentences;
    start = star;
  }
  CHECK(sentences == 2);

  // Unknowns are empty fields, not zeros; the southern and western hemispheres.
  GnssFix unknown;
  unknown.latitude = -33.8688;
  unknown.longitude = -151.2093;
  unknown.altitude = NAN;
  unknown.speed = NAN;
  unknown.heading = NAN;
  unknown.accuracy = NAN;
  unknown.time_ms = 0;
  const std::string sydney = NmeaSentences(unknown);
  CHECK(sydney.find(",3352.1280,S,15112.5580,W,1,08,1.0,,,0.0,M,,*") != std::string::npos);
  CHECK(sydney.find(",W,,,010170,,,A*") != std::string::npos);

  // Under a locale whose decimal separator is a comma, which is what the Flutter app runs
  // with on a German machine. printf would write 5231,2005 here and break every field.
  if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr) {
    const std::string german = NmeaSentences(fix);
    CHECK(german == text);
    std::setlocale(LC_NUMERIC, "C");
  } else {
    std::printf("de_DE.UTF-8 not installed, locale check skipped\n");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
