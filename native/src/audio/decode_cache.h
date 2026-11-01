// Background decoding and an LRU cache of decoded sounds (ARCHITECTURE.md § 3). No XPLM: safe on the worker.
#pragma once

#include "audio/decoder.h"

#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace fxa {

class DecodeCache {
public:
    explicit DecodeCache(size_t maxBytes = 128u << 20);
    ~DecodeCache();
    DecodeCache(const DecodeCache&) = delete;
    DecodeCache& operator=(const DecodeCache&) = delete;

    void setMaxBytes(size_t b);
    // Returns the decoded sound if ready (and marks it recently used); otherwise queues it and returns null.
    std::shared_ptr<PcmBuffer> get(const std::string& path);
    // Queues a decode without waiting (prefetch).
    void request(const std::string& path);
    // True if the file failed to decode (never retried); `error` gets the reason.
    bool failed(const std::string& path, std::string* error = nullptr) const;
    // Stops the worker (pending jobs are dropped). Called before plugin disable.
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
    std::list<std::string> lru_;   // front = most recent
    std::map<std::string, std::string> bad_;
    size_t bytes_ = 0, maxBytes_;
    bool stop_ = false;
    std::thread thread_;
};

} // namespace fxa
