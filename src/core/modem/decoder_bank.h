// Runs several RTTY decoders in parallel on the same audio (the modern
// contest practice of comparing e.g. two differently-tuned engines).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/modem/rtty_demodulator.h"

namespace gtty {

class DecoderBank {
 public:
  struct Slot {
    std::string label;
    std::unique_ptr<RttyDemodulator> demod;
    bool enabled = true;     // disabled slots are skipped entirely (no CPU, no output)
    bool votes = true;       // takes part in the merged best-guess stream
  };

  // Returns the slot index.
  int add(const std::string& label, std::unique_ptr<RttyDemodulator> d);
  void clear() { slots_.clear(); }
  size_t size() const { return slots_.size(); }
  Slot& slot(size_t i) { return slots_[i]; }
  const Slot& slot(size_t i) const { return slots_[i]; }

  void process(const float* in, size_t n);
  void configureAll(const DemodConfig& base);  // keeps per-slot filter tweaks
  void resetAfcAll();

  std::function<void(int slot, char c)> onChar;
  std::function<void(int slot, const CodeEvent& e)> onCode;

 private:
  std::vector<Slot> slots_;
};

// Standard bank, in this slot order:
//   0 "A"          matched filter, Normal preset
//   1 "B"          matched filter, Flutter preset
//   2 "Correlator" joint-timing character correlator
//   3 "codec2"     libcodec2 FSK demodulator (only when built with libcodec2; disabled by default)
void addDefaultDecoders(DecoderBank& bank, const DemodConfig& base);
constexpr int kSlotA = 0, kSlotB = 1, kSlotCorrelator = 2, kSlotCodec2 = 3;

}  // namespace gtty
