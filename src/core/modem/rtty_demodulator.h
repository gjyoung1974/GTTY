// Abstract RTTY demodulator interface. Concrete decoders (matched filter,
// maximum-likelihood, ...) run side by side in a DecoderBank.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/rtty_params.h"

namespace gtty {

struct DemodConfig {
  RttyParams params;
  double sampleRate = 12000.0;   // internal processing rate
  bool atc = true;               // automatic threshold control
  bool afc = true;               // automatic frequency control
  double afcRangeHz = 60.0;      // max AFC excursion from the nominal mark frequency
  double filterLengthBits = 1.0; // matched filter length in bit periods
  int matchedWindow = 1;         // 0 boxcar, 1 Hann (raised cosine)
  double atcTauSec = 0.25;       // ATC peak-follower time constant
  double bitWindow = 0.33;       // fraction of the bit period integrated around each bit center
  double afcGain = 0.35;         // fraction of the measured error applied per character
  float squelch = 0.0f;          // 0 = off .. 1 = only perfect signals
  // Correlator engine: how strongly the start (space) and stop (mark) cells must
  // read, and the mean per-bit correlation needed to accept a character.
  // On the +-1 soft-decision scale (a clean tone reads ~1). tools/bench_rtty.py on the
  // W6IWI recordings: 0.03 / 0.30 gave CER 6.4 % (40 m fading) and 4.3 % (20 m QSB);
  // 0.24 / 0.36 gave 11.8 % / 5.7 %.
  double corrEdgeThreshold = 0.03;
  double corrAcceptScore = 0.30;
};

struct ScopePoint { float x, y; };

// One decoded 5-bit code with the information a voter needs: whether framing
// was valid, how confident the decoder is (0..1) and when the character ended
// (sample index on the decoder's input clock), so parallel engines can be aligned.
struct CodeEvent {
  uint8_t code = 0;
  bool valid = true;
  float confidence = 0;
  uint64_t sample = 0;
};

struct DemodStatus {
  double afcOffsetHz = 0;      // current mark frequency minus nominal
  float quality = 0;           // 0..1
  float markLevel = 0, spaceLevel = 0;  // normalized 0..1
  bool squelched = false;
  uint64_t chars = 0, framingErrors = 0;
};

class RttyDemodulator {
 public:
  virtual ~RttyDemodulator() = default;
  virtual const char* name() const = 0;
  virtual void configure(const DemodConfig& c) = 0;
  virtual const DemodConfig& config() const = 0;
  virtual void process(const float* in, size_t n) = 0;
  virtual void reset() = 0;
  virtual void resetAfc() = 0;
  virtual DemodStatus status() const = 0;
  // Moves accumulated XY-scope points (mark vs space bandpass outputs) to `out`.
  virtual void takeScope(std::vector<ScopePoint>& out) = 0;

  // Output latency in input samples: how long after a character ends the
  // decoder reports it. The voter waits this long before closing a vote.
  virtual uint64_t latencySamples() const { return 0; }

  std::function<void(char)> onChar;                 // decoded printable character
  std::function<void(const CodeEvent&)> onCode;     // raw 5-bit code with timing and confidence
};

// Behavior presets for the matched-filter engines (2Tone/MMTTY-style modes).
enum class DecoderMode { Normal = 0, Selective = 1, Flutter = 2, Custom = 3 };
const char* decoderModeName(DecoderMode m);
// Applies the preset's filter length, window and ATC time constant; Custom leaves them.
void applyDecoderMode(DemodConfig& c, DecoderMode m);

}  // namespace gtty
