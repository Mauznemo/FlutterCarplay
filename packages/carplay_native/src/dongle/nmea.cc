// SPDX-License-Identifier: GPL-3.0-or-later
#include "nmea.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "../text_format.h"

namespace cp {
namespace dongle {

namespace {

// Degrees as NMEA's ddmm.mmmm (or dddmm.mmmm for longitude) and a hemisphere letter.
std::string Coordinate(double degrees, bool longitude) {
  const char hemisphere =
      longitude ? (degrees < 0 ? 'W' : 'E') : (degrees < 0 ? 'S' : 'N');
  const double absolute = std::fabs(degrees);
  int whole = static_cast<int>(absolute);
  double minutes = (absolute - whole) * 60.0;
  // Rounding to four decimals can carry into a whole degree.
  if (minutes >= 59.99995) {
    minutes = 0;
    ++whole;
  }
  // Degrees as integers and minutes through FixedDecimal: no printf of a double, whose
  // decimal point follows the app's locale. See text_format.h.
  char degrees_text[8];
  std::snprintf(degrees_text, sizeof(degrees_text), longitude ? "%03d" : "%02d", whole);
  return degrees_text + FixedDecimal(minutes, 4, 7) + "," + hemisphere;
}

std::string Optional(double value, int decimals) {
  return std::isnan(value) ? std::string() : FixedDecimal(value, decimals);
}

std::string Sentence(const std::string& body) {
  char tail[8];
  std::snprintf(tail, sizeof(tail), "*%02X\r\n", NmeaChecksum(body));
  return "$" + body + tail;
}

}  // namespace

uint8_t NmeaChecksum(const std::string& body) {
  uint8_t sum = 0;
  for (char c : body) {
    sum ^= static_cast<uint8_t>(c);
  }
  return sum;
}

std::string NmeaSentences(const GnssFix& fix) {
  const std::time_t seconds = static_cast<std::time_t>(fix.time_ms / 1000);
  const int centis = static_cast<int>((fix.time_ms % 1000) / 10);
  std::tm utc{};
  gmtime_r(&seconds, &utc);
  char time[32];
  std::snprintf(time, sizeof(time), "%02d%02d%02d.%02d", utc.tm_hour, utc.tm_min, utc.tm_sec,
                centis);
  char date[32];
  std::snprintf(date, sizeof(date), "%02d%02d%02d", utc.tm_mday, utc.tm_mon + 1,
                utc.tm_year % 100);

  const std::string latitude = Coordinate(fix.latitude, false);
  const std::string longitude = Coordinate(fix.longitude, true);
  // HDOP is unitless; a receiver's accuracy in metres is roughly five times it. The
  // phone only uses it to weigh the fix, so the approximation is enough.
  const std::string hdop =
      std::isnan(fix.accuracy) ? "1.0" : Optional(std::fmax(0.5, fix.accuracy / 5.0), 1);
  const std::string altitude = Optional(fix.altitude, 1);
  // Knots, to one decimal.
  const std::string speed = Optional(fix.speed * 1.943844, 1);
  const std::string course = Optional(fix.heading, 1);

  const std::string gga = std::string("GPGGA,") + time + "," + latitude + "," + longitude +
                          ",1,08," + hdop + "," + altitude + "," +
                          (altitude.empty() ? "" : "M") + ",0.0,M,,";
  const std::string rmc = std::string("GPRMC,") + time + ",A," + latitude + "," +
                          longitude + "," + speed + "," + course + "," + date + ",,,A";
  return Sentence(gga) + Sentence(rmc);
}

}  // namespace dongle
}  // namespace cp
