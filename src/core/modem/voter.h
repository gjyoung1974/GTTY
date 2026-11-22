// Decoder voting: aligns the character streams of the parallel engines by
// the sample time at which each character ended and emits a merged
// best-guess stream (a confidence-weighted majority per character or, as an
// experiment, the engines' soft decisions added up bit by bit).
#pragma once
#include <array>
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
  // How the engines' opinions of one character are merged.
  //   Majority: the character with the most confidence behind it wins as a whole.
  //   Soft:     each of the five bits is the sign of the engines' summed soft decisions.
  //             An engine that read a bit clearly outweighs two that barely tipped the
  //             other way, and the result can be a character no single engine printed.
  // Majority is the default: tests/bench_channel.cpp (2026-11-21, 2200 characters per
  // point, white noise and three ITU-R F.1487 fading channels, -6 to +12 dB) puts the
  // two within about two points of each other in continuous text, neither consistently
  // ahead, and Soft up to three points behind in contest traffic, where it lets a little
  // more of the noise between transmissions through. The three engines share a front
  // end, so their bit errors come together and adding them up gains little.
  enum class Combine { Majority, Soft };
  // Engines built the same way make the same mistakes: the two matched-filter engines
  // print the same wrong character when a carrier comes up in the middle of one, and now
  // and then the same one on noise. Engines given the same witness number count as one
  // witness, and it takes two witnesses to start a transmission. By default every engine
  // is its own witness. Call after configure().
  void setWitness(int slot, int witness);
  void setCombine(Combine c) { combine_ = c; }
  Combine combine() const { return combine_; }
  void add(int slot, const CodeEvent& e);  // from the engines, in sample-time order per slot
  void advance(uint64_t now);              // closes groups whose hold time has passed
  // Decides every open group now, late engines or not, and prints what was held back: the
  // input has ended (we are about to transmit).
  void finish();
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
    std::vector<std::array<float, 5>> soft;
  };
  void decide(const Group& g);
  // Noise gate (see decide()). Strength is lastStrength(): about 0.6 when three engines
  // agree on a signal, 0.13 when two disagree on noise. Chosen on simulated contest
  // exchanges with noise between them (vote 30 % character errors without the gate,
  // 3.6 % with it; best single engine 4.5 %) and on continuous text down to -3 dB SNR
  // (0.4 % without, 0.2 % with). With the two matched-filter engines as one witness, two
  // witnesses needed to start and the timed end (2026-11-21, tests/bench_channel.cpp,
  // contest traffic): white noise at +6 dB 7.7 % to 0.6 %, mid-latitude moderate fading
  // at +6 dB 11.1 % to 4.3 %; continuous text unchanged.
  static constexpr float kEnterStrength = 0.4f;   // needed to start a transmission
  static constexpr float kDoubtStrength = 0.2f;   // below this a decision inside one is held back
  static constexpr size_t kMaxDoubtful = 4;       // more held decisions than this end it
  static constexpr double kQuietChars = 3.0;      // so does this long without a sound decision
  struct Pending { uint8_t code; float strength; float confidence; int agree; int total; int backers; };
  void emit(const Pending& p);
  void flushPending();
  void endTransmission();
  std::vector<Pending> pending_;
  bool inSignal_ = false;
  uint64_t lastSound_ = 0;   // when the last decision the engines stood behind was made
  char lastOut_ = 0;         // the last character printed
  std::vector<int> witness_;

  Combine combine_ = Combine::Majority;
  int nSlots_ = 0;
  std::vector<bool> votes_;
  double charSamples_ = 1;
  uint64_t hold_ = 0;
  std::deque<Group> groups_;
  BaudotDecoder baudot_;
  float strength_ = 0;
};

}  // namespace gtty
