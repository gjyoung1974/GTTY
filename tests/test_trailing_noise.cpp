// The noise after a transmission is set apart from its last word.
#include "core/modem/trailing_noise.h"
#include "core/events.h"
#include "check.h"
#include "rtty_sim.h"

#include <variant>

using namespace gtty;

namespace {
const double kChar = 1980;   // samples per character at 45.45 baud, 12 kHz

// Feeds text with one confidence per character ('9' = 0.9 ... '1' = 0.1), one character period apart.
std::string run(TrailingNoise& t, const std::string& text, const std::string& conf, uint64_t* clock = nullptr) {
  std::string out;
  uint64_t local = 0;
  uint64_t& now = clock ? *clock : local;
  for (size_t i = 0; i < text.size(); ++i) {
    now += static_cast<uint64_t>(kChar);
    t.feed(text[i], (conf[i] - '0') / 10.f, now, out);
    t.idle(now, kChar, out);
  }
  return out;
}

// The receiver's text per slot for a transmission followed by seconds of noise.
std::vector<std::string> receive(const std::string& sent, double snrDb, unsigned seed, int* voteSlot) {
  RttyParams p;
  const double fs = 48000;
  auto x = modulate(sent, p, fs);
  x.insert(x.end(), static_cast<size_t>(fs * 6), 0.f);
  addNoise(x, snrDb, fs, seed);
  EventQueue q;
  Receiver rx(nullptr, &q);
  DemodConfig c;
  c.params = p;
  rx.configure(c);
  *voteSlot = rx.voteSlot();
  std::vector<std::string> text(rx.voteSlot() + 1);
  for (size_t pos = 0; pos < x.size(); pos += 4096) {
    rx.processSamples(x.data() + pos, std::min<size_t>(4096, x.size() - pos), fs);
    rx.flushEvents();
    Event e;
    while (q.pop(e))
      if (auto* t = std::get_if<RxTextEvent>(&e)) text[t->slot] += t->text;
  }
  return text;
}
}  // namespace

int main() {
  // A transmission, then noise: one space between them.
  {
    TrailingNoise t;
    CHECK_EQ(run(t, "DE DF7CBTQXV", "999999992312"), std::string("DE DF7CB TQXV"));
  }
  // One doubtful character inside a transmission does not split the word.
  {
    TrailingNoise t;
    CHECK_EQ(run(t, "DE DF7CB DF7CB", "99999299999999"), std::string("DE DF7CB DF7CB"));
  }
  // The transmission ends on a space or a line break: nothing to add.
  {
    TrailingNoise t;
    CHECK_EQ(run(t, "DF7CB K QXV", "99999992222"), std::string("DF7CB K QXV"));
    t.reset();
    CHECK_EQ(run(t, "DF7CB\nQXV", "999999222"), std::string("DF7CB\nQXV"));
  }
  // Noise alone passes through untouched, as do characters of middling confidence.
  {
    TrailingNoise t;
    CHECK_EQ(run(t, "QXVTZ9MW", "23123212"), std::string("QXVTZ9MW"));
    t.reset();
    CHECK_EQ(run(t, "CQ CQ CQ", "99995559"), std::string("CQ CQ CQ"));
  }
  // A few confident characters by chance are not a transmission.
  {
    TrailingNoise t;
    CHECK_EQ(run(t, "QXVTZ", "99922"), std::string("QXVTZ"));
  }
  // A second transmission after the noise is treated like the first.
  {
    TrailingNoise t;
    CHECK_EQ(run(t, "UN3MQX W1AWZQ", "9999223999922"), std::string("UN3M QX W1AW ZQ"));
  }
  // One doubtful character and then silence (the squelch closed): it is printed, set apart, once the wait is over.
  {
    TrailingNoise t;
    uint64_t now = 0;
    std::string out = run(t, "DF7CBT", "999992", &now);
    CHECK_EQ(out, std::string("DF7CB"));
    t.idle(now + static_cast<uint64_t>(2 * kChar), kChar, out);
    CHECK_EQ(out, std::string("DF7CB"));
    t.idle(now + static_cast<uint64_t>(3 * kChar), kChar, out);
    CHECK_EQ(out, std::string("DF7CB T"));
    t.idle(now + static_cast<uint64_t>(9 * kChar), kChar, out);
    CHECK_EQ(out, std::string("DF7CB T"));
  }

  // Through the real receiver: every pane shows the message intact, and what follows
  // its last word is a space or a line break, from a strong signal down to one at the noise level.
  const std::string sent = "CQ WAE DE UN3M UN3M 599 123 123 DF7CB";
  for (double snr : {20.0, 10.0, 5.0, 0.0}) {
    for (unsigned seed = 1; seed <= 4; ++seed) {
      int vote = 0;
      auto text = receive(sent, snr, seed, &vote);
      for (int slot : {0, 1, vote}) {
        size_t at = text[slot].find(sent);
        bool intact = at != std::string::npos;
        // A space, or the line break the noise itself began with. The vote pane gates
        // noise out altogether, so there the message may simply be the end of the text.
        bool ends = intact && at + sent.size() == text[slot].size();
        char next = intact && !ends ? text[slot][at + sent.size()] : 0;
        bool apart = next == ' ' || next == '\n' || next == '\r' || (slot == vote && ends);
        if (!intact || !apart)
          std::fprintf(stderr, "SNR %.0f dB, seed %u, slot %d: %s: \"%s\"\n", snr, seed, slot,
                       intact ? "noise not set apart" : "message changed", text[slot].c_str());
        CHECK(intact);
        CHECK(apart);
      }
    }
  }

  TEST_MAIN_END();
}
