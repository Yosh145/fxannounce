// Logging: FXAnnounce.log (rotating) plus an optional sink for one-line summaries in X-Plane's Log.txt.
// Pure C++ so the core can log in unit tests; the plugin installs the XPLMDebugString sink.
#pragma once

#include <cstdarg>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace fxa {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

class Log {
public:
    static Log& get();

    // Opens (and rotates at 1 MB) the plugin's own log file. Empty path: no file.
    void open(const std::string& path);
    void close();
    void setLevel(LogLevel lvl) { level_ = lvl; }
    LogLevel level() const { return level_; }
    // Receives Warn/Error lines and lines logged with `summary`, prefixed "FXAnnounce: " (Log.txt).
    void setSummarySink(std::function<void(const char*)> sink);

    void write(LogLevel lvl, bool summary, const char* fmt, va_list ap);
    // The most recent lines, oldest first, for the UI (thread-safe copy).
    std::deque<std::string> recent() const;

private:
    Log() = default;
    mutable std::mutex mu_;
    LogLevel level_ = LogLevel::Info;
    std::string path_;
    void* file_ = nullptr;   // FILE*
    std::function<void(const char*)> sink_;
    std::deque<std::string> recent_;
};

void logError(const char* fmt, ...);
void logWarn(const char* fmt, ...);
void logInfo(const char* fmt, ...);
void logDebug(const char* fmt, ...);
// Info line that also goes to Log.txt.
void logSummary(const char* fmt, ...);

} // namespace fxa
