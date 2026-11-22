// The HF channel simulator: mean power kept, Rayleigh fading at the wanted depth,
// repeatable from its seed, and noise at the level the signal-to-noise ratio asks for.
#include <cmath>
#include <vector>

#include "core/dsp/hf_channel.h"
#include "check.h"

using namespace gtty;

namespace {
std::vector<float> tone(double hz, double fs, double seconds) {
  std::vector<float> x(static_cast<size_t>(fs * seconds));
  for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(std::sin(2.0 * M_PI * hz * i / fs));
  return x;
}

// Mean power in consecutive 20 ms blocks.
std::vector<double> blockPower(const std::vector<float>& x, double fs) {
  const size_t n = static_cast<size_t>(fs * 0.02);
  std::vector<double> out;
  for (size_t i = 0; i + n <= x.size(); i += n) {
    double p = 0;
    for (size_t k = 0; k < n; ++k) p += x[i + k] * x[i + k];
    out.push_back(p / n);
  }
  return out;
}
}  // namespace

int main() {
  const double fs = 8000;
  const auto x = tone(2125, fs, 60);

  // Every profile can be found by its key; an unknown key cannot.
  for (const auto& p : hfChannelProfiles()) CHECK(findHfChannelProfile(p.key) == &p);
  CHECK(findHfChannelProfile("nonsense") == nullptr);

  // No fading: the signal passes unchanged.
  CHECK(applyHfChannel(x, fs, *findHfChannelProfile("awgn"), 1) == x);

  // Fast fading (10 Hz spread), so a minute holds hundreds of independent fades.
  const HfChannelProfile& flutter = *findHfChannelProfile("high-moderate");
  const auto y = applyHfChannel(x, fs, flutter, 1);
  CHECK_EQ(y.size(), x.size());
  const auto power = blockPower(y, fs);
  double mean = 0;
  for (double p : power) mean += p;
  mean /= static_cast<double>(power.size());
  std::printf("mean power %.3f (tone alone 0.5)\n", mean);
  CHECK(mean > 0.42 && mean < 0.58);
  // Rayleigh fading: the power is exponentially distributed, so it is below a tenth of
  // its mean 1 - exp(-0.1) = 9.5 % of the time and above twice its mean 13.5 % of the time.
  // (A 20 ms block averages over the bottom of the fastest fades, so fewer read as deep.)
  size_t deep = 0, high = 0;
  for (double p : power) { if (p < 0.1 * mean) ++deep; if (p > 2.0 * mean) ++high; }
  const double deepShare = static_cast<double>(deep) / power.size(), highShare = static_cast<double>(high) / power.size();
  std::printf("below a tenth of the mean %.1f %%, above twice the mean %.1f %%\n", 100 * deepShare, 100 * highShare);
  CHECK(deepShare > 0.06 && deepShare < 0.13);
  CHECK(highShare > 0.10 && highShare < 0.17);

  // The same seed gives the same channel, another seed another one.
  CHECK(applyHfChannel(x, fs, flutter, 1) == y);
  CHECK(applyHfChannel(x, fs, flutter, 2) != y);

  // Slow fading stays slow: with a 0.1 Hz spread the power hardly moves in 20 ms.
  {
    const auto slow = blockPower(applyHfChannel(x, fs, *findHfChannelProfile("mid-quiet"), 3), fs);
    double change = 0, level = 0;
    for (size_t i = 1; i < slow.size(); ++i) { change += std::fabs(slow[i] - slow[i - 1]); level += slow[i]; }
    std::printf("mid-quiet: mean change between 20 ms blocks %.2f %% of the level\n", 100 * change / level);
    CHECK(change / level < 0.02);
  }

  // 0 dB in 3 kHz against a unit tone (power 0.5): the noise has 0.5 W in 3 kHz, so
  // 0.5 / 3000 * fs / 2 over the whole band.
  {
    std::vector<float> n(static_cast<size_t>(fs * 20), 0.f);
    addWhiteNoise(n, fs, 0.0, 5);
    double p = 0;
    for (float v : n) p += v * v;
    p /= static_cast<double>(n.size());
    const double want = 0.5 / 3000.0 * fs / 2.0;
    std::printf("noise power %.4f, wanted %.4f\n", p, want);
    CHECK(std::fabs(p / want - 1.0) < 0.03);
  }
  TEST_MAIN_END();
}
