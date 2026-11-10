#include "core/modem/matched_filter_demod.h"

#include <algorithm>
#include <cmath>

namespace gtty {

MatchedFilterDemod::MatchedFilterDemod(const DemodConfig& c, const char* name)
    : name_(name), baudot_(c.params.variant) {
  configure(c);
}

void MatchedFilterDemod::configure(const DemodConfig& c) {
  cfg_ = c;
  spb_ = cfg_.sampleRate / cfg_.params.baud;
  window_ = spb_ * std::clamp(cfg_.bitWindow, 0.05, 0.95) / 2.0;  // half-width of the integration window
  peakDecay_ = static_cast<float>(std::exp(-1.0 / (std::max(0.02, cfg_.atcTauSec) * cfg_.sampleRate)));
  baudot_.setVariant(cfg_.params.variant);
  baudot_.setUnshiftOnSpace(cfg_.params.unshiftOnSpace);
  designFilter();
  mark_.setFreq(cfg_.params.markFreq + afcOffset_, cfg_.sampleRate);
  space_.setFreq(cfg_.params.spaceFreq() + afcOffset_, cfg_.sampleRate);
  mark_.delay.assign(taps_.size(), cf{});
  space_.delay.assign(taps_.size(), cf{});
  mark_.pos = space_.pos = 0;
  scopeDecim_ = std::max(1, static_cast<int>(cfg_.sampleRate / 3000.0));
}

void MatchedFilterDemod::designFilter() {
  size_t L = std::max<size_t>(8, static_cast<size_t>(std::lround(cfg_.filterLengthBits * spb_)));
  taps_.resize(L);
  double sum = 0;
  for (size_t i = 0; i < L; ++i) {
    double w = 1.0;
    if (cfg_.matchedWindow == 1) {
      // Hann (raised cosine) window over the bit period.
      w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (i + 0.5) / L);
    }
    taps_[i] = static_cast<float>(w);
    sum += w;
  }
  for (auto& t : taps_) t = static_cast<float>(t / sum * 2.0);  // *2: recover tone amplitude from one sideband
}

void MatchedFilterDemod::reset() {
  st_ = St::Idle;
  t_ = 0; bit_ = 0; acc_ = 0; accN_ = 0; code_ = 0; prevMark_ = true;
  mark_.peak = space_.peak = 1e-3f;
  quality_ = 0;
  baudot_.reset();
  std::fill(mark_.delay.begin(), mark_.delay.end(), cf{});
  std::fill(space_.delay.begin(), space_.delay.end(), cf{});
}

void MatchedFilterDemod::resetAfc() {
  afcOffset_ = 0;
  afcSum_ = 0; afcN_ = 0;
  mark_.setFreq(cfg_.params.markFreq, cfg_.sampleRate);
  space_.setFreq(cfg_.params.spaceFreq(), cfg_.sampleRate);
}

DemodStatus MatchedFilterDemod::status() const {
  DemodStatus s;
  s.afcOffsetHz = afcOffset_;
  s.quality = quality_;
  float tot = mark_.peak + space_.peak + 1e-9f;
  s.markLevel = std::min(1.f, mark_.env / tot * 2);
  s.spaceLevel = std::min(1.f, space_.env / tot * 2);
  s.squelched = cfg_.squelch > 0 && quality_ < cfg_.squelch;
  s.chars = chars_;
  s.framingErrors = framingErrors_;
  return s;
}

void MatchedFilterDemod::takeScope(std::vector<ScopePoint>& out) {
  out.insert(out.end(), scope_.begin(), scope_.end());
  scope_.clear();
}

inline MatchedFilterDemod::cf MatchedFilterDemod::runChannel(Channel& ch, float x) {
  // Mix to baseband.
  cf lo(static_cast<float>(std::cos(ch.phase)), static_cast<float>(-std::sin(ch.phase)));
  ch.phase += ch.inc;
  if (ch.phase > 2 * M_PI) ch.phase -= 2 * M_PI;
  ch.delay[ch.pos] = x * lo;
  // FIR (matched filter), circular buffer.
  const size_t L = taps_.size();
  cf acc{};
  size_t idx = ch.pos;
  for (size_t k = 0; k < L; ++k) {
    acc += taps_[k] * ch.delay[idx];
    idx = (idx == 0) ? L - 1 : idx - 1;
  }
  ch.pos = (ch.pos + 1 == L) ? 0 : ch.pos + 1;
  ch.env = std::abs(acc);
  ch.peak = std::max(ch.env, ch.peak * peakDecay_);
  ch.peak = std::max(ch.peak, 1e-4f);
  return acc;
}

void MatchedFilterDemod::process(const float* in, size_t n) {
  for (size_t i = 0; i < n; ++i, ++pos_) {
    float x = in[i];
    cf zm = runChannel(mark_, x);
    cf zs = runChannel(space_, x);

    // Scope: real bandpass outputs re-modulated to their tone frequencies.
    if (++scopeDecim_ >= std::max(1, static_cast<int>(cfg_.sampleRate / 3000.0))) {
      scopeDecim_ = 0;
      float tot = mark_.peak + space_.peak + 1e-9f;
      float xm = std::real(zm * cf(static_cast<float>(std::cos(mark_.phase)), static_cast<float>(std::sin(mark_.phase)))) / tot * 2;
      float ys = std::real(zs * cf(static_cast<float>(std::cos(space_.phase)), static_cast<float>(std::sin(space_.phase)))) / tot * 2;
      scope_.push_back({xm, ys});
      if (scope_.size() > 4096) scope_.erase(scope_.begin(), scope_.begin() + 2048);
    }

    // Decision variable in [-1, 1]: positive = mark. A clean tone reads close to
    // +-1 on both paths so that quality (and so the squelch slider and the
    // vote weight) means the same thing with ATC on or off.
    float d;
    if (cfg_.atc) {
      d = mark_.env / mark_.peak - space_.env / space_.peak;
    } else {
      float tot = std::max(mark_.peak, space_.peak);
      d = (mark_.env - space_.env) / (tot + 1e-9f);
    }
    d = std::clamp(d, -1.f, 1.f);

    // Instantaneous frequency (Hz) of each channel for AFC, measured on the
    // channel currently carrying the tone.
    if (cfg_.afc && st_ == St::Receiving && accN_ >= 0) {
      const cf& z = d >= 0 ? zm : zs;
      const cf& zl = d >= 0 ? mark_.last : space_.last;
      if (std::abs(zl) > 0 && std::abs(z) > 0) {
        float dphi = std::arg(z * std::conj(zl));
        bitFreqSum_ += dphi * cfg_.sampleRate / (2.0 * M_PI);
        bitFreqN_++;
      }
    }
    mark_.last = zm;
    space_.last = zs;

    slicerSample(d);
  }
}

void MatchedFilterDemod::slicerSample(float d) {
  if (st_ == St::Idle) {
    bool markNow = d >= 0;
    if (prevMark_ && !markNow) {
      st_ = St::Receiving;
      t_ = 0; bit_ = 0; acc_ = 0; accN_ = 0; code_ = 0; confSum_ = 0;
      afcSum_ = 0; afcN_ = 0;
      bitFreqSum_ = 0; bitFreqN_ = 0;
    }
    prevMark_ = markNow;
    return;
  }
  t_ += 1.0;
  double center = (bit_ + 0.5) * spb_;
  if (std::fabs(t_ - center) <= window_) {
    acc_ += d;
    accN_++;
  }
  if (t_ >= center + window_) {
    float mean = accN_ ? static_cast<float>(acc_ / accN_) : 0.f;
    finishBit(mean);
    acc_ = 0; accN_ = 0;
  }
}

void MatchedFilterDemod::finishBit(float mean) {
  const bool isMark = mean >= 0;
  // Accumulate per-bit frequency estimate (relative to channel LO).
  if (bitFreqN_ > 0 && std::fabs(mean) > 0.25f) {
    afcSum_ += bitFreqSum_ / bitFreqN_;
    afcN_++;
  }
  bitFreqSum_ = 0; bitFreqN_ = 0;

  if (bit_ == 0) {
    if (isMark) {  // false start
      st_ = St::Idle;
      prevMark_ = true;
      return;
    }
    bit_ = 1;
    return;
  }
  if (bit_ <= 5) {
    if (isMark) code_ |= static_cast<uint8_t>(1u << (bit_ - 1));
    // quality: how far from the threshold the data bits sit
    quality_ = 0.9f * quality_ + 0.1f * std::min(1.f, std::fabs(mean));
    confSum_ += std::min(1.f, std::fabs(mean));
    bit_++;
    return;
  }
  // Stop bit.
  st_ = St::Idle;
  prevMark_ = true;
  if (!isMark) {
    framingErrors_++;
    return;
  }
  chars_++;
  applyAfc();
  bool squelched = cfg_.squelch > 0 && quality_ < cfg_.squelch;
  if (onCode) onCode(CodeEvent{code_, true, std::min(1.f, confSum_ / 5.f) * (squelched ? 0.f : 1.f), pos_});
  if (squelched) return;
  auto ch = baudot_.decode(code_);
  if (ch && onChar) onChar(*ch);
}

void MatchedFilterDemod::applyAfc() {
  if (!cfg_.afc || afcN_ == 0) return;
  double err = afcSum_ / afcN_;  // Hz: positive = signal above our LOs
  afcSum_ = 0; afcN_ = 0;
  if (std::fabs(err) > cfg_.params.baud) return;  // outside the discriminator's linear range
  afcOffset_ += cfg_.afcGain * err;
  afcOffset_ = std::clamp(afcOffset_, -cfg_.afcRangeHz, cfg_.afcRangeHz);
  mark_.setFreq(cfg_.params.markFreq + afcOffset_, cfg_.sampleRate);
  space_.setFreq(cfg_.params.spaceFreq() + afcOffset_, cfg_.sampleRate);
}

}  // namespace gtty
