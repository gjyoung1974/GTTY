// Keeps the noise after a transmission off its last word.
//
// When a station stops sending, the decoders go on printing whatever the band noise
// happens to spell, and the first of it lands straight after the last good character:
// "DF7CB" becomes "DF7CBTQX". RTTY has no end-of-message mark to go by, but the decoders
// know how sure they are of each character: on a signal the confidence stays high down
// to 0 dB SNR, on noise it stays low. This filter passes characters through and puts a
// space where a run of confident characters turns into doubtful ones.
//
// One doubtful character inside a transmission (a fade, a static crash) must not split a
// word, so the first one is held back until the next character shows which it was: a
// second doubtful one means the transmission has ended, a confident one that it goes on.
#pragma once
#include <cstdint>
#include <string>

namespace gtty {

class TrailingNoise {
 public:
  // c: a decoded character. confidence: 0..1 from the decoder. now: sample time, as for
  // idle(). What is ready to print is appended to out.
  void feed(char c, float confidence, uint64_t now, std::string& out) {
    bool weak = confidence < kWeak;
    if (held_) {
      char h = held_;
      held_ = 0;
      if (weak) {
        separate(out);
        emit(h, out);
        emit(c, out);
        inSignal_ = false;
        run_ = 0;
        return;
      }
      emit(h, out);
    } else if (inSignal_ && weak && !isBreak(c)) {
      held_ = c;
      heldAt_ = now;
      return;
    }
    emit(c, out);
    if (confidence >= kStrong) {
      if (++run_ >= kRun) inSignal_ = true;
    } else if (weak) {
      run_ = 0;
    }
  }

  // Call regularly. A held character that nothing followed (the squelch closed, the
  // station stopped) is printed, set apart like any other trailing noise.
  void idle(uint64_t now, double charSamples, std::string& out) {
    if (!held_ || static_cast<double>(now - heldAt_) < kHoldChars * charSamples) return;
    separate(out);
    emit(held_, out);
    held_ = 0;
    inSignal_ = false;
    run_ = 0;
  }

  void reset() { *this = TrailingNoise{}; }

  // From the engines' confidence on recordings and simulated signals: 0.58 and up on a
  // signal at 0 dB SNR (3 kHz), 0.43 and below on noise.
  static constexpr float kStrong = 0.55f;
  static constexpr float kWeak = 0.45f;

 private:
  static constexpr int kRun = 4;             // confident characters in a row that make a transmission
  static constexpr double kHoldChars = 2.5;  // how long a held character waits for the next one
  static bool isBreak(char c) { return c == ' ' || c == '\n' || c == '\r'; }
  void emit(char c, std::string& out) { out.push_back(c); last_ = c; }
  void separate(std::string& out) { if (!isBreak(last_)) emit(' ', out); }

  bool inSignal_ = false;
  int run_ = 0;
  char held_ = 0;
  uint64_t heldAt_ = 0;
  char last_ = '\n';
};

}  // namespace gtty
