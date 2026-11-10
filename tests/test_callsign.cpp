#include "core/log/callsign.h"
#include "check.h"

using namespace gtty;

int main() {
  CHECK(looksLikeCallsign("W1AW"));
  CHECK(looksLikeCallsign("n0call"));
  CHECK(looksLikeCallsign("DL1ABC"));
  CHECK(looksLikeCallsign("9A1A"));
  CHECK(looksLikeCallsign("VE3NEA"));
  CHECK(looksLikeCallsign("G3YYD/P"));
  CHECK(looksLikeCallsign("EA8/DL1ABC"));
  CHECK(looksLikeCallsign("KH6/W1AW/M"));
  CHECK(!looksLikeCallsign("599"));
  CHECK(!looksLikeCallsign("CQ"));
  CHECK(!looksLikeCallsign("TEST"));
  CHECK(!looksLikeCallsign("001"));
  CHECK(!looksLikeCallsign("RYRYRY"));
  CHECK(!looksLikeCallsign("A1"));
  auto spans = findCallsigns("CQ TEST DE W1AW W1AW 599 001 K");
  CHECK_EQ(spans.size(), size_t(2));
  CHECK_EQ(spans[0].first, size_t(11));
  CHECK_EQ(spans[0].second, size_t(4));
  TEST_MAIN_END();
}
