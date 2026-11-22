// Decoder voting: alignment by character end time and confidence-weighted majority.
#include <memory>
#include <string>
#include <vector>

#include "core/modem/voter.h"
#include "check.h"

using namespace gtty;

int main() {
  Voter v;
  const double charSamples = 2000;
  v.configure(3, charSamples, 1500, BaudotVariant::ITA2, true);
  std::vector<uint8_t> codes;
  std::vector<int> agrees, totals;
  std::string text;
  v.onCode = [&](uint8_t c, float, int agree, int total) { codes.push_back(c); agrees.push_back(agree); totals.push_back(total); };
  v.onChar = [&](char c) { text.push_back(c); };

  // All three agree on 'A' (LTRS first so the decoder is in letters).
  v.add(0, {0x1f, true, 0.9f, 10000});
  v.add(1, {0x1f, true, 0.9f, 10150});
  v.add(2, {0x1f, true, 0.9f, 10300});
  v.add(0, {0x03, true, 0.9f, 12000});    // 'A'
  v.add(1, {0x03, true, 0.8f, 12100});
  v.add(2, {0x03, true, 0.7f, 12400});
  v.advance(12000);                        // the LTRS group has closed, the 'A' group has not
  CHECK_EQ(codes.size(), size_t(1));
  CHECK_EQ(codes[0], uint8_t(0x1f));
  v.advance(14000);
  CHECK_EQ(codes.size(), size_t(2));
  CHECK_EQ(codes[1], uint8_t(0x03));
  CHECK_EQ(agrees[1], 3); CHECK_EQ(totals[1], 3);
  CHECK_EQ(text, std::string("A"));

  // Two engines say 'B' (0x19), one confident engine says 'C' (0x0e): majority wins.
  v.add(0, {0x19, true, 0.5f, 14000});
  v.add(1, {0x19, true, 0.5f, 14050});
  v.add(2, {0x0e, true, 0.9f, 14200});
  v.advance(16000);
  CHECK_EQ(codes.back(), uint8_t(0x19));
  CHECK_EQ(agrees.back(), 2);
  CHECK_EQ(text, std::string("AB"));

  // One engine misses a character entirely; the other two still decide it.
  v.add(0, {0x0e, true, 0.6f, 16000});
  v.add(2, {0x0e, true, 0.6f, 16300});
  v.advance(18000);
  CHECK_EQ(text, std::string("ABC"));
  CHECK_EQ(totals.back(), 2);

  // A lone weak (squelched) opinion is dropped; a lone confident one is kept.
  v.add(1, {0x09, true, 0.0f, 18000});     // 'D' with zero confidence
  v.advance(20000);
  CHECK_EQ(text, std::string("ABC"));
  v.add(1, {0x09, true, 0.8f, 20000});
  v.advance(22000);
  CHECK_EQ(text, std::string("ABCD"));

  // Two characters from the same engine close together open separate groups.
  v.add(0, {0x01, true, 0.9f, 24000});     // 'E'
  v.add(0, {0x01, true, 0.9f, 24900});     // next 'E' (within half a char of the first: still separate)
  v.add(1, {0x01, true, 0.9f, 24100});
  v.advance(27000);
  CHECK_EQ(text, std::string("ABCDEE"));

  // Engines excluded from voting are ignored.
  v.setVoting(2, false);
  v.add(2, {0x1b, true, 0.9f, 30000});     // FIGS from the excluded engine
  v.advance(32000);
  CHECK_EQ(text, std::string("ABCDEE"));

  // Soft combining: two engines barely read the first bit as space ('A' without it is
  // 0x02, a line feed), one reads it clearly as mark. The majority follows the two; the
  // summed soft bits follow the one that was sure.
  auto event = [](uint8_t code, float bit0, uint64_t at) {
    CodeEvent e{code, true, 0.8f, at};
    for (int k = 0; k < 5; ++k) e.soft[k] = ((code >> k) & 1) ? 0.9f : -0.9f;
    e.soft[0] = bit0;
    e.hasSoft = true;
    return e;
  };
  for (Voter::Combine mode : {Voter::Combine::Majority, Voter::Combine::Soft}) {
    Voter s;
    s.configure(3, charSamples, 1500, BaudotVariant::ITA2, true);
    s.setCombine(mode);
    std::vector<uint8_t> got;
    s.onCode = [&](uint8_t c, float, int, int) { got.push_back(c); };
    for (int e = 0; e < 3; ++e) s.add(e, event(0x1f, 0.9f, 8000 + 100 * e));   // LTRS from all: a transmission
    s.add(0, event(0x02, -0.05f, 10000));
    s.add(1, event(0x02, -0.05f, 10100));
    s.add(2, event(0x03, 0.9f, 10200));
    s.advance(14000);
    CHECK_EQ(got.size(), size_t(2));
    if (got.size() == 2) CHECK_EQ(got[1], uint8_t(mode == Voter::Combine::Soft ? 0x03 : 0x02));
  }
  // An engine without soft bits (codec2) backs every bit of its character with its confidence.
  {
    Voter s;
    s.configure(2, charSamples, 1500, BaudotVariant::ITA2, true);
    s.setCombine(Voter::Combine::Soft);
    std::vector<uint8_t> got;
    s.onCode = [&](uint8_t c, float, int, int) { got.push_back(c); };
    s.add(0, {0x1f, true, 0.9f, 8000});
    s.add(1, event(0x1f, 0.9f, 8100));
    s.add(0, {0x03, true, 0.9f, 10000});
    s.add(1, event(0x02, -0.3f, 10100));
    s.advance(14000);
    CHECK_EQ(got.size(), size_t(2));
    if (got.size() == 2) CHECK_EQ(got[1], uint8_t(0x03));
  }

  // The gate, with engines 0 and 1 as one witness (two matched filters) and 2 as another
  // (the correlator), as the receiver sets them up.
  auto gated = [&](std::string& out) {
    auto g = std::make_unique<Voter>();
    g->configure(3, charSamples, 1500, BaudotVariant::ITA2, true);
    g->setWitness(1, 0);
    g->onChar = [&out](char ch) { out.push_back(ch); };
    return g;
  };
  auto all = [&](Voter& g, uint8_t code, uint64_t at, float conf = 0.85f) {
    for (int e = 0; e < 3; ++e) g.add(e, {code, true, conf, at + 100 * e});
  };
  // A carrier coming up in the middle of a character: the two matched filters print the
  // same wrong character, sure of it, and the correlator another. One witness does not
  // start a transmission, so it is not printed; what all three then read is.
  {
    std::string out;
    auto g = gated(out);
    g->add(0, {0x1c, true, 0.64f, 10000});   // 'M'
    g->add(1, {0x1c, true, 0.63f, 10100});
    g->add(2, {0x15, true, 0.41f, 10200});
    all(*g, 0x1f, 12000);
    all(*g, 0x03, 14000);                    // 'A'
    g->advance(17000);
    CHECK_EQ(out, std::string("A"));
  }
  // After a transmission: the noise the matched filters print is held back as doubtful,
  // and a chance agreement between the two of them does not let it out. When nothing
  // sound has come for a while the transmission is over: the noise is dropped and one
  // space sets the next transmission apart.
  {
    std::string out;
    auto g = gated(out);
    all(*g, 0x1f, 10000);
    all(*g, 0x03, 12000);                    // 'A'
    g->add(0, {0x19, true, 0.2f, 14000});    // 'B' and 'C': noise, the engines apart
    g->add(1, {0x0e, true, 0.2f, 14100});
    g->add(0, {0x09, true, 0.37f, 16000});   // 'D': noise the two happen to agree on
    g->add(1, {0x09, true, 0.36f, 16100});
    g->advance(19000);
    CHECK_EQ(out, std::string("A"));         // still waiting to see what follows
    g->advance(30000);
    CHECK_EQ(out, std::string("A "));
    g->advance(60000);
    CHECK_EQ(out, std::string("A "));        // one space, however long the gap
    all(*g, 0x1f, 62000);
    all(*g, 0x01, 64000);                    // 'E'
    g->advance(67000);
    CHECK_EQ(out, std::string("A E"));
  }
  // A transmission that ends in a fade: its last characters are weak, but the witnesses
  // agree on them, so they are printed when it is over. A weak one they differ on is not.
  {
    std::string out;
    auto g = gated(out);
    all(*g, 0x1f, 10000);
    all(*g, 0x03, 12000);                    // 'A'
    all(*g, 0x19, 14000, 0.15f);             // 'B', faded, all agree
    g->add(0, {0x0e, true, 0.15f, 16000});   // 'C' against 'D', faded
    g->add(2, {0x09, true, 0.15f, 16100});
    g->advance(40000);
    CHECK_EQ(out, std::string("AB "));
  }
  // A doubtful character inside a transmission is printed once a sound one follows.
  {
    std::string out;
    auto g = gated(out);
    all(*g, 0x1f, 10000);
    all(*g, 0x03, 12000);
    g->add(0, {0x19, true, 0.15f, 14000});   // 'B', weak, and one engine missed it
    g->add(2, {0x19, true, 0.15f, 14100});
    all(*g, 0x0e, 16000);                    // 'C'
    g->advance(19000);
    CHECK_EQ(out, std::string("ABC"));
  }
  // Without the second witness (the correlator switched off) the two matched filters
  // count as one each, as before, and can start a transmission between them.
  {
    std::string out;
    auto g = gated(out);
    g->setVoting(2, false);
    g->add(0, {0x1f, true, 0.85f, 10000});
    g->add(1, {0x1f, true, 0.85f, 10100});
    g->add(0, {0x03, true, 0.85f, 12000});
    g->add(1, {0x03, true, 0.85f, 12100});
    g->advance(15000);
    CHECK_EQ(out, std::string("A"));
  }
  TEST_MAIN_END();
}
