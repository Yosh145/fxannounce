// ogg vorbis decoder

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fxa {

struct PcmBuffer {
  std::vector<int16_t> samples;  // mono
  std::vector<int16_t> muffled;
  int rate = 44100;
  double seconds() const {
    return rate > 0 ? static_cast<double>(samples.size()) / rate : 0.0;
  }
  size_t bytes() const {
    return (samples.size() + muffled.size()) * sizeof(int16_t);
  }
};

struct DecodeLimits {
  uint64_t maxFileBytes = 256ull << 20;
  double maxSeconds = 30 * 60;
  int minRate = 8000, maxRate = 192000;
  int maxChannels = 8;
};

struct DecodeResult {
  std::shared_ptr<PcmBuffer> pcm;  // null on failure
  std::string error;
};

DecodeResult decodeOgg(const std::string& utf8Path,
                       const DecodeLimits& limits = {});

}  // namespace fxa
