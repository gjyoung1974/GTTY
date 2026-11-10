// Morse timing and the keyed tone generator.
#include <cmath>
#include <vector>

#include "core/modem/cw.h"
#include "check.h"

using namespace gtty;

int main() {
  const double dit = 1.2 / 20;
  auto e = morseElements("E", 20);
  CHECK_EQ(e.size(), size_t(1));
  CHECK(e[0].on);
  CHECK(std::fabs(e[0].seconds - dit) < 1e-9);

  // "PARIS" is 50 units including the trailing word gap; without it, 43.
  auto p = morseElements("PARIS", 20);
  CHECK(std::fabs(cwDuration(p) - 43 * dit) < 1e-9);
  auto pw = morseElements("PARIS ", 20);
  CHECK(std::fabs(cwDuration(pw) - 50 * dit) < 1e-9);

  // Inter-character gap is 3 units: "EE" = dit + 3 + dit.
  auto ee = morseElements("EE", 20);
  CHECK_EQ(ee.size(), size_t(3));
  CHECK(!ee[1].on);
  CHECK(std::fabs(ee[1].seconds - 3 * dit) < 1e-9);
  // Word gap is 7 units and swallows the character gap.
  auto ewe = morseElements("E E", 20);
  CHECK_EQ(ewe.size(), size_t(3));
  CHECK(std::fabs(ewe[1].seconds - 7 * dit) < 1e-9);
  // Unknown characters are skipped; slash is known.
  CHECK_EQ(morseElements("#", 20).size(), size_t(0));
  CHECK_EQ(morseElements("/", 20).size(), size_t(9));

  // Tone generator: energy while keyed, silence after, finished flag.
  CwTone t;
  t.prepare(morseElements("T", 20), 8000, 1000, 0.5f);
  CHECK(!t.finished());
  std::vector<float> buf(8000);
  t.generate(buf.data(), buf.size());   // 1 s; the dah lasts 180 ms
  double on = 0, off = 0;
  for (size_t i = 100; i < 1300; ++i) on += buf[i] * buf[i];
  for (size_t i = 4000; i < 8000; ++i) off += buf[i] * buf[i];
  CHECK(on > 100);
  CHECK(off < 1e-6);
  CHECK(t.finished());
  TEST_MAIN_END();
}
