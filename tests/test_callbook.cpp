// Callbook XML parsing (QRZ and HamQTH), without any network access.
#include "core/log/callbook.h"
#include "check.h"

using namespace gtty;

int main() {
  std::string key;
  auto login = Callbook::parseQrz(
      "<?xml version=\"1.0\"?><QRZDatabase version=\"1.34\"><Session><Key>abc123</Key><Count>1</Count></Session></QRZDatabase>", &key);
  CHECK_EQ(key, std::string("abc123"));
  CHECK(!login.found);
  CHECK_EQ(login.error, std::string(""));

  auto r = Callbook::parseQrz(
      "<QRZDatabase><Callsign><call>W1AW</call><fname>Hiram</fname><name>Maxim</name><addr2>Newington</addr2>"
      "<state>CT</state><country>United States</country><grid>FN31pr</grid></Callsign><Session><Key>abc123</Key></Session></QRZDatabase>",
      &key);
  CHECK(r.found);
  CHECK_EQ(r.call, std::string("W1AW"));
  CHECK_EQ(r.name, std::string("Hiram Maxim"));
  CHECK_EQ(r.qth, std::string("Newington"));
  CHECK_EQ(r.state, std::string("CT"));
  CHECK_EQ(r.grid, std::string("FN31pr"));
  CHECK_EQ(r.summary(), std::string("Hiram Maxim, Newington CT, United States (FN31pr)"));

  auto nf = Callbook::parseQrz("<QRZDatabase><Session><Error>Not found: XX9XX</Error><Key>abc123</Key></Session></QRZDatabase>", nullptr);
  CHECK(!nf.found);
  CHECK_EQ(nf.error, std::string("QRZ: Not found: XX9XX"));
  auto bad = Callbook::parseQrz("<QRZDatabase><Session><Error>Username/password incorrect</Error></Session></QRZDatabase>", &key);
  CHECK_EQ(bad.error, std::string("QRZ: Username/password incorrect"));

  std::string id;
  auto hl = Callbook::parseHamQth("<HamQTH version=\"2.8\"><session><session_id>deadbeef</session_id></session></HamQTH>", &id);
  CHECK_EQ(id, std::string("deadbeef"));
  CHECK(!hl.found);
  auto h = Callbook::parseHamQth(
      "<HamQTH><search><callsign>ok1rr</callsign><nick>Martin</nick><qth>Praha</qth><country>Czech Republic</country>"
      "<adr_city>Praha</adr_city><grid>JO70</grid><itu>28</itu></search></HamQTH>", nullptr);
  CHECK(h.found);
  CHECK_EQ(h.call, std::string("OK1RR"));
  CHECK_EQ(h.name, std::string("Martin"));
  CHECK_EQ(h.qth, std::string("Praha"));
  CHECK_EQ(h.grid, std::string("JO70"));
  CHECK_EQ(h.summary(), std::string("Martin, Praha, Czech Republic (JO70)"));
  auto he = Callbook::parseHamQth("<HamQTH><session><error>Session does not exist or expired</error></session></HamQTH>", nullptr);
  CHECK(!he.found);
  CHECK_EQ(he.error, std::string("HamQTH: Session does not exist or expired"));

  // Entities and empty elements.
  CHECK_EQ(Callbook::xmlText("<a><name>Smith &amp; Sons</name></a>", "name"), std::string("Smith & Sons"));
  CHECK_EQ(Callbook::xmlText("<a><name/></a>", "name"), std::string(""));
  CHECK_EQ(Callbook::xmlText("<a><names>x</names><name>y</name></a>", "name"), std::string("y"));
  TEST_MAIN_END();
}
