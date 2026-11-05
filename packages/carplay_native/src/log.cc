// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

namespace cp {

namespace {

LogLevel ThresholdFromEnvironment() {
  const char* value = std::getenv("CP_LOG_LEVEL");
  if (value == nullptr) {
    return LogLevel::kInfo;
  }
  if (strcasecmp(value, "debug") == 0 || strcasecmp(value, "trace") == 0) {
    return LogLevel::kDebug;
  }
  if (strcasecmp(value, "warning") == 0) {
    return LogLevel::kWarning;
  }
  if (strcasecmp(value, "error") == 0) {
    return LogLevel::kError;
  }
  return LogLevel::kInfo;
}

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "debug";
    case LogLevel::kInfo:
      return "info";
    case LogLevel::kWarning:
      return "warning";
    case LogLevel::kError:
      return "error";
  }
  return "info";
}

}  // namespace

bool LogEnabled(LogLevel level) {
  static const LogLevel threshold = ThresholdFromEnvironment();
  return static_cast<int>(level) >= static_cast<int>(threshold);
}

void LogWrite(LogLevel level, const std::string& line) {
  // Wall clock with milliseconds, so a line can be read against a usbmon capture or
  // the journal taken at the same time.
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  const int millis = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
          .count() %
      1000);
  std::tm local{};
  localtime_r(&seconds, &local);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);

  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  std::fprintf(stderr, "[%s.%03d] [carplay] [%s] %s\n", stamp, millis, LevelName(level),
               line.c_str());
  std::fflush(stderr);
}

}  // namespace cp
