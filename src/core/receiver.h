// RX pipeline: audio ring -> resample to the internal rate -> decoder bank
// + spectrum analyser -> events. Runs its own DSP thread when attached to
// an AudioEngine; processSamples() lets files / tests drive it directly.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/audio_engine.h"
#include "core/dsp/biquad.h"
#include "core/dsp/resampler.h"
#include "core/dsp/spectrum.h"
#include "core/events.h"
#include "core/modem/decoder_bank.h"
#include "core/modem/skimmer.h"
#include "core/modem/voter.h"

namespace gtty {

class Receiver {
 public:
  static constexpr double kInternalRate = 12000.0;

  Receiver(AudioEngine* engine, EventQueue* events);
  ~Receiver();

  DecoderBank& bank() { return bank_; }
  const DemodConfig& config() const { return cfg_; }
  void configure(const DemodConfig& base);  // applies to all slots
  // The merged best-guess stream is reported as one more text slot after the engines.
  int voteSlot() const { return static_cast<int>(bank_.size()); }
  int primarySlot() const { return voteSlot(); }
  void setSlotEnabled(size_t slot, bool enabled, bool votes);
  // Edit one engine's configuration under the DSP lock (filter length, mode presets, ...).
  void configureSlot(size_t slot, const std::function<void(DemodConfig&)>& edit);
  // Skimmer across [loHz, hiHz] in stepHz steps; off when stepHz <= 0.
  void setSkimmer(bool on, double loHz, double hiHz, double stepHz);
  bool skimmerOn() const { return skimmerOn_; }
  void resetAfc() { std::lock_guard<std::mutex> lk(mu_); bank_.resetAfcAll(); }
  void setMuted(bool m) { muted_.store(m); }   // ignore audio (while transmitting)
  // Manual notch filters (Hz, at the internal rate) applied before the decoders.
  void setNotches(const std::vector<double>& freqsHz, double bwHz = 60.0);
  std::vector<double> notches() const;

  void start();  // DSP thread pulling from engine->rxRing()
  void stop();
  bool running() const { return run_.load(); }

  // Direct injection (file decoding / tests) at `inRate`.
  void processSamples(const float* in, size_t n, double inRate);
  void flushEvents();  // emit pending text/status/scope events now

 private:
  void loop();
  void processInternal(const float* in, size_t n);

  AudioEngine* engine_;
  EventQueue* events_;
  DemodConfig cfg_;
  DecoderBank bank_;
  Voter voter_;
  Skimmer skimmer_;
  bool skimmerOn_ = false;
  double skimLo_ = 300, skimHi_ = 3000, skimStep_ = 50;
  uint64_t samplePos_ = 0;
  void rebuildVoter();
  std::unique_ptr<Resampler> resampler_;
  double resamplerInRate_ = 0;
  SpectrumAnalyzer spectrum_;
  std::vector<float> resampled_, spectrumDb_, filtered_;
  std::vector<double> notchFreqs_;
  std::vector<Biquad> notchFilters_;
  std::vector<std::string> pendingText_;
  std::thread thread_;
  std::mutex mu_;  // serialises configuration changes against the DSP thread
  std::atomic<bool> run_{false};
  std::atomic<bool> muted_{false};
  size_t samplesSinceStatus_ = 0;
};

}  // namespace gtty
