// SPDX-License-Identifier: GPL-3.0-or-later
// The native core's log, one line per statement on stderr.
//
// android_auto borrowed aasdk's Boost.Log macro for this. There is no aasdk here, and
// Boost.Log is a heavy dependency for writing lines to stderr, so this is the same
// stream shaped call with nothing behind it:
//
//   CP_LOG(info) << "[Dongle] opened, firmware " << version;
//
// Levels are debug, info, warning and error. `CP_LOG_LEVEL=DEBUG` in the environment
// shows debug lines, which are off otherwise. Everything else is always on, because a
// head unit's log is usually the only record of why a session ended.

#ifndef CARPLAY_NATIVE_LOG_H_
#define CARPLAY_NATIVE_LOG_H_

#include <sstream>
#include <string>

namespace cp {

enum class LogLevel { kDebug = 0, kInfo = 1, kWarning = 2, kError = 3 };

// Whether a line at `level` is written. Reads CP_LOG_LEVEL once.
bool LogEnabled(LogLevel level);

// Writes one finished line. Thread safe: a line is written with one call, so lines from
// different threads never interleave mid line.
void LogWrite(LogLevel level, const std::string& line);

// Collects one statement's worth of `<<` and writes it when the statement ends.
class LogLine {
 public:
  explicit LogLine(LogLevel level) : level_(level) {}
  ~LogLine() { LogWrite(level_, stream_.str()); }

  LogLine(const LogLine&) = delete;
  LogLine& operator=(const LogLine&) = delete;

  template <typename T>
  LogLine& operator<<(const T& value) {
    stream_ << value;
    return *this;
  }

 private:
  LogLevel level_;
  std::ostringstream stream_;
};

}  // namespace cp

#define CP_LOG_LEVEL_debug ::cp::LogLevel::kDebug
#define CP_LOG_LEVEL_info ::cp::LogLevel::kInfo
#define CP_LOG_LEVEL_warning ::cp::LogLevel::kWarning
#define CP_LOG_LEVEL_error ::cp::LogLevel::kError

// The `if` keeps a disabled line from formatting anything, and the `else` form keeps
// the macro safe inside an unbraced if of the caller's.
#define CP_LOG(level)                                   \
  if (!::cp::LogEnabled(CP_LOG_LEVEL_##level)) {        \
  } else                                                \
    ::cp::LogLine(CP_LOG_LEVEL_##level)

#endif  // CARPLAY_NATIVE_LOG_H_
