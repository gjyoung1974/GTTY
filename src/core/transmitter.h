// TX pipeline: text -> Baudot codes -> one of several keying back ends
// (AFSK audio, FSK via UART / DTR / RTS, or the rig's own CAT text keyer),
// with PTT sequencing through Hamlib.
#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "core/audio/audio_engine.h"
#include "core/baudot.h"
#include "core/events.h"
#include "core/modem/cw.h"
#include "core/modem/rtty_modulator.h"
#include "core/rig/fsk_keyer.h"
#include "core/rig/rig_control.h"
#include "core/rtty_params.h"

namespace gtty {

struct TxConfig {
  enum class Method { Afsk, FskUart, FskDtr, FskRts, CatText };
  Method method = Method::Afsk;
  std::string fskDevice;      // serial port for FSK keying
  bool fskInvert = false;
  bool diddle = true;         // send LTRS when idle during TX
  float afskLevel = 0.5f;     // 0..1
  bool pttViaRig = true;      // use Hamlib PTT (else VOX / external)
  std::string txMode;         // rig mode to select on TX start (empty = leave)
  int ltrsPreamble = 2;       // LTRS characters before the text
  int pttLeadMs = 100;        // delay between PTT on and first character
  // TX monitor for the FSK / CAT keying methods: an AFSK copy of what is being
  // keyed is played on the output device at this level (0 = off).
  float monitorLevel = 0.f;
  // CW identifier. AFSK: keyed tone at cwIdToneHz (0 = mark tone). Other
  // methods: the rig's PTT is keyed, which sends the mark carrier in RTTY mode.
  bool cwIdAtEnd = false;     // append the ID to every transmission
  std::string cwIdText;       // usually the station callsign
  double cwIdWpm = 20;
  double cwIdToneHz = 0;
  int tuneTimeoutSec = 10;    // Tune (steady mark) switches off by itself after this
};

class Transmitter : private TxSource {
 public:
  Transmitter(AudioEngine* engine, RigControl* rig, EventQueue* events);
  ~Transmitter();

  bool configure(const TxConfig& cfg, const RttyParams& params, std::string& err);
  const TxConfig& config() const { return cfg_; }
  void setParams(const RttyParams& p);

  void sendText(const std::string& text);  // queues (may be called before startTx)
  void startTx();
  void stopTx();    // graceful: finish the queue, then PTT off
  void abortTx();   // immediate
  void cancelPending();  // drop everything queued but not yet started; follow with stopTx()
  bool transmitting() const { return txOn_.load(); }
  bool stopping() const { return stopping_.load(); }
  size_t pending() const;

  // Tune: key the transmitter with a steady mark (no diddle, no text) until
  // stopTx()/abortTx() or the time-out. Returns false while transmitting text.
  bool startTune();
  bool tuning() const { return tuning_.load(); }
  // Send the CW identifier: appended to the current transmission, or sent as
  // a transmission of its own when idle.
  void sendCwId();
  bool sendingCwId() const { return cwActive_.load(); }
  bool monitorActive() const;

  // Call periodically (~50 ms) from the owner thread: drives graceful stop,
  // echo and CAT text pacing.
  void tick();

 private:
  void generate(float* out, size_t n) override;  // TxSource (audio thread)
  void setPtt(bool on);
  void echoCode(uint8_t code);
  void pumpCatText();
  bool keyedCwId() const;        // CW ID goes out by keying PTT (non-AFSK methods)
  void beginCwId();
  void finishTx();               // PTT off, restore rig mode, idle

  AudioEngine* engine_;
  RigControl* rig_;
  EventQueue* events_;
  TxConfig cfg_;
  RttyParams params_;
  RttyModulator mod_;
  CwTone cw_;
  FskKeyer keyer_;
  BaudotEncoder encoder_;
  BaudotDecoder echoDecoder_;
  std::atomic<bool> txOn_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> tuning_{false};
  std::atomic<bool> cwActive_{false};     // CW ID phase in progress
  std::atomic<bool> cwKeyDone_{true};     // PTT-keyed CW ID thread finished
  bool cwRequested_ = false;              // ID requested for the end of this transmission
  bool sentText_ = false;                 // something other than the preamble went out
  std::thread cwThread_;
  std::chrono::steady_clock::time_point txStart_;
  std::chrono::steady_clock::time_point lastTxActivity_;
  std::chrono::steady_clock::time_point drainedAt_{};  // when the queue was first seen empty while stopping

  mutable std::mutex catMu_;
  std::string catPending_;
  std::chrono::steady_clock::time_point catBusyUntil_;
  std::string savedMode_;
};

}  // namespace gtty
