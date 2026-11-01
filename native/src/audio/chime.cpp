#include "audio/chime.h"

#include <algorithm>
#include <cmath>

namespace fxa {

namespace {

constexpr double kPi = 3.14159265358979323846;

// One struck tone: fundamental plus two soft partials, fast attack, exponential decay.
void addTone(std::vector<float>& out, int rate, double start, double freq, double length)
{
    const size_t a = static_cast<size_t>(start * rate);
    const size_t n = static_cast<size_t>(length * rate);
    if (out.size() < a + n) out.resize(a + n, 0.0f);
    for (size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) / rate;
        const double env = std::min(1.0, t / 0.004) * std::exp(-t * 3.2);
        const double v = std::sin(2 * kPi * freq * t) + 0.35 * std::sin(2 * kPi * freq * 2.0 * t) * std::exp(-t * 6) +
                         0.15 * std::sin(2 * kPi * freq * 3.0 * t) * std::exp(-t * 9);
        out[a + k] += static_cast<float>(env * v);
    }
}

} // namespace

std::shared_ptr<PcmBuffer> makeCabinChime(int rate)
{
    std::vector<float> mix;
    addTone(mix, rate, 0.0, 1318.5, 1.1);   // E6
    addTone(mix, rate, 0.55, 1046.5, 1.6);  // C6
    float peak = 0;
    for (float v : mix) peak = std::max(peak, std::fabs(v));
    auto pcm = std::make_shared<PcmBuffer>();
    pcm->rate = rate;
    pcm->samples.reserve(mix.size());
    const float scale = peak > 0 ? 0.7f / peak : 0.0f;
    for (float v : mix) pcm->samples.push_back(static_cast<int16_t>(std::lrint(v * scale * 32767.0f)));
    return pcm;
}

} // namespace fxa
