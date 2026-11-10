// Decoder voting: alignment by character end time and confidence-weighted majority.
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
  TEST_MAIN_END();
}
