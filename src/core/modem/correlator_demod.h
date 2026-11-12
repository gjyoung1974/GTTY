// Joint-timing character correlator ("ML" decoder in the Gritty style).
//
// The front end is the same as the matched-filter engine (complex mixers at
// mark and space, short filters, ATC normalization) and yields one soft
// decision sample d in [-1, 1] per input sample. Instead of triggering on the
// first mark->space edge, the decoder keeps the recent d(t) history and, for
// every candidate start instant, correlates a whole 7-bit character
// (start bit, five data bits, one stop bit) against all 32 Baudot templates.
// The start time and code with the best correlation win, so a noise spike
// cannot start a character early and the whole character's energy decides
// each bit. The start and stop bits are known symbols and must agree.
#pragma once
#include <complex>
#include <vector>

#include "core/baudot.h"
#include "core/modem/rtty_demodulator.h"

namespace gtty {

class CorrelatorDemod : public RttyDemodulator {
 public:
  explicit CorrelatorDemod(const DemodConfig& c, const char* name = "Correlator");

  const char* name() const override { return name_; }
  void configure(const DemodConfig& c) override;
  const DemodConfig& config() const override { return cfg_; }
  void process(const float* in, size_t n) override;
  void reset() override;
  void resetAfc() override;
  DemodStatus status() const override;
  void takeScope(std::vector<ScopePoint>& out) override;
  uint64_t latencySamples() const override { return static_cast<uint64_t>(1.5 * spb_); }

 private:
  using cf = std::complex<float>;
  struct Channel {
    double phase = 0, inc = 0;
    std::vector<cf> delay;
    size_t pos = 0;
    cf last{};
    float env = 0, peak = 1e-3f;
    void setFreq(double f, double fs) { inc = 2.0 * M_PI * f / fs; }
  };

  cf runChannel(Channel& ch, float x);
  double sum(uint64_t a, uint64_t b) const;        // sum of d over [a, b)
  double sumFreq(uint64_t a, uint64_t b) const;    // sum of instantaneous frequency error over [a, b)
  bool evaluate(uint64_t tau, uint8_t& code, float& score, float& start, float& stop) const;
  void search();
  void accept(uint64_t tau, uint8_t code, float score);

  const char* name_;
  DemodConfig cfg_;
  std::vector<float> taps_;
  Channel mark_, space_;
  double spb_ = 1;
  float peakDecay_ = 0;
  double afcOffset_ = 0;

  // Cumulative sums in a ring, indexed by absolute sample number.
  static constexpr size_t kRing = 1 << 14;   // > 9 bits at 300 baud / 12 kHz... and 45 baud needs ~2400
  std::vector<double> cumD_, cumF_;
  uint64_t pos_ = 0;         // samples processed (next index)
  uint64_t cursor_ = 0;      // next candidate start instant to evaluate
  size_t step_ = 1;          // candidate spacing in samples

  float quality_ = 0;
  uint64_t chars_ = 0, framingErrors_ = 0;
  BaudotDecoder baudot_;
  std::vector<ScopePoint> scope_;
  int scopeDecim_ = 0;
};

}  // namespace gtty
