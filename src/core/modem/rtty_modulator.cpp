#include "core/modem/rtty_modulator.h"

#include <cmath>

namespace gtty {

RttyModulator::RttyModulator(double sampleRate, const RttyParams& p) : fs_(sampleRate), p_(p) {}

void RttyModulator::setParams(const RttyParams& p) { p_ = p; }
void RttyModulator::setSampleRate(double fs) { fs_ = fs; }

void RttyModulator::startBit(double freq, double lengthBits) {
  phaseInc_ = 2.0 * M_PI * freq / fs_;
  bitRemain_ += lengthBits * fs_ / p_.baud;  // accumulate to keep fractional timing exact
  toneOn_ = true;
}

bool RttyModulator::nextBit() {
  const double mark = p_.markFreq, space = p_.spaceFreq();
  if (bitIndex_ < 0 || bitIndex_ >= 6) {
    // Need a new character.
    uint8_t c;
    if (codes_.pop(&c, 1) == 1) {
      code_ = c & 0x1f;
      sent_.push(&code_, 1);
      bitIndex_ = 0;
      busy_.store(true);
      startBit(space, 1.0);  // start bit = space
      return true;
    }
    busy_.store(false);
    switch (idleMode()) {
      case Idle::Silence:
        bitIndex_ = -1;
        toneOn_ = false;
        bitRemain_ = 0;
        return false;
      case Idle::Mark:
        bitIndex_ = -1;
        startBit(mark, 1.0);
        return true;
      case Idle::Diddle:
        code_ = 0x1f;  // LTRS
        bitIndex_ = 0;
        startBit(space, 1.0);
        return true;
    }
    return false;
  }
  ++bitIndex_;
  if (bitIndex_ <= 5) {
    bool one = (code_ >> (bitIndex_ - 1)) & 1;
    startBit(one ? mark : space, 1.0);
  } else {
    startBit(mark, p_.stopBits);  // stop bit(s) = mark
  }
  return true;
}

void RttyModulator::generate(float* out, size_t n) {
  const float amp = amplitude_.load();
  const float ramp = static_cast<float>(1.0 / (0.004 * fs_));  // 4 ms attack/decay
  for (size_t i = 0; i < n; ++i) {
    if (bitRemain_ < 1.0) {
      if (!nextBit()) {
        // Idle silence: decay envelope, keep running phase for continuity.
        env_ = std::max(0.f, env_ - ramp);
        out[i] = env_ > 0 ? amp * env_ * static_cast<float>(std::sin(phase_)) : 0.f;
        phase_ += phaseInc_;
        if (phase_ > 2 * M_PI) phase_ -= 2 * M_PI;
        continue;
      }
    }
    bitRemain_ -= 1.0;
    env_ = std::min(1.f, env_ + ramp);
    out[i] = amp * env_ * static_cast<float>(std::sin(phase_));
    phase_ += phaseInc_;
    if (phase_ > 2 * M_PI) phase_ -= 2 * M_PI;
  }
}

}  // namespace gtty
