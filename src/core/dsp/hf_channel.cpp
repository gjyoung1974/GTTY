#include "core/dsp/hf_channel.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>

namespace gtty {

namespace {

using cd = std::complex<double>;

// A complex Gaussian process of unit mean power whose spectrum is Gaussian with a
// standard deviation of dopplerHz / 2, one value per input sample. It is made at a low
// rate (32 samples per Hz of spread, far above what so narrow a spectrum needs) by
// filtering white noise, and interpolated linearly up to the audio rate.
std::vector<cd> fadingProcess(size_t n, double fs, double dopplerHz, std::mt19937& rng) {
  const double sigma = dopplerHz / 2.0;
  const double rate = 32.0 * dopplerHz;
  // H(f) = exp(-f^2 / (4 sigma^2)) shapes white noise to the wanted power spectrum;
  // its impulse response is exp(-(2 pi sigma t)^2).
  const double sigmaT = rate / (2.0 * std::sqrt(2.0) * M_PI * sigma);   // in low-rate samples
  const int half = static_cast<int>(std::ceil(4.0 * sigmaT));
  std::vector<double> h(2 * half + 1);
  double energy = 0;
  for (int i = -half; i <= half; ++i) {
    const double t = i / rate;
    h[i + half] = std::exp(-std::pow(2.0 * M_PI * sigma * t, 2.0));
    energy += h[i + half] * h[i + half];
  }
  for (auto& v : h) v /= std::sqrt(energy);

  const size_t m = static_cast<size_t>(std::ceil(n / fs * rate)) + 2;
  std::normal_distribution<double> g(0.0, std::sqrt(0.5));
  std::vector<cd> white(m + h.size());
  for (auto& w : white) { const double re = g(rng); w = cd(re, g(rng)); }
  std::vector<cd> slow(m);
  for (size_t i = 0; i < m; ++i) {
    cd acc{};
    for (size_t k = 0; k < h.size(); ++k) acc += h[k] * white[i + k];
    slow[i] = acc;
  }
  std::vector<cd> out(n);
  for (size_t i = 0; i < n; ++i) {
    const double pos = i / fs * rate;
    const size_t k = static_cast<size_t>(pos);
    const double frac = pos - static_cast<double>(k);
    out[i] = slow[k] * (1.0 - frac) + slow[k + 1] * frac;
  }
  return out;
}

// The quadrature (Hilbert transform) of x, aligned with x: with it x becomes an
// analytic signal that a complex gain can rotate as well as scale. A windowed FIR,
// good from about 150 Hz to the same distance below half the sample rate.
std::vector<double> quadrature(const std::vector<float>& x, double fs) {
  const int half = std::max(31, static_cast<int>(std::lround(fs * 0.0106)) | 1);
  std::vector<double> h(2 * half + 1, 0.0);
  for (int i = -half; i <= half; ++i) {
    if ((i & 1) == 0) continue;
    const double w = 0.42 + 0.5 * std::cos(M_PI * i / (half + 1)) + 0.08 * std::cos(2.0 * M_PI * i / (half + 1));
    h[i + half] = 2.0 / (M_PI * i) * w;
  }
  const long n = static_cast<long>(x.size());
  std::vector<double> q(x.size(), 0.0);
  for (long i = 0; i < n; ++i) {
    double acc = 0;
    // y[i] = sum_k h[k] x[i - k], odd k only.
    for (int k = -half; k <= half; k += 2) {
      const long j = i - k;
      if (j >= 0 && j < n) acc += h[k + half] * x[static_cast<size_t>(j)];
    }
    q[static_cast<size_t>(i)] = acc;
  }
  return q;
}

}  // namespace

const std::vector<HfChannelProfile>& hfChannelProfiles() {
  // Delay and Doppler spread from ITU-R F.1487, Annex 3. The mid-latitude quiet,
  // moderate and disturbed conditions are the "good", "moderate" and "poor" channels of
  // CCIR 520; high-latitude moderate is close to its "flutter fading".
  static const std::vector<HfChannelProfile> profiles = {
      {"awgn", "No fading, white noise only", 0.0, 0.0},
      {"mid-quiet", "Mid latitudes, quiet (CCIR good)", 0.5, 0.1},
      {"mid-moderate", "Mid latitudes, moderate (CCIR moderate)", 1.0, 0.5},
      {"mid-disturbed", "Mid latitudes, disturbed (CCIR poor)", 2.0, 1.0},
      {"low-quiet", "Low latitudes, quiet", 0.5, 0.5},
      {"low-moderate", "Low latitudes, moderate", 2.0, 1.5},
      {"low-disturbed", "Low latitudes, disturbed", 6.0, 10.0},
      {"high-quiet", "High latitudes, quiet", 1.0, 0.5},
      {"high-moderate", "High latitudes, moderate (flutter)", 3.0, 10.0},
      {"high-disturbed", "High latitudes, disturbed", 7.0, 30.0},
  };
  return profiles;
}

const HfChannelProfile* findHfChannelProfile(const std::string& key) {
  for (const auto& p : hfChannelProfiles())
    if (key == p.key) return &p;
  return nullptr;
}

std::vector<float> applyHfChannel(const std::vector<float>& x, double fs, const HfChannelProfile& p,
                                  unsigned seed) {
  if (p.dopplerHz <= 0) return x;
  std::mt19937 rng(seed);
  const size_t n = x.size();
  const std::vector<double> q = quadrature(x, fs);
  const std::vector<cd> g1 = fadingProcess(n, fs, p.dopplerHz, rng);
  const std::vector<cd> g2 = fadingProcess(n, fs, p.dopplerHz, rng);
  const size_t delay = static_cast<size_t>(std::lround(p.delayMs * 1e-3 * fs));
  const double pathGain = std::sqrt(0.5);   // two paths of equal mean power
  std::vector<float> y(n);
  for (size_t i = 0; i < n; ++i) {
    cd v = g1[i] * cd(x[i], q[i]);
    if (i >= delay) v += g2[i] * cd(x[i - delay], q[i - delay]);
    y[i] = static_cast<float>(pathGain * v.real());
  }
  return y;
}

void addWhiteNoise(std::vector<float>& x, double fs, double snrDb3k, unsigned seed, double signalPower) {
  const double density = signalPower / std::pow(10.0, snrDb3k / 10.0) / 3000.0;   // per Hz
  const double sigma = std::sqrt(density * fs / 2.0);
  std::mt19937 rng(seed);
  std::normal_distribution<float> n(0.f, static_cast<float>(sigma));
  for (auto& v : x) v += n(rng);
}

}  // namespace gtty
