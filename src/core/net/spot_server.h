// Telnet spot feed: gtty as a small DX cluster node. Loggers connect to it as
// they would to a cluster or to a CW Skimmer and receive a "DX de" line for
// every station the skimmer identifies, so their band maps fill from the
// receiver's own passband.
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gtty {

// One spot in the column layout DXSpider uses and fixed-column readers such as
// TLF depend on: frequency (kHz) ending in column 24, callsign from column 26,
// comment from column 39, "HHMMZ" from column 70 (columns counted from 0).
// `hhmmUtc` is four digits. No line ending.
std::string formatSpotLine(const std::string& spotter, double freqHz, const std::string& call,
                           const std::string& comment, const std::string& hhmmUtc);

// The callsign that `text`, decoded on one frequency, identifies well enough to
// spot, or "": the last one that either follows DE or CQ or is seen twice.
// A word the text ends in is not considered, since it may be only the start of a
// callsign. `myCall` is never returned. `cq` is set when the text holds a CQ or TEST.
std::string spottableCall(const std::string& text, const std::string& myCall, bool* cq = nullptr);

class SpotServer {
 public:
  SpotServer() = default;
  ~SpotServer();
  SpotServer(const SpotServer&) = delete;
  SpotServer& operator=(const SpotServer&) = delete;

  // `spotter` is the callsign spots are signed with; "-#" is appended, the mark of a skimmer.
  bool start(const std::string& bindAddr, uint16_t port, const std::string& spotter, std::string& err);
  void stop();
  bool running() const { return fd_ >= 0; }

  // Queue a spot for every logged-in client. Any thread.
  void spot(const std::string& call, double freqHz, const std::string& comment);

  size_t clients() const { return clients_.load(); }
  uint64_t spotsSent() const { return spots_.load(); }

 private:
  struct Client {
    int fd = -1;
    std::string in, out;
    bool loggedIn = false;
    bool closing = false;
  };
  void loop();
  void onLine(Client& c, const std::string& line);
  static void flush(Client& c);

  int fd_ = -1;
  std::string spotter_;
  std::thread thread_;
  std::atomic<bool> run_{false};
  std::atomic<size_t> clients_{0};
  std::atomic<uint64_t> spots_{0};
  std::mutex mu_;
  std::vector<std::string> pending_;   // lines not yet handed to the clients
  std::deque<std::string> history_;    // the last spots, replayed at login and for sh/dx (server thread)
};

}  // namespace gtty
