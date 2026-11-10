// Morse code for the CW identifier: text -> timed key elements, and a keyed
// sine tone generator that can run as the audio TX source.
#pragma once
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace gtty {

struct CwElement {
  bool on;
  double seconds;
};

// PARIS timing: dit = 1.2 / wpm seconds. Unknown characters are skipped.
std::vector<CwElement> morseElements(const std::string& text, double wpm);
double cwDuration(const std::vector<CwElement>& el);

class CwTone {
 public:
  void prepare(const std::vector<CwElement>& el, double sampleRate, double toneHz, float amplitude);
  void generate(float* out, size_t n);   // audio thread; zeros once finished
  bool finished() const { return done_.load(); }

 private:
  std::vector<CwElement> el_;
  size_t idx_ = 0;
  double remain_ = 0;       // samples left in the current element
  double fs_ = 48000, phase_ = 0, inc_ = 0;
  float amp_ = 0.5f, env_ = 0;
  std::atomic<bool> done_{true};
};

}  // namespace gtty
