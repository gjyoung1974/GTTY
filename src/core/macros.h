// fldigi-compatible macro tags: <TX> <RX> <TX/RX> <MYCALL> <CALL> <NAME> <MYNAME>
// <QTH> <MYQTH> <LOC> <MYLOC> <RST> <MYRST> <CNTR> <INCR> <DECR> <XCHG> <LOG>
// <CLRRX> <CLRTX> <TIME> <ZDT> <CR> <LTRS> <FIGS> <CWID>
#pragma once
#include <string>
#include <vector>

namespace gtty {

struct MacroContext {
  std::string myCall, myName, myQth, myGrid;
  std::string call, name, qth, grid;
  std::string rstSent = "599", rstRcvd = "599";
  std::string exchSent, exchRcvd;
  int serial = 1;
};

struct MacroAction {
  enum class Type { Text, Tx, Rx, Log, ClearRx, ClearTx, IncSerial, DecSerial, CwId };
  Type type;
  std::string text;  // for Text
};

std::vector<MacroAction> expandMacro(const std::string& macro, const MacroContext& ctx);

// Default F1..F12 macro set for casual RTTY QSOs / contesting.
struct MacroDef { std::string label, text; };
std::vector<MacroDef> defaultMacros();

}  // namespace gtty
