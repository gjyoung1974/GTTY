#include "core/log/rx_highlight.h"

#include <algorithm>
#include <cctype>

namespace gtty {

namespace {
std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool allDigits(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Words that follow a report without being part of the exchange.
bool endsExchange(const std::string& w) {
  static const char* const kWords[] = {"DE", "TU", "K", "KN", "BK", "SK", "R", "RR", "QSL", "QRZ", "AGN", "PSE",
                                       "GL", "GA", "GM", "GE", "73", "TEST", "UR", "TNX", "TKS", "DX", "QTC"};
  return std::any_of(std::begin(kWords), std::end(kWords), [&](const char* k) { return w == k; });
}

const size_t kMaxExchangeWords = 3;   // "599 123 123", "599 05 NY"
const size_t kMaxExchangeLen = 6;
const size_t kMaxEmbeddedWord = 24;   // longer runs are noise, not a call with something stuck to it
}  // namespace

bool findEmbeddedCall(const std::string& wordIn, const RxHighlightContext& ctx, RxSpan& out) {
  std::string w = upper(wordIn);
  if (w.size() > kMaxEmbeddedWord) return false;
  for (const auto& [callIn, mark] : {std::pair{ctx.myCall, RxMark::MyCall}, std::pair{ctx.wantedCall, RxMark::Call}}) {
    std::string call = upper(callIn);
    if (call.size() < 3 || call.size() >= w.size()) continue;
    size_t at = w.find(call);
    if (at == std::string::npos) continue;
    bool known = mark == RxMark::Call && ctx.scp && ctx.scp->contains(call);
    out = {at, call.size(), known ? RxMark::KnownCall : mark};
    return true;
  }
  if (!ctx.scp || !ctx.scp->loaded()) return false;
  // Longest known call first; four characters at least, shorter ones turn up in anything.
  for (size_t len = std::min<size_t>(w.size() - 1, 10); len >= 4 && len < w.size(); --len)
    for (size_t at = 0; at + len <= w.size(); ++at) {
      std::string part = w.substr(at, len);
      if (part.find('/') == std::string::npos && ctx.scp->contains(part)) {
        out = {at, len, RxMark::KnownCall};
        return true;
      }
    }
  return false;
}

std::vector<RxSpan> RxHighlighter::word(const std::string& wordIn, const RxHighlightContext& ctx) {
  std::string w = upper(wordIn);
  if (w.empty()) return {};
  std::string my = upper(ctx.myCall);

  if (w == "599" || w == "5NN") {
    exchangeLeft_ = kMaxExchangeWords;
    return {{0, w.size(), RxMark::Report}};
  }
  // Report and serial run together: 599123.
  if (allDigits(w) && w.size() > 3 && w.size() <= 9 && w.compare(0, 3, "599") == 0) {
    exchangeLeft_ = kMaxExchangeWords - 1;
    return {{0, 3, RxMark::Report}, {3, w.size() - 3, RxMark::Exchange}};
  }
  if (w == "CQ") {
    exchangeLeft_ = 0;
    return {{0, w.size(), RxMark::Cq}};
  }
  if (!my.empty() && w == my) {
    exchangeLeft_ = 0;
    return {{0, w.size(), RxMark::MyCall}};
  }
  // The operator's own call counts even with something stuck to it ("DF7CBK", "DEDF7CB").
  RxSpan embedded{};
  if (my.size() >= 3 && w.find(my) != std::string::npos && findEmbeddedCall(w, ctx, embedded) && embedded.mark == RxMark::MyCall) {
    exchangeLeft_ = 0;
    return {embedded};
  }
  if (looksLikeCallsign(w)) {
    exchangeLeft_ = 0;
    bool known = ctx.scp && ctx.scp->contains(w);
    return {{0, w.size(), known ? RxMark::KnownCall : RxMark::Call}};
  }
  if (exchangeLeft_ > 0) {
    if (endsExchange(w) || w.size() > kMaxExchangeLen) {
      exchangeLeft_ = 0;
    } else {
      --exchangeLeft_;
      return {{0, w.size(), RxMark::Exchange}};
    }
  }
  if (findEmbeddedCall(w, ctx, embedded)) return {embedded};
  return {};
}

}  // namespace gtty
