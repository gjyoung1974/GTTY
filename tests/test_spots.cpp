// The skimmer's telnet spot feed: the "DX de" line layout, which decoded text
// is worth a spot, and the server itself, read back through gtty's own
// cluster client over a loopback connection.
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/net/cluster_client.h"
#include "core/net/spot_server.h"
#include "check.h"

using namespace gtty;

namespace {
template <typename F>
bool waitFor(F cond, int ms = 5000) {
  for (int i = 0; i < ms / 10; ++i) {
    if (cond()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return cond();
}
}  // namespace

int main() {
  // ------------------------------------------------------------- line layout
  {
    std::string l = formatSpotLine("WA8Q-#", 14083500, "DL1ABC", "RTTY  45 BPS  CQ", "1234");
    CHECK_EQ(l, std::string("DX de WA8Q-#:    14083.5  DL1ABC       RTTY  45 BPS  CQ               1234Z"));
    CHECK_EQ(l.size(), size_t(75));
    // The columns TLF's bm_add() and cluster ageing read: frequency from 16, call from 26, time at 70.
    CHECK(std::atof(l.c_str() + 16) == 14083.5);
    CHECK_EQ(l.substr(26, 6), std::string("DL1ABC"));
    CHECK_EQ(l.substr(70, 5), std::string("1234Z"));
    // Not1MM splits on blanks: "DX", "de", spotter, frequency, call.
    CHECK_EQ(l.substr(6, 7), std::string("WA8Q-#:"));

    // A 40 m frequency is one digit shorter and still ends in column 24; a long comment is cut.
    l = formatSpotLine("N0CALL-#", 7043200, "JA1XYZ/P", std::string(50, 'x'), "0001");
    CHECK_EQ(l.size(), size_t(75));
    CHECK(std::atof(l.c_str() + 16) == 7043.2);
    CHECK_EQ(l.substr(18, 6), std::string("7043.2"));
    CHECK_EQ(l.substr(26, 8), std::string("JA1XYZ/P"));
    CHECK_EQ(l.substr(70, 5), std::string("0001Z"));

    Spot s;
    CHECK(parseSpotLine(l, s));
    CHECK_EQ(s.spotter, std::string("N0CALL"));
    CHECK_EQ(s.call, std::string("JA1XYZ/P"));
    CHECK_EQ(s.freqHz, 7043200.0);
    CHECK_EQ(s.timeUtc, std::string("00:01"));
  }

  // -------------------------------------------------------- what gets spotted
  {
    bool cq = false;
    CHECK_EQ(spottableCall("CQ CQ TEST DE K1ABC K1ABC K", "WA8Q", &cq), std::string("K1ABC"));
    CHECK(cq);
    CHECK_EQ(spottableCall("cq test k1abc k1abc cq", "WA8Q", &cq), std::string("K1ABC"));   // no DE, seen twice
    CHECK(cq);
    CHECK_EQ(spottableCall("W1AW DE K1ABC 599 001 001", "WA8Q", &cq), std::string("K1ABC"));
    CHECK(!cq);
    CHECK_EQ(spottableCall("K1ABC 599 001", "WA8Q"), std::string(""));              // once, unannounced: could be noise
    CHECK_EQ(spottableCall("QX5RT VVMZ 4KQ", "WA8Q"), std::string(""));
    CHECK_EQ(spottableCall("WA8Q DE WA8Q WA8Q", "WA8Q"), std::string(""));           // never ourselves
    CHECK_EQ(spottableCall("WA8Q WA8Q DE DL1ABC DL1ABC K", "wa8q"), std::string("DL1ABC"));
    // The station heard last wins.
    CHECK_EQ(spottableCall("CQ DE K1ABC K1ABC\nCQ DE JA1XYZ JA1XYZ\n", "WA8Q"), std::string("JA1XYZ"));
    // Text arrives a few characters at a time: a call is not spotted until the word is over.
    CHECK_EQ(spottableCall("CQ TEST DE K1A", "WA8Q"), std::string(""));
    CHECK_EQ(spottableCall("CQ TEST DE K1ABC", "WA8Q"), std::string(""));
    CHECK_EQ(spottableCall("CQ TEST DE K1ABC ", "WA8Q"), std::string("K1ABC"));
    CHECK_EQ(spottableCall("CQ TEST DE K1ABC K1A", "WA8Q"), std::string("K1ABC"));
    CHECK_EQ(spottableCall("", "WA8Q", &cq), std::string(""));
    CHECK(!cq);
  }

  // ------------------------------------------------- server, through a client
  SpotServer server;
  uint16_t port = 0;
  std::string err;
  for (uint16_t p = 18730; p < 18750; ++p) {
    if (server.start("127.0.0.1", p, "wa8q", err)) { port = p; break; }
  }
  CHECK(port != 0);
  if (port == 0) { std::fprintf(stderr, "could not bind a loopback port: %s\n", err.c_str()); TEST_MAIN_END(); }
  CHECK(server.running());

  server.spot("K1OLD", 14081000, "RTTY  45 BPS");   // before anyone is connected: kept for the replay

  std::mutex mu;
  std::vector<Spot> spots;
  std::vector<std::string> lines;
  ClusterClient client;
  client.onSpot = [&](const Spot& s) { std::lock_guard<std::mutex> lk(mu); spots.push_back(s); };
  client.onLine = [&](const std::string& l) { std::lock_guard<std::mutex> lk(mu); lines.push_back(l); };
  ClusterConfig cfg;
  cfg.enabled = true;
  cfg.host = "127.0.0.1";
  cfg.port = port;
  cfg.login = "N0CALL";
  cfg.commands = "set/filter doc/pass rtty";   // a real node's commands must not upset it
  client.start(cfg);

  auto count = [&] { std::lock_guard<std::mutex> lk(mu); return spots.size(); };
  // The client answers the "login:" prompt; the server greets it and replays what it has.
  CHECK(waitFor([&] { return count() >= 1; }));
  CHECK_EQ(server.clients(), size_t(1));
  {
    std::lock_guard<std::mutex> lk(mu);
    bool hello = false;
    for (const auto& l : lines) hello = hello || l.find("Hello N0CALL") != std::string::npos;
    CHECK(hello);
    if (!spots.empty()) CHECK_EQ(spots[0].call, std::string("K1OLD"));
  }

  server.spot("dl1abc", 14083500, "RTTY  45 BPS  CQ");
  server.spot("JA1XYZ", 7043200, "RTTY  45 BPS");
  CHECK(waitFor([&] { return count() >= 3; }));
  {
    std::lock_guard<std::mutex> lk(mu);
    if (spots.size() >= 3) {
      CHECK_EQ(spots[1].call, std::string("DL1ABC"));
      CHECK_EQ(spots[1].spotter, std::string("WA8Q"));   // the client drops the "-#"
      CHECK_EQ(spots[1].freqHz, 14083500.0);
      CHECK_EQ(spots[1].comment, std::string("RTTY  45 BPS  CQ"));
      CHECK(looksLikeRtty(spots[1]));
      CHECK_EQ(spots[1].timeUtc.size(), size_t(5));
      CHECK_EQ(spots[2].call, std::string("JA1XYZ"));
      CHECK_EQ(spots[2].freqHz, 7043200.0);
    }
  }
  CHECK_EQ(server.spotsSent(), uint64_t(3));

  // sh/dx replays the list.
  client.send("sh/dx");
  CHECK(waitFor([&] { return count() >= 6; }));

  // A client that goes away is forgotten, and spotting into the void is harmless.
  client.stop();
  CHECK(waitFor([&] { return server.clients() == 0; }));
  server.spot("K1ABC", 14085000, "RTTY");
  CHECK(waitFor([&] { return server.spotsSent() == 4; }));
  server.stop();
  CHECK(!server.running());
  server.spot("K1ABC", 14085000, "RTTY");   // stopped: dropped
  TEST_MAIN_END();
}
