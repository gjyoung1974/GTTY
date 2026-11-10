// RTTY through libcodec2's FSK demodulator, for comparison with the in-house
// engines. codec2 is a synchronous modem, so it runs at twice the RTTY baud
// rate: every RTTY element (1 start, 5 data, 1.5 stop bits) is then a whole
// number of half-bit symbols and its symbol-timing tracker can lock. A small
// UART framer on the half-bit soft decisions recovers the characters.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "core/baudot.h"
#include "core/dsp/resampler.h"
#include "core/modem/rtty_demodulator.h"

struct FSK;

namespace gtty {

bool codec2Available();

class Codec2Demod : public RttyDemodulator {
 public:
  explicit Codec2Demod(const DemodConfig& c);
  ~Codec2Demod() override;
  Codec2Demod(const Codec2Demod&) = delete;
  Codec2Demod& operator=(const Codec2Demod&) = delete;

  const char* name() const override { return "codec2"; }
  void configure(const DemodConfig& c) override;
  const DemodConfig& config() const override { return cfg_; }
  void process(const float* in, size_t n) override;
  void reset() override;
  void resetAfc() override;
  DemodStatus status() const override;
  void takeScope(std::vector<ScopePoint>& out) override;
  uint64_t latencySamples() const override;

 private:
  void destroy();
  void frame();                           // one fsk_demod cycle on inBuf_
  void symbol(float soft, uint64_t endSample);

  DemodConfig cfg_;
  FSK* fsk_ = nullptr;
  int fs_ = 0, rs_ = 0, nsym_ = 0, ts_ = 0;
  bool markIsHigh_ = true;
  std::unique_ptr<Resampler> resampler_;
  std::vector<float> inBuf_, resampled_;
  size_t frameSamples_ = 0;
  uint64_t pos_ = 0;                      // input samples (cfg_.sampleRate clock)
  uint64_t framePos_ = 0;                 // input-clock sample index at the start of inBuf_
  // half-bit symbol framer
  std::vector<float> syms_;               // soft symbols of the character being assembled (+ = mark)
  float prevSym_ = 1.f;
  bool collecting_ = false;
  uint64_t charEnd_ = 0;
  float quality_ = 0, markLevel_ = 0, spaceLevel_ = 0;
  double afcHz_ = 0;
  uint64_t chars_ = 0, framingErrors_ = 0;
  BaudotDecoder baudot_;
  std::vector<ScopePoint> scope_;
};

}  // namespace gtty
