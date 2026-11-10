#include "core/modem/correlator_demod.h"

#include <algorithm>
#include <cmath>

namespace gtty {

CorrelatorDemod::CorrelatorDemod(const DemodConfig& c, const char* name)
    : name_(name), baudot_(c.params.variant) {
  cumD_.assign(kRing, 0.0);
  cumF_.assign(kRing, 0.0);
  configure(c);
}

void CorrelatorDemod::configure(const DemodConfig& c) {
  cfg_ = c;
  spb_ = cfg_.sampleRate / cfg_.params.baud;
  step_ = std::max<size_t>(1, static_cast<size_t>(spb_ / 8.0));
  peakDecay_ = static_cast<float>(std::exp(-1.0 / (std::max(0.02, cfg_.atcTauSec) * cfg_.sampleRate)));
  baudot_.setVariant(cfg_.params.variant);
  baudot_.setUnshiftOnSpace(cfg_.params.unshiftOnSpace);
  // Short (half-bit) Hann filter: the character-long correlation supplies the
  // rest of the integration, so the front end can stay wide and fast.
  size_t L = std::max<size_t>(8, static_cast<size_t>(std::lround(0.5 * spb_)));
  taps_.resize(L);
  double s = 0;
  for (size_t i = 0; i < L; ++i) { taps_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * (i + 0.5) / L)); s += taps_[i]; }
  for (auto& t : taps_) t = static_cast<float>(t / s * 2.0);
  mark_.setFreq(cfg_.params.markFreq + afcOffset_, cfg_.sampleRate);
  space_.setFreq(cfg_.params.spaceFreq() + afcOffset_, cfg_.sampleRate);
  mark_.delay.assign(L, cf{});
  space_.delay.assign(L, cf{});
  mark_.pos = space_.pos = 0;
}

void CorrelatorDemod::reset() {
  std::fill(cumD_.begin(), cumD_.end(), 0.0);
  std::fill(cumF_.begin(), cumF_.end(), 0.0);
  pos_ = 0; cursor_ = 0;
  mark_.peak = space_.peak = 1e-3f;
  quality_ = 0;
  baudot_.reset();
  std::fill(mark_.delay.begin(), mark_.delay.end(), cf{});
  std::fill(space_.delay.begin(), space_.delay.end(), cf{});
}

void CorrelatorDemod::resetAfc() {
  afcOffset_ = 0;
  mark_.setFreq(cfg_.params.markFreq, cfg_.sampleRate);
  space_.setFreq(cfg_.params.spaceFreq(), cfg_.sampleRate);
}

DemodStatus CorrelatorDemod::status() const {
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

void CorrelatorDemod::takeScope(std::vector<ScopePoint>& out) {
  out.insert(out.end(), scope_.begin(), scope_.end());
  scope_.clear();
}

inline CorrelatorDemod::cf CorrelatorDemod::runChannel(Channel& ch, float x) {
  cf lo(static_cast<float>(std::cos(ch.phase)), static_cast<float>(-std::sin(ch.phase)));
  ch.phase += ch.inc;
  if (ch.phase > 2 * M_PI) ch.phase -= 2 * M_PI;
  ch.delay[ch.pos] = x * lo;
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

inline double CorrelatorDemod::sum(uint64_t a, uint64_t b) const {
  // cum[i] holds the sum of d[0..i]; entries older than kRing samples are gone.
  double hi = cumD_[(b - 1) % kRing];
  double lo = a == 0 ? 0.0 : cumD_[(a - 1) % kRing];
  return hi - lo;
}

inline double CorrelatorDemod::sumFreq(uint64_t a, uint64_t b) const {
  double hi = cumF_[(b - 1) % kRing];
  double lo = a == 0 ? 0.0 : cumF_[(a - 1) % kRing];
  return hi - lo;
}

void CorrelatorDemod::process(const float* in, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    float x = in[i];
    cf zm = runChannel(mark_, x);
    cf zs = runChannel(space_, x);
    if (++scopeDecim_ >= std::max(1, static_cast<int>(cfg_.sampleRate / 3000.0))) {
      scopeDecim_ = 0;
      float tot = mark_.peak + space_.peak + 1e-9f;
      float xm = std::real(zm * cf(static_cast<float>(std::cos(mark_.phase)), static_cast<float>(std::sin(mark_.phase)))) / tot * 2;
      float ys = std::real(zs * cf(static_cast<float>(std::cos(space_.phase)), static_cast<float>(std::sin(space_.phase)))) / tot * 2;
      scope_.push_back({xm, ys});
      if (scope_.size() > 4096) scope_.erase(scope_.begin(), scope_.begin() + 2048);
    }
    // Soft decision on the same +-1 scale as MatchedFilterDemod, so scores,
    // quality and the squelch threshold are comparable across engines.
    float d;
    if (cfg_.atc) d = mark_.env / mark_.peak - space_.env / space_.peak;
    else d = (mark_.env - space_.env) / (std::max(mark_.peak, space_.peak) + 1e-9f);
    d = std::clamp(d, -1.f, 1.f);
    // Instantaneous frequency error of whichever channel carries the tone.
    double ferr = 0;
    {
      const cf& z = d >= 0 ? zm : zs;
      const cf& zl = d >= 0 ? mark_.last : space_.last;
      if (std::abs(zl) > 0 && std::abs(z) > 0) ferr = std::arg(z * std::conj(zl)) * cfg_.sampleRate / (2.0 * M_PI);
    }
    mark_.last = zm;
    space_.last = zs;
    double prevD = pos_ == 0 ? 0.0 : cumD_[(pos_ - 1) % kRing];
    double prevF = pos_ == 0 ? 0.0 : cumF_[(pos_ - 1) % kRing];
    cumD_[pos_ % kRing] = prevD + d;
    cumF_[pos_ % kRing] = prevF + ferr;
    ++pos_;
    // Search as we go so a large input block cannot outrun the history ring.
    if ((pos_ & 255) == 0) search();
  }
  search();
}

// Correlates the character starting at tau with all 32 templates. Returns the
// best code and its normalised score (mean per-bit correlation, 0..1).
bool CorrelatorDemod::evaluate(uint64_t tau, uint8_t& code, float& score, float& start, float& stop) const {
  // Bit k occupies [tau + k*spb, tau + (k+1)*spb); integrate its central 60 %.
  float m[7];
  for (int k = 0; k < 7; ++k) {
    uint64_t a = tau + static_cast<uint64_t>((k + 0.2) * spb_);
    uint64_t b = tau + static_cast<uint64_t>((k + 0.8) * spb_);
    if (b <= a) return false;
    m[k] = static_cast<float>(sum(a, b) / static_cast<double>(b - a));
  }
  start = m[0];
  stop = m[6];
  const float edge = static_cast<float>(cfg_.corrEdgeThreshold);
  if (!(start < -edge && stop > edge)) return false;
  // 32-way correlation: template bits are -1 (space) / +1 (mark).
  float best = -1e9f;
  uint8_t bestCode = 0;
  for (int c = 0; c < 32; ++c) {
    float s = -m[0] + m[6];
    for (int k = 0; k < 5; ++k) s += ((c >> k) & 1) ? m[k + 1] : -m[k + 1];
    if (s > best) { best = s; bestCode = static_cast<uint8_t>(c); }
  }
  code = bestCode;
  score = best / 7.f;
  return true;
}

void CorrelatorDemod::search() {
  // Need the whole character plus half a bit of look-ahead for the peak search,
  // and never look back further than the ring holds.
  const uint64_t need = static_cast<uint64_t>(7.5 * spb_) + 2;
  const uint64_t maxBack = kRing - need - 4;
  if (pos_ > maxBack && cursor_ + maxBack < pos_) cursor_ = pos_ - maxBack;
  while (cursor_ + need <= pos_) {
    uint8_t code; float score, start, stop;
    if (!evaluate(cursor_, code, score, start, stop) || score < cfg_.corrAcceptScore) { cursor_ += step_; continue; }
    // A candidate: refine over the next bit period and take the strongest correlation.
    uint64_t bestTau = cursor_; float bestScore = score; uint8_t bestCode = code;
    uint64_t limit = cursor_ + static_cast<uint64_t>(spb_);
    if (limit + need > pos_) return;   // wait for more samples before deciding
    for (uint64_t t = cursor_ + step_; t <= limit; t += step_) {
      uint8_t c2; float s2, st2, sp2;
      if (evaluate(t, c2, s2, st2, sp2) && s2 > bestScore) { bestScore = s2; bestTau = t; bestCode = c2; }
    }
    accept(bestTau, bestCode, bestScore);
    cursor_ = bestTau + static_cast<uint64_t>(7.0 * spb_);
  }
}

void CorrelatorDemod::accept(uint64_t tau, uint8_t code, float score) {
  chars_++;
  quality_ = 0.8f * quality_ + 0.2f * std::min(1.f, score);
  // AFC: mean frequency error over the character (all bits carry a tone).
  if (cfg_.afc) {
    uint64_t a = tau, b = tau + static_cast<uint64_t>(7.0 * spb_);
    double err = sumFreq(a, b) / static_cast<double>(b - a);
    if (std::fabs(err) <= cfg_.params.baud) {
      afcOffset_ = std::clamp(afcOffset_ + cfg_.afcGain * err, -cfg_.afcRangeHz, cfg_.afcRangeHz);
      mark_.setFreq(cfg_.params.markFreq + afcOffset_, cfg_.sampleRate);
      space_.setFreq(cfg_.params.spaceFreq() + afcOffset_, cfg_.sampleRate);
    }
  }
  bool squelched = cfg_.squelch > 0 && quality_ < cfg_.squelch;
  uint64_t endSample = tau + static_cast<uint64_t>(7.0 * spb_);
  if (onCode) onCode(CodeEvent{code, true, squelched ? 0.f : std::min(1.f, score), endSample});
  if (squelched) return;
  auto ch = baudot_.decode(code);
  if (ch && onChar) onChar(*ch);
}

}  // namespace gtty
