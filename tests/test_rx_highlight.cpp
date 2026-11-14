#include "check.h"
#include "core/log/rx_highlight.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace gtty;

namespace {
// Marks a line the way the receive pane does: C call, K known call, M my call, Q CQ,
// R report, X exchange, '.' nothing. Spaces stay spaces.
std::string marks(RxHighlighter& hl, const RxHighlightContext& ctx, const std::string& line) {
  static const char kLetter[] = {'C', 'K', 'M', 'Q', 'R', 'X'};
  std::string out;
  std::istringstream in(line);
  std::string w;
  bool first = true;
  while (in >> w) {
    if (!first) out.push_back(' ');
    first = false;
    std::string m(w.size(), '.');
    for (const auto& s : hl.word(w, ctx))
      for (size_t i = 0; i < s.len; ++i) m[s.pos + i] = kLetter[static_cast<int>(s.mark)];
    out += m;
  }
  hl.newLine();
  return out;
}
}  // namespace

int main() {
  RxHighlightContext ctx;
  ctx.myCall = "DF7CB";
  RxHighlighter hl;

  // CQ and callsigns.
  CHECK_EQ(marks(hl, ctx, "CQ WAE DE UN3M UN3M CQ"), std::string("QQ ... .. CCCC CCCC QQ"));
  // The report and the exchange after it; the exchange stops at the next call.
  CHECK_EQ(marks(hl, ctx, "DF7CB 599 123 123 UN3M"), std::string("MMMMM RRR XXX XXX CCCC"));
  CHECK_EQ(marks(hl, ctx, "599 599 001 001"), std::string("RRR RRR XXX XXX"));
  CHECK_EQ(marks(hl, ctx, "UR 5NN 05 NY K"), std::string(".. RRR XX XX ."));
  CHECK_EQ(marks(hl, ctx, "599123 TU"), std::string("RRRXXX .."));
  // Words that close an over are not an exchange, and nothing after them is.
  CHECK_EQ(marks(hl, ctx, "599 TU QRZ"), std::string("RRR .. ..."));
  CHECK_EQ(marks(hl, ctx, "599 BK 14"), std::string("RRR .. .."));
  // At most three words, and not across a line break.
  CHECK_EQ(marks(hl, ctx, "599 1 2 3 4"), std::string("RRR X X X ."));
  CHECK_EQ(marks(hl, ctx, "599"), std::string("RRR"));
  CHECK_EQ(marks(hl, ctx, "123 ABC"), std::string("... ..."));
  // Plain numbers and words on their own are left alone.
  CHECK_EQ(marks(hl, ctx, "TEST 73 GL 59 5990"), std::string(".... .. .. .. RRRX"));

  // The operator's call, alone or with something stuck to it.
  CHECK_EQ(marks(hl, ctx, "df7cb DEDF7CB DF7CBK XDF7CBQ7"), std::string("MMMMM ..MMMMM MMMMM. .MMMMM.."));
  CHECK_EQ(marks(hl, ctx, "DF7CB/P"), std::string("MMMMM.."));
  // Other calls inside a longer word are not guessed at without a reason.
  CHECK_EQ(marks(hl, ctx, "QRZUN3MKKK"), std::string(".........."));
  // The call being worked is a reason.
  ctx.wantedCall = "UN3M";
  CHECK_EQ(marks(hl, ctx, "QRZUN3MKKK UN3M"), std::string("...CCCC... CCCC"));
  ctx.wantedCall.clear();

  // So is the Super Check Partial list.
  const char* path = "test_rx_highlight.scp";
  { std::ofstream f(path); f << "# test\nUN3M\n4Z5FI\nK1ABC\nAB1\n"; }
  ScpDatabase scp;
  std::string err;
  CHECK(scp.load(path, err));
  std::remove(path);
  ctx.scp = &scp;
  CHECK_EQ(marks(hl, ctx, "UN3M W1AW"), std::string("KKKK CCCC"));
  CHECK_EQ(marks(hl, ctx, "QRZUN3MKKK XX4Z5FI"), std::string("...KKKK... ..KKKKK"));
  CHECK_EQ(marks(hl, ctx, "ZZAB1ZZ"), std::string("......."));          // three characters: too short to trust
  CHECK_EQ(marks(hl, ctx, "ABCDEFGHIJKLMNOPQRSTUVWXYZUN3M"), std::string(30, '.'));   // a long run is noise
  ctx.wantedCall = "K1ABC";
  CHECK_EQ(marks(hl, ctx, "DEK1ABCK"), std::string("..KKKKK."));

  RxSpan span{};
  CHECK(findEmbeddedCall("QRZUN3MKKK", ctx, span));
  CHECK_EQ(span.pos, static_cast<size_t>(3));
  CHECK_EQ(span.len, static_cast<size_t>(4));
  CHECK(!findEmbeddedCall("UN3M", ctx, span));   // not embedded: the whole word is the call
  CHECK(!findEmbeddedCall("", ctx, span));

  TEST_MAIN_END();
}
