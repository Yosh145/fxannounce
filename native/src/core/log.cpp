#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace fxa {

namespace {
constexpr size_t kRotateBytes = 1u << 20;
const char* kLevelNames[] = {"ERROR", "WARN ", "INFO ", "DEBUG"};
const char* kLevelWords[] = {"ERROR", "WARN", "INFO", "DEBUG"};

std::string timestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
                tm.tm_sec);
  return buf;
}
}  // namespace

const char* logLevelName(LogLevel lvl) {
  return kLevelWords[std::clamp(static_cast<int>(lvl), 0, 3)];
}

Log& Log::get() {
  static Log instance;
  return instance;
}

void Log::open(const std::string& path) {
  std::lock_guard<std::mutex> lock(mu_);
  if (file_) {
    std::fclose(static_cast<FILE*>(file_));
    file_ = nullptr;
  }
  path_ = path;
  if (path_.empty()) return;
  std::error_code ec;
  const auto p = std::filesystem::u8path(path_);
  if (std::filesystem::exists(p, ec) &&
      std::filesystem::file_size(p, ec) > kRotateBytes) {
    auto old = p;
    old += ".1";
    std::filesystem::remove(old, ec);
    std::filesystem::rename(p, old, ec);
  }
#if defined(_WIN32)
  file_ = _wfopen(p.wstring().c_str(), L"a");
#else
  file_ = std::fopen(p.string().c_str(), "a");
#endif
}

void Log::close() {
  std::lock_guard<std::mutex> lock(mu_);
  if (file_) std::fclose(static_cast<FILE*>(file_));
  file_ = nullptr;
}

void Log::setSummarySink(std::function<void(const char*)> sink) {
  std::lock_guard<std::mutex> lock(mu_);
  sink_ = std::move(sink);
}

void Log::write(LogLevel lvl, bool summary, const char* fmt, va_list ap) {
  if (static_cast<int>(lvl) > static_cast<int>(level_)) return;
  char msg[1024];
  std::vsnprintf(msg, sizeof(msg), fmt, ap);
  const std::string time = timestamp();
  const std::string line =
      time + " " + kLevelNames[static_cast<int>(lvl)] + " " + msg;

  std::lock_guard<std::mutex> lock(mu_);
  if (file_) {
    std::fprintf(static_cast<FILE*>(file_), "%s\n", line.c_str());
    std::fflush(static_cast<FILE*>(file_));
  }
  // repeats move to the bottom w a count
  int count = 1;
  const auto same = std::find_if(
      recent_.rbegin(), recent_.rend(),
      [&](const LogLine& l) { return l.level == lvl && l.msg == msg; });
  if (same != recent_.rend()) {
    count = same->count + 1;
    recent_.erase(std::next(same).base());
  }
  recent_.push_back(LogLine{time, lvl, msg, count});
  while (recent_.size() > kRecentLines) recent_.pop_front();
  if (sink_ && (summary || lvl <= LogLevel::Warn)) {
    char out[1100];
    std::snprintf(out, sizeof(out), "FXAnnounce: %s%s\n",
                  lvl == LogLevel::Error
                      ? "ERROR: "
                      : (lvl == LogLevel::Warn ? "warning: " : ""),
                  msg);
    sink_(out);
  }
}

std::deque<LogLine> Log::recent() const {
  std::lock_guard<std::mutex> lock(mu_);
  return recent_;
}

#define FXA_LOG_IMPL(LVL, SUMMARY)         \
  va_list ap;                              \
  va_start(ap, fmt);                       \
  Log::get().write(LVL, SUMMARY, fmt, ap); \
  va_end(ap);

void logError(const char* fmt, ...) { FXA_LOG_IMPL(LogLevel::Error, false) }
void logWarn(const char* fmt, ...) { FXA_LOG_IMPL(LogLevel::Warn, false) }
void logInfo(const char* fmt, ...) { FXA_LOG_IMPL(LogLevel::Info, false) }
void logDebug(const char* fmt, ...) { FXA_LOG_IMPL(LogLevel::Debug, false) }
void logSummary(const char* fmt, ...) { FXA_LOG_IMPL(LogLevel::Info, true) }

#undef FXA_LOG_IMPL

}  // namespace fxa
