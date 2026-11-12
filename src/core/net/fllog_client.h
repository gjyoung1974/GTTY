// Hands a logged QSO to a logbook program the way fldigi hands it to fllog:
// one XML-RPC call, log.add_record, whose only parameter is the QSO as an ADIF
// record. fllog and QLog (both on port 8421) take QSOs this way.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gtty {

// Blocking, with a time-out of a few seconds on each step: call it off the UI
// thread. False with `err` set when the server cannot be reached or refuses.
bool fllogAddRecord(const std::string& host, uint16_t port, const std::string& adifRecord, std::string& err);

// Sends records one after the other from a thread of its own, so a logbook
// that is slow or switched off never holds up the caller.
class FllogForwarder {
 public:
  FllogForwarder() = default;
  ~FllogForwarder();
  FllogForwarder(const FllogForwarder&) = delete;
  FllogForwarder& operator=(const FllogForwarder&) = delete;

  void setTarget(const std::string& host, uint16_t port);
  // `label` (the callsign) only names the record in onResult.
  void add(const std::string& label, const std::string& adifRecord);
  void stop();   // waits for the record being sent, drops the ones still queued

  // Runs on the forwarder's thread, once per record.
  std::function<void(bool ok, const std::string& label, const std::string& err)> onResult;

 private:
  struct Job { std::string host; uint16_t port; std::string label, adif; };
  void loop();
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Job> jobs_;
  std::thread thread_;
  bool run_ = false;
  std::string host_ = "127.0.0.1";
  uint16_t port_ = 8421;
};

}  // namespace gtty
