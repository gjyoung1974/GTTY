#include "core/receiver.h"

#include <algorithm>
#include <chrono>

namespace gtty {

Receiver::Receiver(AudioEngine* engine, EventQueue* events)
    : engine_(engine), events_(events), spectrum_(4096, kInternalRate, 0.875) {
  cfg_.sampleRate = kInternalRate;
  addDefaultDecoders(bank_, cfg_);
  pendingText_.resize(bank_.size() + 1);   // + the voted stream
  bank_.onChar = [this](int slot, char c) {
    if (slot < static_cast<int>(pendingText_.size())) pendingText_[slot].push_back(c);
  };
  bank_.onCode = [this](int slot, const CodeEvent& e) { voter_.add(slot, e); };
  voter_.onChar = [this](char c) { pendingText_[voteSlot()].push_back(c); };
  rebuildVoter();
}

void Receiver::rebuildVoter() {
  uint64_t hold = 0;
  for (size_t i = 0; i < bank_.size(); ++i) {
    const auto& s = bank_.slot(i);
    if (s.enabled && s.votes) hold = std::max(hold, s.demod->latencySamples());
  }
  double charSamples = cfg_.params.charPeriod() * kInternalRate;
  hold += static_cast<uint64_t>(0.75 * charSamples);
  voter_.configure(static_cast<int>(bank_.size()), charSamples, hold, cfg_.params.variant, cfg_.params.unshiftOnSpace);
  for (size_t i = 0; i < bank_.size(); ++i) voter_.setVoting(static_cast<int>(i), bank_.slot(i).enabled && bank_.slot(i).votes);
}

void Receiver::setSlotEnabled(size_t slot, bool enabled, bool votes) {
  std::lock_guard<std::mutex> lk(mu_);
  if (slot >= bank_.size()) return;
  bank_.slot(slot).enabled = enabled;
  bank_.slot(slot).votes = votes;
  rebuildVoter();
}

void Receiver::configureSlot(size_t slot, const std::function<void(DemodConfig&)>& edit) {
  std::lock_guard<std::mutex> lk(mu_);
  if (slot >= bank_.size()) return;
  DemodConfig c = bank_.slot(slot).demod->config();
  edit(c);
  bank_.slot(slot).demod->configure(c);
  rebuildVoter();
}

void Receiver::setSkimmer(bool on, double loHz, double hiHz, double stepHz) {
  std::lock_guard<std::mutex> lk(mu_);
  skimmerOn_ = on && stepHz > 0;
  skimLo_ = loHz; skimHi_ = hiHz; skimStep_ = stepHz;
  if (skimmerOn_) skimmer_.configure(cfg_, skimLo_, skimHi_, skimStep_);
  else skimmer_.clear();
}

Receiver::~Receiver() { stop(); }

void Receiver::configure(const DemodConfig& base) {
  std::lock_guard<std::mutex> lk(mu_);
  cfg_ = base;
  cfg_.sampleRate = kInternalRate;
  bank_.configureAll(cfg_);
  rebuildVoter();
  if (skimmerOn_) skimmer_.configure(cfg_, skimLo_, skimHi_, skimStep_);
}

void Receiver::start() {
  if (run_ || !engine_) return;
  run_ = true;
  thread_ = std::thread([this] { loop(); });
}

void Receiver::stop() {
  run_ = false;
  if (thread_.joinable()) thread_.join();
}

void Receiver::loop() {
  std::vector<float> buf(2048);
  while (run_) {
    size_t n = engine_->rxRing().pop(buf.data(), buf.size());
    if (n == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
      continue;
    }
    if (muted_) {
      for (size_t i = 0; i < n; ++i) buf[i] = 0;
    }
    processSamples(buf.data(), n, engine_->inputRate());
  }
}

void Receiver::processSamples(const float* in, size_t n, double inRate) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!resampler_ || resamplerInRate_ != inRate) {
    resampler_ = std::make_unique<Resampler>(inRate, kInternalRate);
    resamplerInRate_ = inRate;
  }
  resampled_.clear();
  resampler_->process(in, n, resampled_);
  processInternal(resampled_.data(), resampled_.size());
}

void Receiver::setNotches(const std::vector<double>& freqsHz, double bwHz) {
  std::lock_guard<std::mutex> lk(mu_);
  notchFreqs_ = freqsHz;
  notchFilters_.clear();
  for (double f : freqsHz)
    if (f > 50 && f < kInternalRate / 2 - 50) notchFilters_.push_back(Biquad::notch(f, bwHz, kInternalRate));
}

std::vector<double> Receiver::notches() const {
  std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
  return notchFreqs_;
}

void Receiver::processInternal(const float* inConst, size_t n) {
  const float* in = inConst;
  if (!notchFilters_.empty()) {
    filtered_.assign(inConst, inConst + n);
    for (auto& f : notchFilters_)
      for (size_t i = 0; i < n; ++i) filtered_[i] = f.process(filtered_[i]);
    in = filtered_.data();
  }
  bank_.process(in, n);
  samplePos_ += n;
  voter_.advance(samplePos_);
  if (skimmerOn_) skimmer_.process(in, n);
  if (spectrum_.process(in, n, spectrumDb_) && events_) {
    events_->push(SpectrumEvent{spectrumDb_, spectrum_.binHz()});
  }
  samplesSinceStatus_ += n;
  if (samplesSinceStatus_ >= static_cast<size_t>(kInternalRate / 25)) {  // 25 Hz
    samplesSinceStatus_ = 0;
    flushEvents();
  }
}

void Receiver::flushEvents() {
  if (!events_) return;
  for (size_t i = 0; i < pendingText_.size(); ++i) {
    if (!pendingText_[i].empty()) {
      events_->push(RxTextEvent{static_cast<int>(i), pendingText_[i]});
      pendingText_[i].clear();
    }
  }
  if (skimmerOn_)
    skimmer_.collect([this](double hz, float q, const std::string& t) { events_->push(SkimmerTextEvent{hz, q, t}); });
  for (size_t i = 0; i < bank_.size(); ++i) {
    if (!bank_.slot(i).enabled) continue;
    std::vector<ScopePoint> pts;
    bank_.slot(i).demod->takeScope(pts);
    if (!pts.empty()) events_->push(ScopeEvent{static_cast<int>(i), std::move(pts)});
    events_->push(DemodStatusEvent{static_cast<int>(i), bank_.slot(i).demod->status()});
  }
  if (engine_) events_->push(LevelEvent{engine_->inputPeak(), engine_->inputOverruns()});
}

}  // namespace gtty
