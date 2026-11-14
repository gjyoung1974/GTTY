// What to highlight in received text, one word at a time: callsigns, the operator's
// own call, CQ, the signal report and the exchange that follows it.
#pragma once
#include "core/log/callsign.h"

#include <string>
#include <vector>

namespace gtty {

enum class RxMark { Call, KnownCall, MyCall, Cq, Report, Exchange };

struct RxSpan {
  size_t pos, len;   // within the word
  RxMark mark;
};

// What the classifier knows about the station and the QSO in progress.
struct RxHighlightContext {
  std::string myCall;                // highlighted wherever it appears, alone or inside a longer word
  std::string wantedCall;            // the call being worked (the Call field), found inside longer words too
  const ScpDatabase* scp = nullptr;  // known calls, found inside longer words too
};

// A callsign inside a longer run of characters ("QRZDL1ABCK", noise glued to a call).
// Only calls there is a reason to expect are looked for: any callsign-shaped stretch
// would light up in random noise. Returns false when there is none.
bool findEmbeddedCall(const std::string& word, const RxHighlightContext& ctx, RxSpan& out);

class RxHighlighter {
 public:
  // A finished word (letters, digits, '/') in the order received. Returns the parts to mark.
  std::vector<RxSpan> word(const std::string& word, const RxHighlightContext& ctx);
  // The exchange does not carry over a line break.
  void newLine() { exchangeLeft_ = 0; }

 private:
  int exchangeLeft_ = 0;   // words after a report still taken for the exchange
};

}  // namespace gtty
