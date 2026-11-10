// Classic high-performance RTTY demodulator in the MMTTY style:
//   complex mixers at mark/space -> windowed matched filters (one bit long)
//   -> envelope detectors -> ATC (per-tone peak normalisation) -> slicer
//   -> asynchronous start/stop framing with soft bit integration
//   -> AFC from baseband frequency discriminators -> Baudot decoder.
#pragma once
#include <complex>
#include <vector>

#include "core/baudot.h"
#include "core/modem/rtty_demodulator.h"

namespace gtty {

class MatchedFilterDemod : public RttyDemodulator {
 public:
  explicit MatchedFilterDemod(const DemodConfig& c, const char* name = "Matched");

  const char* name() const override { return name_; }
  void configure(const DemodConfig& c) override;
  const DemodConfig& config() const override { return cfg_; }
  void process(const float* in, size_t n) override;
  void reset() override;
  void resetAfc() override;
  DemodStatus status() const override;
  void takeScope(std::vector<ScopePoint>& out) override;
  // Absolute signal level (sum of the mark and space peak followers), for the skimmer.
  float level() const { return mark_.peak + space_.peak; }

 private:
  using cf = std::complex<float>;

  struct Channel {
    double phase = 0, inc = 0;
    std::vector<cf> delay;  // circular buffer for the FIR
    size_t pos = 0;
    cf last{};              // last filter output (for discriminator)
    float env = 0;
    float peak = 1e-3f;
    double freq = 0;
    void setFreq(double f, double fs) { freq = f; inc = 2.0 * M_PI * f / fs; }
  };

  void designFilter();
  cf runChannel(Channel& ch, float x);
  void slicerSample(float d);
  void finishBit(float mean);
  void applyAfc();

  const char* name_;
  DemodConfig cfg_;
  std::vector<float> taps_;
  Channel mark_, space_;
  double spb_ = 1;     // samples per bit
  float peakDecay_ = 0;

  // UART framing state
  enum class St { Idle, Receiving } st_ = St::Idle;
  double t_ = 0;
  int bit_ = 0;
  double acc_ = 0;
  int accN_ = 0;
  uint8_t code_ = 0;
  bool prevMark_ = true;
  double window_ = 0;   // half-width of the integration window (samples)

  // AFC accumulators
  double afcSum_ = 0;
  int afcN_ = 0;
  double afcOffset_ = 0;
  double bitFreqSum_ = 0;
  int bitFreqN_ = 0;

  // quality / squelch
  float quality_ = 0;
  float confSum_ = 0;        // sum of |bit mean| over the data bits of the current character
  uint64_t pos_ = 0;         // input samples processed
  uint64_t chars_ = 0, framingErrors_ = 0;

  BaudotDecoder baudot_;
  std::vector<ScopePoint> scope_;
  int scopeDecim_ = 0;
};

}  // namespace gtty
