#include "audio/muffle.h"

#include <algorithm>
#include <cmath>

namespace fxa {

std::vector<int16_t> lowPass(const std::vector<int16_t>& in, int rate,
                             float cutoffHz) {
  std::vector<int16_t> out(in.size());
  if (in.empty() || rate <= 0) return out;
  // rbj cookbook lowpass q 0.707 butterworth direct form 1 in doubles
  constexpr double kPi = 3.14159265358979323846;
  const double fc = std::clamp(
      static_cast<double>(std::isfinite(cutoffHz) ? cutoffHz : 800.0f), 20.0,
      0.45 * rate);
  const double w0 = 2.0 * kPi * fc / rate;
  const double cw = std::cos(w0),
               alpha = std::sin(w0) / (2.0 * (1.0 / std::sqrt(2.0)));
  const double a0 = 1.0 + alpha;
  const double b0 = (1.0 - cw) / 2.0 / a0, b1 = (1.0 - cw) / a0, b2 = b0;
  const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (size_t k = 0; k < in.size(); ++k) {
    const double x = in[k];
    double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    if (std::fabs(y) < 1e-9) y = 0;  // no denormals in long silence
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    out[k] = static_cast<int16_t>(std::clamp(std::lround(y), -32768L, 32767L));
  }
  return out;
}

}  // namespace fxa
