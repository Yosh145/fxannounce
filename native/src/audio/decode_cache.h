// decoded audio cache background worker

#pragma once

#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "audio/decoder.h"

namespace fxa {

class DecodeCache {
 public:
  explicit DecodeCache(size_t maxBytes = 192u << 20);
  ~DecodeCache();
  DecodeCache(const DecodeCache&) = delete;
  DecodeCache& operator=(const DecodeCache&) = delete;

  void setMaxBytes(size_t b);
  // lowpass cutoff for everything
  void setMuffleCutoff(float hz);
  std::shared_ptr<PcmBuffer> get(const std::string& path);
  // prefetch
  void request(const std::string& path);
  bool failed(const std::string& path, std::string* error = nullptr) const;
  void shutdown();
  size_t bytes() const;

 private:
  void worker();
  void evictLocked();

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::string> jobs_;
  std::set<std::string> pending_;
  std::map<std::string, std::shared_ptr<PcmBuffer>> ready_;
  std::list<std::string> lru_;  // front newest
  std::map<std::string, std::string> bad_;
  size_t bytes_ = 0, maxBytes_;
  float muffleHz_ = 0;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace fxa
