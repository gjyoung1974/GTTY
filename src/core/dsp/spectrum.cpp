#include "core/dsp/spectrum.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gtty {

SpectrumAnalyzer::SpectrumAnalyzer(size_t fftSize, double sampleRate, double overlap)
    : fftSize_(fftSize), sampleRate_(sampleRate) {
  hop_ = std::max<size_t>(1, static_cast<size_t>(fftSize * (1.0 - overlap)));
  window_.resize(fftSize);
  // Blackman-Harris window: low sidelobes so strong signals don't smear.
  for (size_t i = 0; i < fftSize; ++i) {
    double x = 2.0 * M_PI * i / (fftSize - 1);
    window_[i] = static_cast<float>(0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) -
                                    0.01168 * std::cos(3 * x));
  }
  buf_.assign(fftSize, 0.f);
  in_ = fftwf_alloc_real(fftSize);
  out_ = fftwf_alloc_complex(fftSize / 2 + 1);
  plan_ = fftwf_plan_dft_r2c_1d(static_cast<int>(fftSize), in_, out_, FFTW_ESTIMATE);
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
  fftwf_destroy_plan(plan_);
  fftwf_free(in_);
  fftwf_free(out_);
}

bool SpectrumAnalyzer::process(const float* in, size_t n, std::vector<float>& powerDb) {
  bool ready = false;
  size_t i = 0;
  while (i < n) {
    size_t take = std::min(n - i, fftSize_ - filled_);
    std::memcpy(buf_.data() + filled_, in + i, take * sizeof(float));
    filled_ += take;
    i += take;
    if (filled_ == fftSize_) {
      compute(powerDb);
      ready = true;
      // slide by hop
      std::memmove(buf_.data(), buf_.data() + hop_, (fftSize_ - hop_) * sizeof(float));
      filled_ = fftSize_ - hop_;
    }
  }
  return ready;
}

void SpectrumAnalyzer::compute(std::vector<float>& powerDb) {
  for (size_t i = 0; i < fftSize_; ++i) in_[i] = buf_[i] * window_[i];
  fftwf_execute(plan_);
  powerDb.resize(bins());
  const float norm = 1.0f / (static_cast<float>(fftSize_) * 0.5f);
  for (size_t k = 0; k < bins(); ++k) {
    float re = out_[k][0] * norm, im = out_[k][1] * norm;
    float p = re * re + im * im;
    powerDb[k] = 10.0f * std::log10(p + 1e-12f);
  }
}

}  // namespace gtty
