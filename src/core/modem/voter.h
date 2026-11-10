// Decoder voting: aligns the character streams of the parallel engines by
// the sample time at which each character ended and emits a merged
// best-guess stream (confidence-weighted majority per character).
#pragma once
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

 private:
  struct Group {
    uint64_t t0 = 0;                        // earliest end time seen
    std::vector<int8_t> code;               // per slot, -1 = no vote
    std::vector<float> conf;
  };
  void decide(const Group& g);

  int nSlots_ = 0;
  std::vector<bool> votes_;
  double charSamples_ = 1;
  uint64_t hold_ = 0;
  std::deque<Group> groups_;
  BaudotDecoder baudot_;
};

}  // namespace gtty
