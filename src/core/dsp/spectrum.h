// Windowed power spectrum for the waterfall display (FFTW, single precision).
#pragma once
#include <complex>
#include <cstddef>
#include <vector>

#include <fftw3.h>

namespace gtty {

class SpectrumAnalyzer {
 public:
  // fftSize must be even. Produces fftSize/2 + 1 bins from 0 to sampleRate/2.
  SpectrumAnalyzer(size_t fftSize, double sampleRate, double overlap = 0.5);
  ~SpectrumAnalyzer();
  SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
  SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;

  size_t bins() const { return fftSize_ / 2 + 1; }
  double binHz() const { return sampleRate_ / static_cast<double>(fftSize_); }

  // Feed samples; returns true each time a new spectrum frame is ready in power().
  // Averages frames computed in between (call power() after each true).
  bool process(const float* in, size_t n, std::vector<float>& powerDb);

 private:
  void compute(std::vector<float>& powerDb);
  size_t fftSize_;
  double sampleRate_;
  size_t hop_;
  std::vector<float> window_;
  std::vector<float> buf_;  // sample FIFO
  size_t filled_ = 0;
  float* in_ = nullptr;
  fftwf_complex* out_ = nullptr;
  fftwf_plan plan_{};
};

}  // namespace gtty
