// fxannounce.log rotates optional sink into log.txt pure cpp so tests can log
#pragma once

#include <cstdarg>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace fxa {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// ui log row exact repeats collapse into the newest with a count
struct LogLine {
  std::string time;  // local
  LogLevel level = LogLevel::Info;
  std::string msg;
  int count = 1;
};
const char* logLevelName(LogLevel lvl);

class Log {
 public:
  static Log& get();

  // rotates at 1mb empty path no file
  void open(const std::string& path);
  void close();
  void setLevel(LogLevel lvl) { level_ = lvl; }
  LogLevel level() const { return level_; }
  // warn/error summary lines go here
  void setSummarySink(std::function<void(const char*)> sink);

  void write(LogLevel lvl, bool summary, const char* fmt, va_list ap);
  // oldest first file still has everything
  std::deque<LogLine> recent() const;
  static constexpr size_t kRecentLines = 400;

 private:
  Log() = default;
  mutable std::mutex mu_;
  LogLevel level_ = LogLevel::Info;
  std::string path_;
  void* file_ = nullptr;  // file
  std::function<void(const char*)> sink_;
  std::deque<LogLine> recent_;
};

void logError(const char* fmt, ...);
void logWarn(const char* fmt, ...);
void logInfo(const char* fmt, ...);
void logDebug(const char* fmt, ...);
// also to log.txt
void logSummary(const char* fmt, ...);

}  // namespace fxa
