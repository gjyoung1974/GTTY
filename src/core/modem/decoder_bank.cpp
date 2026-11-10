#include "core/modem/decoder_bank.h"

#include "core/modem/codec2_demod.h"
#include "core/modem/correlator_demod.h"
#include "core/modem/matched_filter_demod.h"

namespace gtty {

const char* decoderModeName(DecoderMode m) {
  switch (m) {
    case DecoderMode::Normal: return "Normal";
    case DecoderMode::Selective: return "Selective";
    case DecoderMode::Flutter: return "Flutter";
    default: return "Custom";
  }
}

void applyDecoderMode(DemodConfig& c, DecoderMode m) {
  switch (m) {
    // Values measured with tools/bench_rtty.py on the W6IWI off-air recordings
    // (2026-11-08, character error rate 40 m weak+fading / 20 m QSB):
    //   Normal    0.6-bit Hann, 0.3 s ATC             9.0 % / 4.3 %
    //   Selective 1.0-bit Hann, 0.25 s ATC           12.5 % / 3.4 %  (narrower: best on the stronger signal, crowded bands)
    //   Flutter   0.5-bit Hann, 0.3 s ATC, 25 % cell 12.3 % / 5.6 %  (wider, faster; for flutter paths)
    // The former defaults, 0.7-bit Hann with a 1 s ATC, gave 10.3 % / 4.1 %; a
    // 0.5-bit boxcar with an 80 ms ATC gave 23.9 % / 5.2 %.
    case DecoderMode::Normal:
      c.filterLengthBits = 0.6; c.matchedWindow = 1; c.atcTauSec = 0.3; c.bitWindow = 0.33;
      break;
    case DecoderMode::Selective:
      c.filterLengthBits = 1.0; c.matchedWindow = 1; c.atcTauSec = 0.25; c.bitWindow = 0.33;
      break;
    case DecoderMode::Flutter:
      c.filterLengthBits = 0.5; c.matchedWindow = 1; c.atcTauSec = 0.3; c.bitWindow = 0.25;
      break;
    case DecoderMode::Custom:
      break;
  }
}

int DecoderBank::add(const std::string& label, std::unique_ptr<RttyDemodulator> d) {
  int idx = static_cast<int>(slots_.size());
  d->onChar = [this, idx](char c) { if (onChar) onChar(idx, c); };
  d->onCode = [this, idx](const CodeEvent& e) { if (onCode) onCode(idx, e); };
  slots_.push_back({label, std::move(d)});
  return idx;
}

void DecoderBank::process(const float* in, size_t n) {
  for (auto& s : slots_)
    if (s.enabled) s.demod->process(in, n);
}

void DecoderBank::configureAll(const DemodConfig& base) {
  for (auto& s : slots_) {
    DemodConfig c = s.demod->config();
    c.params = base.params;
    c.sampleRate = base.sampleRate;
    c.atc = base.atc;
    c.afc = base.afc;
    c.afcRangeHz = base.afcRangeHz;
    c.squelch = base.squelch;
    c.afcGain = base.afcGain;
    c.corrEdgeThreshold = base.corrEdgeThreshold;
    c.corrAcceptScore = base.corrAcceptScore;
    s.demod->configure(c);
  }
}

void DecoderBank::resetAfcAll() {
  for (auto& s : slots_) s.demod->resetAfc();
}

void addDefaultDecoders(DecoderBank& bank, const DemodConfig& base) {
  DemodConfig a = base;
  applyDecoderMode(a, DecoderMode::Normal);
  bank.add("A", std::make_unique<MatchedFilterDemod>(a, "A"));

  DemodConfig b = base;
  applyDecoderMode(b, DecoderMode::Flutter);
  bank.add("B", std::make_unique<MatchedFilterDemod>(b, "B"));

  bank.add("Correlator", std::make_unique<CorrelatorDemod>(base));

  if (codec2Available()) {
    int i = bank.add("codec2", std::make_unique<Codec2Demod>(base));
    bank.slot(i).enabled = false;   // comparison engine: opt in from Preferences
    bank.slot(i).votes = false;     // half a second behind the others
  }
}

}  // namespace gtty
