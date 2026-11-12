// Skimmer: a bank of narrow decoders stepped across the audio passband so
// every RTTY signal in the receiver's bandwidth is decoded at once.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/modem/matched_filter_demod.h"

namespace gtty {

class Skimmer {
 public:
  struct Channel {
    double markHz;
    std::unique_ptr<MatchedFilterDemod> demod;
    std::string pending;     // decoded text not yet collected
    float quality = 0;
  };

  // Channels every stepHz from loHz to hiHz (mark frequency). Each channel's AFC
  // range is half a step so neighboring channels do not chase the same signal.
  void configure(const DemodConfig& base, double loHz, double hiHz, double stepHz);
  void clear() { channels_.clear(); }
  size_t size() const { return channels_.size(); }
  const Channel& channel(size_t i) const { return channels_[i]; }
  void process(const float* in, size_t n);
  // Hands out the text decoded since the last call, per channel with its current mark frequency.
  void collect(const std::function<void(double markHz, float quality, const std::string& text)>& fn);

 private:
  std::vector<Channel> channels_;
};

}  // namespace gtty
