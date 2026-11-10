#include "core/modem/codec2_demod.h"

#include <algorithm>
#include <cmath>

#ifdef GTTY_HAVE_CODEC2
extern "C" {
#include <codec2/fsk.h>
}
#endif

namespace gtty {

bool codec2Available() {
#ifdef GTTY_HAVE_CODEC2
  return true;
#else
  return false;
#endif
}

Codec2Demod::Codec2Demod(const DemodConfig& c) : baudot_(c.params.variant) { configure(c); }
Codec2Demod::~Codec2Demod() { destroy(); }

void Codec2Demod::destroy() {
#ifdef GTTY_HAVE_CODEC2
  if (fsk_) fsk_destroy(fsk_);
#endif
  fsk_ = nullptr;
}

void Codec2Demod::configure(const DemodConfig& c) {
  cfg_ = c;
  destroy();
  baudot_.setVariant(cfg_.params.variant);
  baudot_.setUnshiftOnSpace(cfg_.params.unshiftOnSpace);
  rs_ = static_cast<int>(std::lround(2.0 * cfg_.params.baud));   // half-bit symbols
  ts_ = 80;                                                        // samples per symbol
  fs_ = rs_ * ts_;
  nsym_ = 24;                                                      // symbols per demod call (~12 bits)
  markIsHigh_ = cfg_.params.reverse;   // default RTTY: space above mark
  double lo = std::min(cfg_.params.markFreq, cfg_.params.spaceFreq());
#ifdef GTTY_HAVE_CODEC2
  fsk_ = fsk_create_hbr(fs_, rs_, 2, 8, nsym_, static_cast<int>(std::lround(lo)),
                        static_cast<int>(std::lround(cfg_.params.shift)));
  if (fsk_) {
    int margin = static_cast<int>(std::max(50.0, cfg_.afcRangeHz));
    fsk_set_freq_est_limits(fsk_, std::max(50, static_cast<int>(lo) - margin),
                            std::min(fs_ / 2 - 50, static_cast<int>(lo + cfg_.params.shift) + margin));
    frameSamples_ = fsk_nin(fsk_);
  }
#endif
  resampler_ = std::make_unique<Resampler>(cfg_.sampleRate, fs_);
  inBuf_.clear();
  syms_.clear();
  collecting_ = false;
  prevSym_ = 1.f;
}

void Codec2Demod::reset() {
  inBuf_.clear();
  syms_.clear();
  collecting_ = false;
  prevSym_ = 1.f;
  quality_ = 0;
  baudot_.reset();
#ifdef GTTY_HAVE_CODEC2
  if (fsk_) fsk_clear_estimators(fsk_);
#endif
}

void Codec2Demod::resetAfc() {
#ifdef GTTY_HAVE_CODEC2
  if (fsk_) fsk_clear_estimators(fsk_);
#endif
  afcHz_ = 0;
}

DemodStatus Codec2Demod::status() const {
  DemodStatus s;
  s.afcOffsetHz = afcHz_;
  s.quality = quality_;
  s.markLevel = markLevel_;
  s.spaceLevel = spaceLevel_;
  s.squelched = cfg_.squelch > 0 && quality_ < cfg_.squelch;
  s.chars = chars_;
  s.framingErrors = framingErrors_;
  return s;
}

void Codec2Demod::takeScope(std::vector<ScopePoint>& out) {
  out.insert(out.end(), scope_.begin(), scope_.end());
  scope_.clear();
}

uint64_t Codec2Demod::latencySamples() const {
  // A whole demod frame plus the resampler, on the input clock.
  return static_cast<uint64_t>((frameSamples_ + 2.0 * ts_) * cfg_.sampleRate / std::max(1, fs_)) + 64;
}

void Codec2Demod::process(const float* in, size_t n) {
  pos_ += n;
#ifdef GTTY_HAVE_CODEC2
  if (!fsk_) return;
  resampled_.clear();
  resampler_->process(in, n, resampled_);
  inBuf_.insert(inBuf_.end(), resampled_.begin(), resampled_.end());
  while (inBuf_.size() >= frameSamples_) frame();
#endif
}

void Codec2Demod::frame() {
#ifdef GTTY_HAVE_CODEC2
  std::vector<COMP> x(frameSamples_);
  for (size_t i = 0; i < frameSamples_; ++i) { x[i].real = inBuf_[i] * FSK_SCALE; x[i].imag = 0.f; }
  std::vector<float> filt(static_cast<size_t>(nsym_) * 2);
  fsk_demod_sd(fsk_, filt.data(), x.data());
  // Tone estimates: tone 0 is the lower frequency.
  double fLow = fsk_->f_est[0], fHigh = fsk_->f_est[1];
  double markEst = markIsHigh_ ? fHigh : fLow;
  if (markEst > 0) afcHz_ = markEst - cfg_.params.markFreq;
  // Each symbol i ended at input-clock sample framePos_ + (i+1)*ts_ scaled back to cfg_.sampleRate.
  double scale = cfg_.sampleRate / fs_;
  float sumMark = 0, sumSpace = 0;
  for (int i = 0; i < nsym_; ++i) {
    float low = filt[i], high = filt[static_cast<size_t>(nsym_) + i];
    float m = markIsHigh_ ? high : low, s = markIsHigh_ ? low : high;
    sumMark += m; sumSpace += s;
    float soft = (m - s) / (m + s + 1e-6f);
    if (scope_.size() < 4096) scope_.push_back({std::min(1.f, m / (m + s + 1e-6f) * 1.4f), std::min(1.f, s / (m + s + 1e-6f) * 1.4f)});
    uint64_t end = framePos_ + static_cast<uint64_t>((i + 1) * ts_ * scale);
    symbol(soft, end);
  }
  float tot = sumMark + sumSpace + 1e-6f;
  markLevel_ = std::min(1.f, sumMark / tot * 2);
  spaceLevel_ = std::min(1.f, sumSpace / tot * 2);
  framePos_ += static_cast<uint64_t>(frameSamples_ * scale);
  inBuf_.erase(inBuf_.begin(), inBuf_.begin() + static_cast<long>(frameSamples_));
  frameSamples_ = fsk_nin(fsk_);
#endif
}

// Half-bit symbol framer: start = 2 space symbols after a mark, 5 data bits of
// 2 symbols each, stop = at least 2 mark symbols (1.5 stop bits = 3 symbols).
void Codec2Demod::symbol(float soft, uint64_t endSample) {
  if (!collecting_) {
    if (prevSym_ > 0 && soft < 0) { collecting_ = true; syms_.clear(); }
    prevSym_ = soft;
    if (!collecting_) return;
  }
  syms_.push_back(soft);
  if (syms_.size() < 14) return;
  collecting_ = false;
  float start = syms_[0] + syms_[1];
  float stop = syms_[12] + syms_[13];
  prevSym_ = syms_[13];
  if (!(start < -0.1f && stop > 0.1f)) { framingErrors_++; return; }
  uint8_t code = 0;
  float conf = 0;
  for (int b = 0; b < 5; ++b) {
    float v = syms_[2 + 2 * b] + syms_[3 + 2 * b];
    if (v > 0) code |= static_cast<uint8_t>(1u << b);
    conf += std::min(1.f, std::fabs(v) / 2.f);
  }
  conf /= 5.f;
  chars_++;
  quality_ = 0.8f * quality_ + 0.2f * conf;
  charEnd_ = endSample;
  bool squelched = cfg_.squelch > 0 && quality_ < cfg_.squelch;
  if (onCode) onCode(CodeEvent{code, true, squelched ? 0.f : conf, endSample});
  if (squelched) return;
  auto ch = baudot_.decode(code);
  if (ch && onChar) onChar(*ch);
}

}  // namespace gtty
