#include "core/transmitter.h"

#include <algorithm>
#include <cstring>
#include <thread>

namespace gtty {

Transmitter::Transmitter(AudioEngine* engine, RigControl* rig, EventQueue* events)
    : engine_(engine), rig_(rig), events_(events), mod_(48000.0, params_) {
  keyer_.onCodeSent = [this](uint8_t c) { echoCode(c); };
}

Transmitter::~Transmitter() {
  abortTx();
  if (cwThread_.joinable()) cwThread_.join();
}

bool Transmitter::monitorActive() const {
  return cfg_.method != TxConfig::Method::Afsk && cfg_.monitorLevel > 0.f && engine_ && engine_->outputOpen();
}

bool Transmitter::keyedCwId() const { return cfg_.method != TxConfig::Method::Afsk; }

bool Transmitter::configure(const TxConfig& cfg, const RttyParams& params, std::string& err) {
  if (txOn_) abortTx();
  cfg_ = cfg;
  params_ = params;
  mod_.setParams(params_);
  mod_.setAmplitude(cfg_.method == TxConfig::Method::Afsk ? cfg_.afskLevel : cfg_.monitorLevel);
  mod_.setIdleMode(cfg_.diddle ? RttyModulator::Idle::Diddle : RttyModulator::Idle::Mark);
  encoder_.setVariant(params_.variant);
  encoder_.setUnshiftOnSpace(params_.unshiftOnSpace);
  echoDecoder_.setVariant(params_.variant);
  echoDecoder_.setUnshiftOnSpace(params_.unshiftOnSpace);
  keyer_.close();
  using M = TxConfig::Method;
  if (cfg_.method == M::FskUart || cfg_.method == M::FskDtr || cfg_.method == M::FskRts) {
    FskKeyer::Method km = cfg_.method == M::FskUart ? FskKeyer::Method::Uart
                        : cfg_.method == M::FskDtr ? FskKeyer::Method::Dtr : FskKeyer::Method::Rts;
    if (!keyer_.open(cfg_.fskDevice, km, params_, cfg_.fskInvert, err)) return false;
    keyer_.setDiddle(cfg_.diddle);
  }
  return true;
}

void Transmitter::setParams(const RttyParams& p) {
  params_ = p;
  mod_.setParams(p);
  encoder_.setVariant(p.variant);
  encoder_.setUnshiftOnSpace(p.unshiftOnSpace);
  echoDecoder_.setVariant(p.variant);
  echoDecoder_.setUnshiftOnSpace(p.unshiftOnSpace);
  keyer_.setParams(p);
}

size_t Transmitter::pending() const {
  switch (cfg_.method) {
    case TxConfig::Method::Afsk: return mod_.pending() + (mod_.busy() ? 1 : 0);
    case TxConfig::Method::CatText: { std::lock_guard<std::mutex> lk(catMu_); return catPending_.size(); }
    default: return keyer_.pending() + (keyer_.busy() ? 1 : 0);
  }
}

void Transmitter::sendText(const std::string& text) {
  lastTxActivity_ = std::chrono::steady_clock::now();
  if (cfg_.method == TxConfig::Method::CatText) {
    if (monitorActive()) for (uint8_t c : encoder_.encode(text)) mod_.queueCode(c);
    std::lock_guard<std::mutex> lk(catMu_);
    for (char c : text) catPending_.push_back(c == '\n' ? '\r' : static_cast<char>(toupper(static_cast<unsigned char>(c))));
    sentText_ = true;
    return;
  }
  auto codes = encoder_.encode(text);
  bool monitor = monitorActive();
  for (uint8_t c : codes) {
    if (cfg_.method == TxConfig::Method::Afsk || monitor) mod_.queueCode(c);
    if (cfg_.method != TxConfig::Method::Afsk) keyer_.sendCode(c);
  }
  if (!codes.empty()) sentText_ = true;
}

void Transmitter::setPtt(bool on) {
  if (cfg_.pttViaRig && rig_ && rig_->isOpen()) {
    if (!rig_->setPtt(on) && events_) events_->push(MessageEvent{"PTT failed: " + rig_->state().error, true});
  }
  if (events_) events_->push(PttEvent{on});
}

void Transmitter::startTx() {
  if (tuning_) abortTx();
  if (txOn_) { stopping_ = false; return; }
  stopping_ = false;
  sentText_ = false;
  cwRequested_ = false;
  cwActive_ = false;
  encoder_.reset();
  echoDecoder_.reset();
  if (rig_ && rig_->isOpen() && !cfg_.txMode.empty()) {
    savedMode_ = rig_->state().mode;
    if (savedMode_ != cfg_.txMode) rig_->setMode(cfg_.txMode);
  }
  setPtt(true);
  txStart_ = std::chrono::steady_clock::now();
  lastTxActivity_ = txStart_;
  // Preamble LTRS so the far end's decoder locks before the text.
  bool monitor = monitorActive();
  if (cfg_.method != TxConfig::Method::CatText) {
    for (int i = 0; i < cfg_.ltrsPreamble; ++i) {
      if (cfg_.method == TxConfig::Method::Afsk || monitor) mod_.queueCode(baudot::LTRS);
      if (cfg_.method != TxConfig::Method::Afsk) keyer_.sendCode(baudot::LTRS);
    }
  }
  if (cfg_.method == TxConfig::Method::Afsk || monitor) {
    if (engine_) {
      mod_.setSampleRate(engine_->outputRate() > 0 ? engine_->outputRate() : 48000.0);
      engine_->setTxSource(this);
    }
  }
  if (cfg_.method != TxConfig::Method::Afsk && cfg_.method != TxConfig::Method::CatText) keyer_.start();
  txOn_ = true;
}

bool Transmitter::startTune() {
  if (txOn_ && !tuning_) return false;
  if (tuning_) return true;
  stopping_ = false;
  sentText_ = false;
  cwRequested_ = false;
  if (rig_ && rig_->isOpen() && !cfg_.txMode.empty()) {
    savedMode_ = rig_->state().mode;
    if (savedMode_ != cfg_.txMode) rig_->setMode(cfg_.txMode);
  }
  setPtt(true);
  txStart_ = std::chrono::steady_clock::now();
  lastTxActivity_ = txStart_;
  mod_.clearQueue();
  mod_.setIdleMode(RttyModulator::Idle::Mark);
  keyer_.setDiddle(false);
  if (cfg_.method == TxConfig::Method::Afsk || monitorActive()) {
    if (engine_) {
      mod_.setSampleRate(engine_->outputRate() > 0 ? engine_->outputRate() : 48000.0);
      engine_->setTxSource(this);
    }
  }
  if (cfg_.method != TxConfig::Method::Afsk && cfg_.method != TxConfig::Method::CatText) keyer_.start();
  tuning_ = true;
  txOn_ = true;
  return true;
}

void Transmitter::sendCwId() {
  if (cfg_.cwIdText.empty()) return;
  if (tuning_) abortTx();
  if (!txOn_) {
    startTx();
    cwRequested_ = true;
    stopTx();
    return;
  }
  cwRequested_ = true;
}

void Transmitter::beginCwId() {
  double tone = cfg_.cwIdToneHz > 0 ? cfg_.cwIdToneHz : params_.markFreq;
  auto el = morseElements(cfg_.cwIdText, cfg_.cwIdWpm);
  float amp = cfg_.method == TxConfig::Method::Afsk ? cfg_.afskLevel : cfg_.monitorLevel;
  bool audio = cfg_.method == TxConfig::Method::Afsk || monitorActive();
  // Prepare the tone before the audio thread can see cwActive_.
  if (audio) cw_.prepare(el, engine_ && engine_->outputRate() > 0 ? engine_->outputRate() : 48000.0, tone, amp);
  cwActive_ = true;
  if (audio && engine_) engine_->setTxSource(this);
  if (keyedCwId()) {
    keyer_.stop();
    cwKeyDone_ = false;
    if (cwThread_.joinable()) cwThread_.join();
    cwThread_ = std::thread([this, el] {
      bool viaRig = cfg_.pttViaRig && rig_ && rig_->isOpen();
      for (const auto& e : el) {
        if (!cwActive_) break;
        if (viaRig) rig_->setPtt(e.on);
        std::this_thread::sleep_for(std::chrono::duration<double>(e.seconds));
      }
      if (viaRig) rig_->setPtt(false);
      cwKeyDone_ = true;
    });
  }
}

void Transmitter::finishTx() {
  if (engine_) engine_->setTxSource(nullptr);
  mod_.clearQueue();     // anything queued after the stop (e.g. during the CW ID) must not leak into the next TX
  keyer_.clearQueue();
  keyer_.stop();
  keyer_.setDiddle(cfg_.diddle);
  mod_.setIdleMode(cfg_.diddle ? RttyModulator::Idle::Diddle : RttyModulator::Idle::Mark);
  setPtt(false);
  if (rig_ && rig_->isOpen() && !cfg_.txMode.empty() && !savedMode_.empty() && savedMode_ != cfg_.txMode)
    rig_->setMode(savedMode_);
  txOn_ = false;
  stopping_ = false;
  tuning_ = false;
  cwActive_ = false;
  cwRequested_ = false;
}

void Transmitter::stopTx() {
  if (!txOn_) return;
  if (tuning_) { abortTx(); return; }
  if (cwActive_) return;
  stopping_ = true;
  drainedAt_ = {};
  // Trailing LTRS leaves the far end in letters case.
  if (cfg_.method == TxConfig::Method::Afsk || monitorActive()) mod_.queueCode(baudot::LTRS);
  if (cfg_.method != TxConfig::Method::Afsk && cfg_.method != TxConfig::Method::CatText) keyer_.sendCode(baudot::LTRS);
  tick();
}

void Transmitter::abortTx() {
  if (!txOn_) return;
  cwActive_ = false;
  if (cwThread_.joinable()) cwThread_.join();
  mod_.clearQueue();
  keyer_.clearQueue();
  { std::lock_guard<std::mutex> lk(catMu_); catPending_.clear(); }
  if (cfg_.method == TxConfig::Method::CatText && rig_) rig_->stopText();
  finishTx();
}

void Transmitter::tick() {
  if (!txOn_) return;
  auto now = std::chrono::steady_clock::now();
  if (tuning_) {
    if (cfg_.tuneTimeoutSec > 0 && now - txStart_ > std::chrono::seconds(cfg_.tuneTimeoutSec)) abortTx();
    return;
  }
  if (cwActive_) {
    bool audioDone = !(cfg_.method == TxConfig::Method::Afsk || monitorActive()) || cw_.finished();
    bool keyDone = !keyedCwId() || cwKeyDone_;
    if (audioDone && keyDone) {
      if (cwThread_.joinable()) cwThread_.join();
      finishTx();
    }
    return;
  }
  // Echo characters whose transmission has started (AFSK path).
  uint8_t codes[64];
  size_t n = mod_.takeSentCodes(codes, sizeof codes);
  for (size_t i = 0; i < n; ++i) echoCode(codes[i]);

  if (cfg_.method == TxConfig::Method::CatText) pumpCatText();

  if (stopping_) {
    bool done = false;
    switch (cfg_.method) {
      case TxConfig::Method::Afsk:
        done = mod_.pending() == 0 && !mod_.busy();
        if (done) {
          // Let the last stop bit and the envelope ramp reach the sound card.
          if (drainedAt_ == std::chrono::steady_clock::time_point{}) drainedAt_ = now;
          if (now - drainedAt_ < std::chrono::milliseconds(120)) done = false;
        } else {
          drainedAt_ = {};
        }
        break;
      case TxConfig::Method::CatText: {
        std::lock_guard<std::mutex> lk(catMu_);
        done = catPending_.empty() && now >= catBusyUntil_;
        break;
      }
      default:
        done = keyer_.pending() == 0 && !keyer_.busy();
        break;
    }
    if (done && monitorActive() && (mod_.pending() > 0 || mod_.busy())) done = false;
    if (!done) { lastTxActivity_ = now; return; }
    if ((cwRequested_ || (cfg_.cwIdAtEnd && sentText_)) && !cfg_.cwIdText.empty()) {
      stopping_ = false;
      beginCwId();
      return;
    }
    finishTx();
  }
}

void Transmitter::pumpCatText() {
  std::lock_guard<std::mutex> lk(catMu_);
  auto now = std::chrono::steady_clock::now();
  if (catPending_.empty() || now < catBusyUntil_ || !rig_ || !rig_->isOpen()) return;
  // Kenwood / Elecraft KY buffers accept up to 24 characters per command.
  std::string chunk = catPending_.substr(0, 24);
  catPending_.erase(0, chunk.size());
  if (rig_->sendText(chunk)) {
    if (events_) events_->push(TxEchoEvent{chunk});
    auto ms = static_cast<long>(chunk.size() * params_.charPeriod() * 1000.0 * 0.9);
    catBusyUntil_ = now + std::chrono::milliseconds(ms);
  } else if (events_) {
    events_->push(MessageEvent{"CAT text send failed: " + rig_->state().error, true});
  }
}

void Transmitter::echoCode(uint8_t code) {
  auto ch = echoDecoder_.decode(code);
  if (ch && events_) events_->push(TxEchoEvent{std::string(1, *ch)});
}

void Transmitter::generate(float* out, size_t n) {
  // Audio thread. PTT lead-in: silence until the rig has switched.
  auto now = std::chrono::steady_clock::now();
  if (now - txStart_ < std::chrono::milliseconds(cfg_.pttLeadMs)) {
    std::fill(out, out + n, 0.f);
    return;
  }
  if (cwActive_) { cw_.generate(out, n); return; }
  mod_.generate(out, n);
}

}  // namespace gtty
