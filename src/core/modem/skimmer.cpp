#include "core/modem/skimmer.h"

#include <algorithm>
#include <cmath>

namespace gtty {

void Skimmer::configure(const DemodConfig& base, double loHz, double hiHz, double stepHz) {
  channels_.clear();
  if (stepHz < 5) stepHz = 5;
  double shift = base.params.shift;
  // Keep both tones inside the passband.
  double lo = base.params.reverse ? loHz + shift : loHz;
  double hi = base.params.reverse ? hiHz : hiHz - shift;
  for (double f = lo; f <= hi + 0.01; f += stepHz) {
    DemodConfig c = base;
    applyDecoderMode(c, DecoderMode::Normal);
    c.params.markFreq = f;
    c.afc = true;
    c.afcRangeHz = stepHz / 2.0;
    c.squelch = std::max(base.squelch, 0.5f);    // keep noise out of the list (quality of a clean tone is ~1)
    Channel ch;
    ch.markHz = f;
    ch.demod = std::make_unique<MatchedFilterDemod>(c, "skim");
    channels_.push_back(std::move(ch));
    Channel& back = channels_.back();
    MatchedFilterDemod* d = back.demod.get();
    size_t idx = channels_.size() - 1;
    d->onChar = [this, idx](char ch) { channels_[idx].pending.push_back(ch); };
  }
}

void Skimmer::process(const float* in, size_t n) {
  for (auto& ch : channels_) ch.demod->process(in, n);
}

void Skimmer::collect(const std::function<void(double, float, const std::string&)>& fn) {
  // A strong signal leaks into the channels on either side (their filters are
  // wider than the channel step), so only the strongest channel within one
  // shift of any other decoding channel reports; the rest are discarded.
  const double shift = channels_.empty() ? 170.0 : channels_[0].demod->config().params.shift;
  std::vector<bool> report(channels_.size(), false);
  for (size_t i = 0; i < channels_.size(); ++i) {
    auto& ch = channels_[i];
    ch.quality = ch.demod->status().quality;
    if (ch.pending.empty()) continue;
    float level = ch.demod->level();
    bool strongest = true;
    for (size_t j = 0; j < channels_.size() && strongest; ++j) {
      if (j == i) continue;
      if (std::fabs(channels_[j].markHz - ch.markHz) > shift + 1) continue;
      float lj = channels_[j].demod->level();
      if (lj > level * 1.05f || (lj > level * 0.95f && j < i)) strongest = false;   // ties go to the lower channel
    }
    report[i] = strongest;
  }
  for (size_t i = 0; i < channels_.size(); ++i) {
    auto& ch = channels_[i];
    if (report[i]) fn(ch.markHz + ch.demod->status().afcOffsetHz, ch.quality, ch.pending);
    ch.pending.clear();
  }
}

}  // namespace gtty
