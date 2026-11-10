#include "core/log/grid.h"
#include "core/net/wsjtx_udp.h"
#include "check.h"

using namespace gtty;

int main() {
  // Heartbeat layout: magic, schema, type 0, id.
  auto hb = wsjtx::encodeHeartbeat("gtty", "0.1");
  CHECK(hb.size() > 16);
  CHECK(hb[0] == 0xad && hb[1] == 0xbc && hb[2] == 0xcb && hb[3] == 0xda);
  CHECK(hb[7] == 2);   // schema 2
  CHECK(hb[11] == 0);  // heartbeat
  CHECK(hb[15] == 4 && hb[16] == 'g');

  // A Reply message as GridTracker sends it must decode.
  // Build one by hand using the Decode encoder layout (Reply = Decode without 'new' + modifiers).
  WsjtxDecode d;
  d.timeMsSinceMidnightUtc = 12345;
  d.snr = -7;
  d.deltaTime = 0.25;
  d.deltaFreq = 2125;
  d.mode = "RTTY";
  d.message = "CQ W1AW FN31";
  auto dec = wsjtx::encodeDecode("gtty", d);
  // Rewrite as a Reply: type 4, drop the 'new' byte, append modifiers.
  std::vector<uint8_t> reply(dec.begin(), dec.begin() + 12);
  reply[11] = 4;
  size_t idEnd = 12 + 4 + 4;  // header + len + "gtty"
  reply.insert(reply.end(), dec.begin() + 12, dec.begin() + idEnd);
  reply.insert(reply.end(), dec.begin() + idEnd + 1, dec.end() - 2);  // skip 'new', drop lowconf/offair
  reply.push_back(d.lowConfidence ? 1 : 0);
  reply.push_back(0);  // modifiers
  wsjtx::Incoming in;
  CHECK(wsjtx::decodeIncoming(reply.data(), reply.size(), in));
  CHECK(in.type == wsjtx::Incoming::Type::Reply);
  CHECK_EQ(in.id, std::string("gtty"));
  CHECK_EQ(in.reply.message, std::string("CQ W1AW FN31"));
  CHECK_EQ(in.reply.snr, -7);
  CHECK_EQ(in.reply.deltaFreq, 2125u);
  CHECK(in.reply.deltaTime == 0.25);

  // HaltTx / FreeText
  std::vector<uint8_t> halt = {0xad, 0xbc, 0xcb, 0xda, 0, 0, 0, 2, 0, 0, 0, 8, 0, 0, 0, 2, 'G', 'T', 1};
  CHECK(wsjtx::decodeIncoming(halt.data(), halt.size(), in));
  CHECK(in.type == wsjtx::Incoming::Type::HaltTx && in.autoTxOnly);
  std::vector<uint8_t> ft = {0xad, 0xbc, 0xcb, 0xda, 0, 0, 0, 2, 0, 0, 0, 9, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 2, 'H', 'I', 1};
  CHECK(wsjtx::decodeIncoming(ft.data(), ft.size(), in));
  CHECK(in.type == wsjtx::Incoming::Type::FreeText && in.text == "HI" && in.send);
  // Truncated packet is rejected.
  CHECK(!wsjtx::decodeIncoming(ft.data(), 14, in));

  // QDateTime: 1970-01-02 00:00:00 UTC -> JD 2440589, 0 ms, spec 1
  WsjtxQso q;
  q.timeOffUnix = 86400 + 61;
  q.timeOnUnix = 86400;
  q.dxCall = "W1AW";
  auto ql = wsjtx::encodeQsoLogged("gtty", q);
  size_t o = 12 + 4 + 4;
  CHECK(ql[o + 5] == 0x25 && ql[o + 6] == 0x3d && ql[o + 7] == 0x8d);  // 2440589 = 0x253D8D
  CHECK(ql[o + 8] == 0 && ql[o + 9] == 0 && ql[o + 10] == 0xee && ql[o + 11] == 0x48);  // 61000 ms
  CHECK(ql[o + 12] == 1);

  CHECK(looksLikeGrid("FN31"));
  CHECK(looksLikeGrid("fn31pr"));
  CHECK(!looksLikeGrid("RR73"));
  CHECK(!looksLikeGrid("W1AW"));
  CHECK(!looksLikeGrid("5999"));
  CHECK_EQ(findGrid("CQ DE W1AW W1AW FN31 K"), std::string("FN31"));
  CHECK_EQ(findGrid("599 001 TU"), std::string(""));
  TEST_MAIN_END();
}
