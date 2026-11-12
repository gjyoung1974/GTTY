// DX cluster spot-line parsing and the RTTY filter.
#include "core/net/cluster_client.h"
#include "check.h"

using namespace gtty;

int main() {
  Spot s;
  CHECK(parseSpotLine("DX de W1AW:     14083.5  DL1ABC       RTTY cq                   1234Z FN31", s));
  CHECK_EQ(s.spotter, std::string("W1AW"));
  CHECK_EQ(s.call, std::string("DL1ABC"));
  CHECK_EQ(s.freqHz, 14083500.0);
  CHECK_EQ(s.comment, std::string("RTTY cq"));
  CHECK_EQ(s.timeUtc, std::string("12:34"));
  CHECK(s.receivedAt > 0);
  CHECK(looksLikeRtty(s));

  // No comment, no locator, lower-case prefix, CR/LF noise.
  CHECK(parseSpotLine("dx de k1ttt-#:  7045.0  JA1XYZ                                    0001Z\r\n", s));
  CHECK_EQ(s.spotter, std::string("K1TTT"));
  CHECK_EQ(s.comment, std::string(""));
  CHECK_EQ(s.timeUtc, std::string("00:01"));
  CHECK(looksLikeRtty(s));   // 7045 is in the digital segment and no other mode is named

  // Other modes are filtered out even inside the digital segment.
  CHECK(parseSpotLine("DX de N0CALL:   14074.0  VK3ABC       FT8 -12 dB                 1500Z", s));
  CHECK(!looksLikeRtty(s));
  CHECK(parseSpotLine("DX de N0CALL:   14205.0  VK3ABC       cq dx                      1500Z", s));
  CHECK(!looksLikeRtty(s));   // SSB segment, no RTTY mention
  CHECK(parseSpotLine("DX de N0CALL:   14205.0  VK3ABC       rtty test                  1500Z", s));
  CHECK(looksLikeRtty(s));    // explicit RTTY wins

  // Text from the node, made fit to show: valid UTF-8 stays, Latin-1 is converted,
  // control characters and DEL go.
  CHECK_EQ(toDisplayText("tnx Jos\xC3\xA9 73"), std::string("tnx Jos\xC3\xA9 73"));     // UTF-8 as sent
  CHECK_EQ(toDisplayText("tnx Jos\xE9 73"), std::string("tnx Jos\xC3\xA9 73"));          // Latin-1 e-acute
  CHECK_EQ(toDisplayText("a\x07" "b\x7f" "c\x1b[0m"), std::string("abc[0m"));
  CHECK_EQ(toDisplayText("x\x85y"), std::string("xy"));                                    // a C1 control in Latin-1
  CHECK_EQ(toDisplayText("x\xC2\x85y"), std::string("xy"));                               // the same in UTF-8
  CHECK_EQ(toDisplayText("\xE2\x82"), std::string("\xC3\xA2"));                         // cut-off UTF-8: nothing invalid left
  CHECK(parseSpotLine(toDisplayText("DX de EA1XX:     14083.5  DL1ABC       RTTY Jos\xE9          1234Z"), s));
  CHECK_EQ(s.comment, std::string("RTTY Jos\xC3\xA9"));

  // Not spots.
  CHECK(!parseSpotLine("login: ", s));
  CHECK(!parseSpotLine("WWV de W0MU <18>:   SFI=140, A=5, K=1, No Storms", s));
  CHECK(!parseSpotLine("DX de W1AW: notanumber DL1ABC 1234Z", s));
  CHECK(!parseSpotLine("", s));
  TEST_MAIN_END();
}
