#include "core/baudot.h"
#include "check.h"

using namespace gtty;

int main() {
  BaudotEncoder enc;
  BaudotDecoder dec;
  std::string msg = "CQ CQ DE W1AW W1AW 599 001 TEST\nABC";
  std::string out;
  for (uint8_t c : enc.encode(msg)) {
    auto ch = dec.decode(c);
    if (ch) out.push_back(*ch);
  }
  CHECK_EQ(out, std::string("CQ CQ DE W1AW W1AW 599 001 TEST\r\nABC"));

  // First character must be preceded by an explicit shift.
  BaudotEncoder e2;
  auto codes = e2.encode('A');
  CHECK_EQ(codes.size(), size_t(2));
  CHECK_EQ(codes[0], baudot::LTRS);
  CHECK_EQ(codes[1], uint8_t(0x03));
  // No redundant shift for a second letter.
  CHECK_EQ(e2.encode('B').size(), size_t(1));
  // Figures shift inserted once.
  auto f = e2.encode("12");
  CHECK_EQ(f.size(), size_t(3));
  CHECK_EQ(f[0], baudot::FIGS);

  // Unshift on space.
  BaudotDecoder d2;
  d2.setUnshiftOnSpace(true);
  d2.decode(baudot::FIGS);
  CHECK_EQ(*d2.decode(0x17), '1');
  d2.decode(baudot::SPACE);
  CHECK_EQ(*d2.decode(0x17), 'Q');
  BaudotDecoder d3;
  d3.setUnshiftOnSpace(false);
  d3.decode(baudot::FIGS);
  d3.decode(baudot::SPACE);
  CHECK_EQ(*d3.decode(0x17), '1');

  // US-TTY vs ITA2 figures.
  BaudotDecoder us(BaudotVariant::USTTY), ita(BaudotVariant::ITA2);
  us.decode(baudot::FIGS); ita.decode(baudot::FIGS);
  CHECK_EQ(*us.decode(0x09), '$');
  CHECK_EQ(*ita.decode(0x11), '+');
  CHECK_EQ(*us.decode(0x11), '"');
  TEST_MAIN_END();
}
