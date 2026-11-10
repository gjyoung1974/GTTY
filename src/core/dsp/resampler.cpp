#include "core/dsp/resampler.h"

#include <samplerate.h>

#include <stdexcept>

namespace gtty {

Resampler::Resampler(double inRate, double outRate, int quality) : ratio_(outRate / inRate) {
  int err = 0;
  state_ = src_new(quality, 1, &err);
  if (!state_) throw std::runtime_error(std::string("src_new: ") + src_strerror(err));
}

Resampler::~Resampler() {
  if (state_) src_delete(state_);
}

void Resampler::reset() {
  if (state_) src_reset(state_);
}

void Resampler::process(const float* in, size_t n, std::vector<float>& out) {
  if (identity()) { out.insert(out.end(), in, in + n); return; }
  size_t maxOut = static_cast<size_t>(n * ratio_) + 16;
  tmp_.resize(maxOut);
  SRC_DATA d{};
  d.data_in = in;
  d.input_frames = static_cast<long>(n);
  d.data_out = tmp_.data();
  d.output_frames = static_cast<long>(maxOut);
  d.src_ratio = ratio_;
  d.end_of_input = 0;
  int err = src_process(state_, &d);
  if (err) throw std::runtime_error(std::string("src_process: ") + src_strerror(err));
  out.insert(out.end(), tmp_.begin(), tmp_.begin() + d.output_frames_gen);
}

}  // namespace gtty
