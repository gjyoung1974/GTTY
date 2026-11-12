#include <cmath>
#include <vector>

#include "core/dsp/biquad.h"
#include "core/net/xmlrpc_server.h"
#include "check.h"

using namespace gtty;
using namespace gtty::xmlrpc;

int main() {
  // Parse a typical xmlrpc-c request.
  std::string req =
      "<?xml version=\"1.0\"?>\n<methodCall><methodName>text.get_rx</methodName>\n"
      "<params><param><value><i4>10</i4></value></param><param><value><int>25</int></value></param>"
      "<param><value>plain &amp; simple</value></param>"
      "<param><value><boolean>1</boolean></value></param>"
      "<param><value><double>14080.5</double></value></param>"
      "<param><value><array><data><value><string>a</string></value><value><i4>2</i4></value></data></array></value></param>"
      "<param><value><struct><member><name>k</name><value><string>v</string></value></member></struct></value></param>"
      "<param><value><base64>SGVsbG8=</base64></value></param>"
      "</params></methodCall>";
  std::string method, err;
  std::vector<Value> params;
  CHECK(parseRequest(req, method, params, err));
  CHECK_EQ(method, std::string("text.get_rx"));
  CHECK_EQ(params.size(), size_t(8));
  CHECK_EQ(params[0].asInt(), 10);
  CHECK_EQ(params[1].asInt(), 25);
  CHECK_EQ(params[2].asString(), std::string("plain & simple"));
  CHECK(params[3].asBool());
  CHECK(params[4].asDouble() == 14080.5);
  CHECK(params[5].type == Value::Type::Array && params[5].arr.size() == 2 && params[5].arr[1].asInt() == 2);
  CHECK(params[6].type == Value::Type::Struct && params[6].members[0].first == "k");
  CHECK_EQ(params[7].asString(), std::string("Hello"));

  // No params.
  CHECK(parseRequest("<methodCall><methodName>main.rx</methodName><params></params></methodCall>", method, params, err));
  CHECK_EQ(method, std::string("main.rx"));
  CHECK(params.empty());
  CHECK(parseRequest("<methodCall><methodName>main.tx</methodName></methodCall>", method, params, err));
  CHECK(!parseRequest("<nonsense/>", method, params, err));

  // Serialize + base64 round trip.
  CHECK_EQ(base64Encode("Hello"), std::string("SGVsbG8="));
  CHECK_EQ(base64Decode("SGVsbG8="), std::string("Hello"));
  CHECK_EQ(base64Decode(base64Encode("ab")), std::string("ab"));
  CHECK_EQ(base64Decode(base64Encode("abcd")), std::string("abcd"));
  std::string resp = serializeResponse(Value::string("RT<TY>"));
  // Strings go out untyped, as fldigi sends them: CQRLOG takes whatever stands between
  // <value> and </value> as the answer.
  CHECK(resp.find("<value>RT&lt;TY&gt;</value>") != std::string::npos);
  CHECK(resp.find("<string>") == std::string::npos);
  // A request written by serializeRequest reads back, and its string is untyped too.
  std::string call = serializeRequest("log.add_record", {Value::string("<CALL:4>W1AW<eor>"), Value::integer(7)});
  CHECK(call.find("<value>&lt;CALL:4&gt;W1AW&lt;eor&gt;</value>") != std::string::npos);
  CHECK(parseRequest(call, method, params, err));
  CHECK_EQ(method, std::string("log.add_record"));
  CHECK_EQ(params.size(), size_t(2));
  CHECK_EQ(params[0].asString(), std::string("<CALL:4>W1AW<eor>"));
  CHECK_EQ(params[1].asInt(), 7);
  std::string fault = serializeFault(1, "bad");
  CHECK(fault.find("<fault>") != std::string::npos && fault.find("faultString") != std::string::npos);

  // Notch filter: attenuates its center, passes a tone 300 Hz away.
  {
    Biquad n = Biquad::notch(2125, 60, 12000);
    double eOn = 0, eOff = 0;
    Biquad n2 = Biquad::notch(2125, 60, 12000);
    for (int i = 0; i < 24000; ++i) {
      float a = n.process(static_cast<float>(std::sin(2 * M_PI * 2125 * i / 12000.0)));
      float b = n2.process(static_cast<float>(std::sin(2 * M_PI * 2425 * i / 12000.0)));
      if (i > 12000) { eOn += a * a; eOff += b * b; }
    }
    CHECK(10 * std::log10(eOn / eOff) < -30);
  }
  TEST_MAIN_END();
}
