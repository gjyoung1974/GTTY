// DX cluster (telnet) client: logs in, parses "DX de ..." spot lines and
// keeps the connection alive, reconnecting with back-off when it drops.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gtty {

struct Spot {
  std::string call;        // the DX station
  double freqHz = 0;
  std::string spotter;
  std::string comment;
  std::string timeUtc;     // "HH:MM" as given in the spot line ("" if absent)
  int64_t receivedAt = 0;  // unix seconds when we received it
};

// Parse one cluster line such as
//   "DX de W1AW:     14083.5  DL1ABC       RTTY cq                   1234Z FN31"
// Returns false if the line is not a spot.
bool parseSpotLine(const std::string& line, Spot& out);
// True if the spot is plausibly RTTY: the comment mentions RTTY, or it names no
// other mode and the frequency lies in a customary RTTY/digital segment.
bool looksLikeRtty(const Spot& s);

struct ClusterConfig {
  bool enabled = false;
  std::string host;
  int port = 7300;
  std::string login;       // callsign sent at the login prompt
  std::string commands;    // newline-separated commands sent after login (filters etc.)
};

class ClusterClient {
 public:
  ClusterClient() = default;
  ~ClusterClient();
  ClusterClient(const ClusterClient&) = delete;
  ClusterClient& operator=(const ClusterClient&) = delete;

  void start(const ClusterConfig& cfg);   // background connect; reconnects on failure
  void stop();
  bool running() const { return run_.load(); }
  bool connected() const { return connected_.load(); }
  void send(const std::string& line);     // queued until connected; CRLF appended

  // All callbacks run on the client thread.
  std::function<void(const Spot&)> onSpot;
  std::function<void(bool connected, const std::string& info)> onStatus;
  std::function<void(const std::string& line)> onLine;   // every non-spot line (prompts, replies)

 private:
  void loop();
  int connectOnce(std::string& err);
  void status(bool connected, const std::string& info);
  void flushOutgoing(int fd);

  ClusterConfig cfg_;
  std::thread thread_;
  std::atomic<bool> run_{false};
  std::atomic<bool> connected_{false};
  std::mutex mu_;
  std::vector<std::string> outgoing_;
};

}  // namespace gtty
