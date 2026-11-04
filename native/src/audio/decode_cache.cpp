#include "audio/decode_cache.h"

#include "audio/muffle.h"

namespace fxa {

DecodeCache::DecodeCache(size_t maxBytes) : maxBytes_(maxBytes) {
  thread_ = std::thread([this] { worker(); });
}

DecodeCache::~DecodeCache() { shutdown(); }

void DecodeCache::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
    jobs_.clear();
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void DecodeCache::setMaxBytes(size_t b) {
  std::lock_guard<std::mutex> lock(mu_);
  maxBytes_ = b;
  evictLocked();
}

void DecodeCache::setMuffleCutoff(float hz) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!(hz > 0)) hz = 0;
  if (hz == muffleHz_) return;
  muffleHz_ = hz;
  ready_.clear();
  lru_.clear();
  bytes_ = 0;
}

size_t DecodeCache::bytes() const {
  std::lock_guard<std::mutex> lock(mu_);
  return bytes_;
}

std::shared_ptr<PcmBuffer> DecodeCache::get(const std::string& path) {
  std::unique_lock<std::mutex> lock(mu_);
  auto it = ready_.find(path);
  if (it != ready_.end()) {
    lru_.remove(path);
    lru_.push_front(path);
    return it->second;
  }
  if (stop_ || bad_.count(path) || pending_.count(path)) return nullptr;
  pending_.insert(path);
  jobs_.push_back(path);
  lock.unlock();
  cv_.notify_one();
  return nullptr;
}

void DecodeCache::request(const std::string& path) { (void)get(path); }

bool DecodeCache::failed(const std::string& path, std::string* error) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = bad_.find(path);
  if (it == bad_.end()) return false;
  if (error) *error = it->second;
  return true;
}

void DecodeCache::evictLocked() {
  // lru evict till under the limit
  for (auto it = lru_.end(); bytes_ > maxBytes_ && it != lru_.begin();) {
    --it;
    if (it == lru_.begin()) break;
    auto r = ready_.find(*it);
    if (r != ready_.end() && r->second.use_count() == 1) {
      bytes_ -= r->second->bytes();
      ready_.erase(r);
      it = lru_.erase(it);
    }
  }
}

void DecodeCache::worker() {
  while (true) {
    std::string path;
    float muffleHz = 0;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [&] { return stop_ || !jobs_.empty(); });
      if (stop_) return;
      path = jobs_.front();
      jobs_.pop_front();
      muffleHz = muffleHz_;
    }
    DecodeResult r;
    try {
      r = decodeOgg(path);
      if (r.pcm && muffleHz > 0)
        r.pcm->muffled = lowPass(r.pcm->samples, r.pcm->rate, muffleHz);
    } catch (const std::exception& e) {
      r.error = std::string("decoder exception: ") + e.what();
    } catch (...) {
      r.error = "decoder exception";
    }
    std::lock_guard<std::mutex> lock(mu_);
    pending_.erase(path);
    if (stop_) return;
    if (!r.pcm) {
      bad_[path] = r.error;
      continue;
    }
    if (muffleHz != muffleHz_) continue;  // muffle cutoff changed drop it
    bytes_ += r.pcm->bytes();
    ready_[path] = r.pcm;
    lru_.push_front(path);
    evictLocked();
  }
}

}  // namespace fxa
