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

// fldigi control codes embedded in text a logger queues over XML-RPC
// (text.add_tx). "^r" (or "^R") asks for a return to receive once the text has
// gone out; it is removed from the text and never transmitted.
struct TxControlText {
  std::string text;          // the text with the control codes removed
  bool returnToRx = false;   // a "^r" was present
};
TxControlText extractTxControls(const std::string& text);

// Default F1..F12 macro set for casual RTTY QSOs / contesting.
struct MacroDef { std::string label, text; };
std::vector<MacroDef> defaultMacros();

}  // namespace gtty
