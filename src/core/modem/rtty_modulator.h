// AFSK RTTY modulator: queues 5-bit Baudot codes and generates a
// phase-continuous two-tone FSK audio signal with start/stop framing.
// queueCode() may be called from one thread while generate() runs in the
// audio thread (lock-free).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/ring_buffer.h"
#include "core/rtty_params.h"

namespace gtty {

class RttyModulator {
 public:
  enum class Idle { Silence, Mark, Diddle };

  RttyModulator(double sampleRate, const RttyParams& p);

  void setParams(const RttyParams& p);
  void setSampleRate(double fs);
  void setIdleMode(Idle m) { idle_.store(static_cast<int>(m)); }
  Idle idleMode() const { return static_cast<Idle>(idle_.load()); }
  void setAmplitude(float a) { amplitude_.store(a); }

  // Producer side.
  bool queueCode(uint8_t code) { return codes_.push(&code, 1) == 1; }
  size_t pending() const { return codes_.size(); }
  void clearQueue() { codes_.clear(); }

  // Consumer (audio thread) side.
  void generate(float* out, size_t n);
  // True while a character (or its stop bits) is still being sent.
  bool busy() const { return busy_.load(); }
  // Codes whose transmission has started, for echoing to the screen.
  size_t takeSentCodes(uint8_t* out, size_t max) { return sent_.pop(out, max); }

 private:
  void startBit(double freq, double lengthBits);
  bool nextBit();  // returns false if nothing to send

  double fs_;
  RttyParams p_;
  std::atomic<int> idle_{static_cast<int>(Idle::Diddle)};
  std::atomic<float> amplitude_{0.5f};
  std::atomic<bool> busy_{false};

  SpscRing<uint8_t> codes_{1 << 12};
  SpscRing<uint8_t> sent_{1 << 12};

  // Current character / bit state.
  int bitIndex_ = -1;  // -1 idle, 0 start, 1..5 data, 6 stop
  uint8_t code_ = 0;
  double bitRemain_ = 0;  // samples left in current bit (fractional)
  double phase_ = 0;
  double phaseInc_ = 0;
  float env_ = 0;  // output envelope for click-free keying
  bool toneOn_ = false;
};

}  // namespace gtty
