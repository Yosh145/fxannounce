#include "audio/decoder.h"

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fxa {

namespace {

size_t readCb(void* ptr, size_t size, size_t n, void* src) {
  return std::fread(ptr, size, n, static_cast<FILE*>(src));
}

int seekCb(void* src, ogg_int64_t off, int whence) {
#if defined(_WIN32)
  return _fseeki64(static_cast<FILE*>(src), off, whence);
#else
  return fseeko(static_cast<FILE*>(src), static_cast<off_t>(off), whence);
#endif
}

long tellCb(void* src) {
#if defined(_WIN32)
  return static_cast<long>(_ftelli64(static_cast<FILE*>(src)));
#else
  return static_cast<long>(ftello(static_cast<FILE*>(src)));
#endif
}

struct FileCloser {
  void operator()(FILE* f) const {
    if (f) std::fclose(f);
  }
};

}  // namespace

DecodeResult decodeOgg(const std::string& utf8Path, const DecodeLimits& lim) {
  DecodeResult r;
  std::error_code ec;
  const auto path = std::filesystem::u8path(utf8Path);
  const uint64_t size = std::filesystem::file_size(path, ec);
  if (ec) {
    r.error = "cannot read file";
    return r;
  }
  if (size > lim.maxFileBytes) {
    r.error = "file too large";
    return r;
  }
#if defined(_WIN32)
  std::unique_ptr<FILE, FileCloser> file(
      _wfopen(path.wstring().c_str(), L"rb"));
#else
  std::unique_ptr<FILE, FileCloser> file(
      std::fopen(path.string().c_str(), "rb"));
#endif
  if (!file) {
    r.error = "cannot open file";
    return r;
  }
  OggVorbis_File vf;
  ov_callbacks cb{readCb, seekCb, nullptr, tellCb};  // we close the file
  if (ov_open_callbacks(file.get(), &vf, nullptr, 0, cb) != 0) {
    r.error = "not an Ogg Vorbis file";
    return r;
  }
  struct VfGuard {
    OggVorbis_File* v;
    ~VfGuard() { ov_clear(v); }
  } guard{&vf};

  const vorbis_info* info = ov_info(&vf, -1);
  if (!info || info->channels < 1 || info->channels > lim.maxChannels ||
      info->rate < lim.minRate || info->rate > lim.maxRate) {
    r.error = "unsupported channels or sample rate";
    return r;
  }
  const int channels = info->channels;
  const int rate = static_cast<int>(info->rate);
  const ogg_int64_t total = ov_pcm_total(&vf, -1);
  const double maxSamples = lim.maxSeconds * rate;
  if (total > 0 && static_cast<double>(total) > maxSamples) {
    r.error = "longer than the limit";
    return r;
  }

  auto pcm = std::make_shared<PcmBuffer>();
  pcm->rate = rate;
  if (total > 0) pcm->samples.reserve(static_cast<size_t>(total));
  int section = 0;
  int holes = 0;
  while (true) {
    float** data = nullptr;
    const long n = ov_read_float(&vf, &data, 4096, &section);
    if (n == 0) break;
    if (n == OV_HOLE) {  // recoverable gap
      if (++holes > 100) {
        r.error = "stream too damaged";
        return r;
      }
      continue;
    }
    if (n < 0) {
      r.error = "decode error";
      return r;
    }
    const vorbis_info* cur = ov_info(&vf, section);
    const int ch = cur ? std::min(cur->channels, channels) : channels;
    for (long k = 0; k < n; ++k) {
      float mix = 0;
      for (int c = 0; c < ch; ++c) mix += data[c][k];
      mix /= static_cast<float>(ch);
      if (!std::isfinite(mix)) mix = 0;
      mix = std::clamp(mix, -1.0f, 1.0f);
      pcm->samples.push_back(static_cast<int16_t>(std::lrint(mix * 32767.0f)));
    }
    if (static_cast<double>(pcm->samples.size()) > maxSamples) {
      r.error = "longer than the limit";
      return r;
    }
  }
  if (pcm->samples.empty()) {
    r.error = "no audio";
    return r;
  }
  r.pcm = std::move(pcm);
  return r;
}

}  // namespace fxa
