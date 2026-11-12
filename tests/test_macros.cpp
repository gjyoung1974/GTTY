#include "core/macros.h"
#include "check.h"

using namespace gtty;

int main() {
  MacroContext ctx;
  ctx.myCall = "N0CALL"; ctx.call = "W1AW"; ctx.serial = 7; ctx.rstSent = "599";
  auto acts = expandMacro("<TX><CALL> <RST> <CNTR> <CNTR> <CALL>\n<RX><LOG>", ctx);
  CHECK_EQ(acts.size(), size_t(4));
  CHECK(acts[0].type == MacroAction::Type::Tx);
  CHECK(acts[1].type == MacroAction::Type::Text);
  CHECK_EQ(acts[1].text, std::string("W1AW 599 007 007 W1AW\n"));
  CHECK(acts[2].type == MacroAction::Type::Rx);
  CHECK(acts[3].type == MacroAction::Type::Log);
  auto cw = expandMacro("<TX>73<CWID><RX>", ctx);
  CHECK_EQ(cw.size(), size_t(4));
  CHECK(cw[2].type == MacroAction::Type::CwId);
  // fldigi "^r" in logger text: removed, and flagged as a return to receive.
  auto t = extractTxControls("\nCQ TEST N0CALL N0CALL CQ^r");
  CHECK_EQ(t.text, std::string("\nCQ TEST N0CALL N0CALL CQ"));
  CHECK(t.returnToRx);
  auto t2 = extractTxControls("A^RB^r");
  CHECK_EQ(t2.text, std::string("AB"));
  CHECK(t2.returnToRx);
  auto t3 = extractTxControls("5^2 ^");
  CHECK_EQ(t3.text, std::string("5^2 ^"));
  CHECK(!t3.returnToRx);
  auto d = defaultMacros();
  CHECK_EQ(d.size(), size_t(12));
  TEST_MAIN_END();
}
