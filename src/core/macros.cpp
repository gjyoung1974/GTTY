#include "core/macros.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace gtty {

namespace {
std::string serialStr(int n) {
  char buf[16];
  snprintf(buf, sizeof buf, "%03d", n);
  return buf;
}
std::string utcTime(const char* fmt) {
  auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof buf, fmt, &tm);
  return buf;
}
}  // namespace

std::vector<MacroAction> expandMacro(const std::string& macro, const MacroContext& ctx) {
  std::vector<MacroAction> out;
  std::string text;
  auto flush = [&] {
    if (!text.empty()) { out.push_back({MacroAction::Type::Text, text}); text.clear(); }
  };
  size_t i = 0;
  while (i < macro.size()) {
    if (macro[i] != '<') { text.push_back(macro[i++]); continue; }
    size_t end = macro.find('>', i);
    if (end == std::string::npos) { text.push_back(macro[i++]); continue; }
    std::string tag = macro.substr(i + 1, end - i - 1);
    for (auto& c : tag) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    i = end + 1;
    if (tag == "TX") { flush(); out.push_back({MacroAction::Type::Tx, ""}); }
    else if (tag == "RX") { flush(); out.push_back({MacroAction::Type::Rx, ""}); }
    else if (tag == "TX/RX") { flush(); out.push_back({MacroAction::Type::Tx, ""}); out.push_back({MacroAction::Type::Rx, ""}); }
    else if (tag == "LOG") { flush(); out.push_back({MacroAction::Type::Log, ""}); }
    else if (tag == "CLRRX") { flush(); out.push_back({MacroAction::Type::ClearRx, ""}); }
    else if (tag == "CLRTX") { flush(); out.push_back({MacroAction::Type::ClearTx, ""}); }
    else if (tag == "INCR") { flush(); out.push_back({MacroAction::Type::IncSerial, ""}); }
    else if (tag == "DECR") { flush(); out.push_back({MacroAction::Type::DecSerial, ""}); }
    else if (tag == "CWID") { flush(); out.push_back({MacroAction::Type::CwId, ""}); }
    else if (tag == "MYCALL") text += ctx.myCall;
    else if (tag == "CALL") text += ctx.call;
    else if (tag == "NAME") text += ctx.name;
    else if (tag == "MYNAME") text += ctx.myName;
    else if (tag == "QTH") text += ctx.qth;
    else if (tag == "MYQTH") text += ctx.myQth;
    else if (tag == "LOC") text += ctx.grid;
    else if (tag == "MYLOC") text += ctx.myGrid;
    else if (tag == "RST") text += ctx.rstSent;
    else if (tag == "MYRST") text += ctx.rstRcvd;
    else if (tag == "CNTR") text += serialStr(ctx.serial);
    else if (tag == "XCHG") text += ctx.exchSent;
    else if (tag == "TIME") text += utcTime("%H%M");
    else if (tag == "ZDT") text += utcTime("%Y-%m-%d %H:%MZ");
    else if (tag == "CR") text += "\n";
    else text += "<" + tag + ">";  // unknown tag: send literally
  }
  flush();
  return out;
}

TxControlText extractTxControls(const std::string& text) {
  TxControlText out;
  out.text.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '^' && i + 1 < text.size() && (text[i + 1] == 'r' || text[i + 1] == 'R')) {
      out.returnToRx = true;
      ++i;
    } else {
      out.text.push_back(text[i]);
    }
  }
  return out;
}

std::vector<MacroDef> defaultMacros() {
  return {
      {"CQ", "<TX>CQ CQ CQ DE <MYCALL> <MYCALL> <MYCALL> PSE K\n<RX>"},
      {"ANS", "<TX><CALL> <CALL> DE <MYCALL> <MYCALL> K\n<RX>"},
      {"RPT", "<TX><CALL> DE <MYCALL> UR RST <RST> <RST> NAME <MYNAME> QTH <MYQTH> <MYQTH> BTU <CALL> DE <MYCALL> K\n<RX>"},
      {"73", "<TX><CALL> DE <MYCALL> TNX FER QSO 73 SK\n<RX><LOG>"},
      {"CQ TEST", "<TX>CQ TEST <MYCALL> <MYCALL> CQ\n<RX>"},
      {"EXCH", "<TX><CALL> <RST> <CNTR> <CNTR> <CALL>\n<RX>"},
      {"TU QRZ", "<TX>TU <MYCALL> CQ\n<RX><LOG>"},
      {"AGN", "<TX>AGN AGN <CALL> <CALL>\n<RX>"},
      {"MYCALL", "<MYCALL> <MYCALL>"},
      {"CALL", "<CALL> "},
      {"TX", "<TX>"},
      {"RX", "<RX>"},
  };
}

}  // namespace gtty
