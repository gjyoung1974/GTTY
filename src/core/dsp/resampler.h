// Streaming sample-rate converter (libsamplerate).
#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct SRC_STATE_tag;

namespace gtty {

class Resampler {
 public:
  Resampler(double inRate, double outRate, int quality = 2 /* SRC_SINC_FASTEST */);
  ~Resampler();
  Resampler(const Resampler&) = delete;
  Resampler& operator=(const Resampler&) = delete;

  double ratio() const { return ratio_; }
  bool identity() const { return ratio_ == 1.0; }

  // Converts `n` mono samples and appends the result to `out`.
  void process(const float* in, size_t n, std::vector<float>& out);
  void reset();

 private:
  SRC_STATE_tag* state_ = nullptr;
  double ratio_;
  std::vector<float> tmp_;
};

}  // namespace gtty
