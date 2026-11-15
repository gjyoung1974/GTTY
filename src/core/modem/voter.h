// Decoder voting: aligns the character streams of the parallel engines by
// the sample time at which each character ended and emits a merged
// best-guess stream (confidence-weighted majority per character).
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "core/baudot.h"
#include "core/modem/rtty_demodulator.h"

namespace gtty {

class Voter {
 public:
  // nSlots: number of engines that may vote. holdSamples: how long after a
  // character's end time to wait for late engines before deciding
  // (max engine latency + a margin). charSamples: one character period.
  void configure(int nSlots, double charSamples, uint64_t holdSamples, BaudotVariant v, bool unshiftOnSpace);
  void setVoting(int slot, bool votes);    // engines that are disabled or excluded do not vote
  void add(int slot, const CodeEvent& e);  // from the engines, in sample-time order per slot
  void advance(uint64_t now);              // closes groups whose hold time has passed
  void reset();

  std::function<void(char)> onChar;
  std::function<void(uint8_t code, float confidence, int agree, int total)> onCode;
  // How sure the engines were of the character just decided, 0..1: the confidence of
  // those that agreed on it, averaged over all that may vote. Unlike the share of the
  // vote above it stays low on noise, where engines disagree or stay silent.
  float lastStrength() const { return strength_; }

 private:
  struct Group {
    uint64_t t0 = 0;                        // earliest end time seen
    std::vector<int8_t> code;               // per slot, -1 = no vote
    std::vector<float> conf;
  };
  void decide(const Group& g);
  // Noise gate (see decide()). Strength is lastStrength(): about 0.6 when three engines
  // agree on a signal, 0.13 when two disagree on noise. Chosen on simulated contest
  // exchanges with noise between them (vote 30 % character errors without the gate,
  // 3.6 % with it; best single engine 4.5 %) and on continuous text down to -3 dB SNR
  // (0.4 % without, 0.2 % with).
  static constexpr float kEnterStrength = 0.4f;   // needed to start a transmission
  static constexpr float kDoubtStrength = 0.2f;   // below this a decision inside one is held back
  static constexpr size_t kMaxDoubtful = 4;       // more held decisions than this end it
  struct Pending { uint8_t code; float strength; float confidence; int agree; int total; };
  void flushPending();
  std::vector<Pending> pending_;
  uint64_t pendingAt_ = 0;
  bool inSignal_ = false;

  int nSlots_ = 0;
  std::vector<bool> votes_;
  double charSamples_ = 1;
  uint64_t hold_ = 0;
  std::deque<Group> groups_;
  BaudotDecoder baudot_;
  float strength_ = 0;
};

}  // namespace gtty
