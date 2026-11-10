// Modulator -> channel (noise, frequency offset, resampling) -> demodulator.
#include <cmath>
#include <random>

#include "core/modem/correlator_demod.h"
#include "core/receiver.h"
#include "check.h"
#include "rtty_sim.h"

using namespace gtty;

int main() {
  const std::string msg = "CQ CQ DE W1AW W1AW 599 001 TEST K";
  RttyParams p;

  // Clean loopback at 48 kHz.
  {
    auto x = modulate(msg, p, 48000);
    auto got = trim(demodulate(x, 48000, p, true));
    CHECK_EQ(got, msg);
  }
  // Clean loopback at 44.1 kHz (resampler path) and 8 kHz.
  {
    auto x = modulate(msg, p, 44100);
    CHECK_EQ(trim(demodulate(x, 44100, p, true)), msg);
    auto x8 = modulate(msg, p, 8000);
    CHECK_EQ(trim(demodulate(x8, 8000, p, true)), msg);
  }
  // Other speeds / shifts / reverse.
  {
    RttyParams q = p; q.baud = 75; q.shift = 850; q.reverse = true; q.markFreq = 1500;
    auto x = modulate(msg, q, 48000);
    CHECK_EQ(trim(demodulate(x, 48000, q, true)), msg);
  }
  // Frequency offset: signal 40 Hz high; AFC should pull in.
  {
    RttyParams tx = p; tx.markFreq = p.markFreq + 40;
    auto x = modulate("RYRYRYRYRYRY " + msg, tx, 48000);
    DemodStatus st;
    auto got = demodulate(x, 48000, p, true, &st);
    CHECK(std::fabs(st.afcOffsetHz - 40) < 8);
    CHECK(got.find("599 001 TEST K") != std::string::npos);
  }
  // Noise: 10 dB SNR (3 kHz) should be near-perfect, 3 dB mostly readable.
  {
    auto x = modulate(msg, p, 48000);
    addNoise(x, 10.0, 48000, 1);
    auto got = demodulate(x, 48000, p, true);
    CHECK(errors(msg, got) <= 1);
    auto y = modulate(msg, p, 48000);
    addNoise(y, 3.0, 48000, 2);
    auto got3 = demodulate(y, 48000, p, true);
    std::printf("3 dB SNR: '%s' (%d errors)\n", trim(got3).c_str(), errors(msg, got3));
    CHECK(errors(msg, got3) <= 6);
  }
  // Receiver bank end to end with events.
  {
    EventQueue q;
    Receiver rx(nullptr, &q);
    DemodConfig c; c.params = p;
    rx.configure(c);
    auto x = modulate(msg, p, 48000);
    rx.processSamples(x.data(), x.size(), 48000);
    rx.flushEvents();
    std::vector<std::string> text(rx.bank().size() + 1);
    Event e;
    while (q.pop(e))
      if (auto* t = std::get_if<RxTextEvent>(&e)) text[t->slot] += t->text;
    for (size_t i = 0; i < rx.bank().size(); ++i) {
      if (!rx.bank().slot(i).enabled) continue;
      if (trim(text[i]) != msg) std::printf("slot %zu (%s): '%s'\n", i, rx.bank().slot(i).label.c_str(), trim(text[i]).c_str());
      CHECK_EQ(trim(text[i]), msg);
    }
    CHECK_EQ(trim(text[rx.voteSlot()]), msg);   // the merged stream
  }
  // Correlator engine: clean, offset and noisy.
  {
    DemodConfig c; c.params = p; c.sampleRate = Receiver::kInternalRate;
    auto x = modulate(msg, p, 48000);
    CorrelatorDemod d(c);
    CHECK_EQ(trim(demodulateWith(d, x, 48000)), msg);
    RttyParams q75 = p; q75.baud = 75; q75.shift = 850; q75.reverse = true; q75.markFreq = 1500;
    DemodConfig c75 = c; c75.params = q75;
    CorrelatorDemod d75(c75);
    CHECK_EQ(trim(demodulateWith(d75, modulate(msg, q75, 48000), 48000)), msg);
    RttyParams tx = p; tx.markFreq = p.markFreq + 40;
    CorrelatorDemod dOff(c);
    auto got = demodulateWith(dOff, modulate("RYRYRYRYRYRY " + msg, tx, 48000), 48000);
    CHECK(std::fabs(dOff.status().afcOffsetHz - 40) < 8);
    CHECK(got.find("599 001 TEST K") != std::string::npos);
    auto y = modulate(msg, p, 48000);
    addNoise(y, 3.0, 48000, 2);
    CorrelatorDemod dN(c);
    auto got3 = demodulateWith(dN, y, 48000);
    std::printf("correlator 3 dB SNR: '%s' (%d errors)\n", trim(got3).c_str(), errors(msg, got3));
    CHECK(errors(msg, got3) <= 6);
  }
  TEST_MAIN_END();
}
