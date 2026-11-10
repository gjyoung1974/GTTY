// Shared RTTY simulation helpers for the modem tests:
// modulator -> channel (noise, offset, resampling) -> demodulator.
#pragma once
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "core/baudot.h"
#include "core/dsp/resampler.h"
#include "core/modem/matched_filter_demod.h"
#include "core/modem/rtty_modulator.h"
#include "core/receiver.h"

namespace gtty {

inline std::vector<float> modulate(const std::string& text, const RttyParams& p, double fs) {
  RttyModulator mod(fs, p);
  mod.setIdleMode(RttyModulator::Idle::Silence);
  mod.setAmplitude(1.0f);
  BaudotEncoder enc(p.variant);
  for (int i = 0; i < 3; ++i) mod.queueCode(baudot::LTRS);
  for (uint8_t c : enc.encode(text)) mod.queueCode(c);
  mod.queueCode(baudot::LTRS);
  std::vector<float> out(static_cast<size_t>(fs * 0.3), 0.f), buf(512);
  while (mod.pending() > 0 || mod.busy()) {
    mod.generate(buf.data(), buf.size());
    out.insert(out.end(), buf.begin(), buf.end());
  }
  out.insert(out.end(), static_cast<size_t>(fs * 0.3), 0.f);
  return out;
}

// Adds white noise for a given SNR in a 3 kHz bandwidth (contest convention).
inline void addNoise(std::vector<float>& x, double snrDb, double fs, unsigned seed) {
  double sigPow = 0.5;  // unit-amplitude tone
  double noiseDensity = sigPow / std::pow(10.0, snrDb / 10.0) / 3000.0;  // per Hz
  double sigma = std::sqrt(noiseDensity * fs / 2.0);
  std::mt19937 rng(seed);
  std::normal_distribution<float> n(0.f, static_cast<float>(sigma));
  for (auto& v : x) v += n(rng);
}

inline std::string demodulate(const std::vector<float>& x, double fs, const RttyParams& p,
                              bool afc, DemodStatus* statusOut = nullptr) {
  DemodConfig c;
  c.params = p;
  c.afc = afc;
  c.sampleRate = Receiver::kInternalRate;
  MatchedFilterDemod d(c);
  std::string out;
  d.onChar = [&](char ch) { out.push_back(ch); };
  Resampler rs(fs, c.sampleRate);
  std::vector<float> y;
  rs.process(x.data(), x.size(), y);
  d.process(y.data(), y.size());
  if (statusOut) *statusOut = d.status();
  return out;
}

inline std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \r\n"), b = s.find_last_not_of(" \r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

inline int errors(const std::string& want, const std::string& got) {
  // crude: count chars of `want` missing from `got` in order
  size_t j = 0; int err = 0;
  for (char c : want) {
    size_t k = got.find(c, j);
    if (k == std::string::npos || k > j + 3) ++err; else j = k + 1;
  }
  return err;
}


// Runs any demodulator over `x` (sampled at fs) and returns the decoded text.
inline std::string demodulateWith(RttyDemodulator& d, const std::vector<float>& x, double fs) {
  std::string out;
  d.onChar = [&](char ch) { out.push_back(ch); };
  Resampler rs(fs, Receiver::kInternalRate);
  std::vector<float> y;
  rs.process(x.data(), x.size(), y);
  for (size_t pos = 0; pos < y.size(); pos += 2048) d.process(y.data() + pos, std::min<size_t>(2048, y.size() - pos));
  return out;
}

}  // namespace gtty
