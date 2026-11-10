// Callbook lookup (QRZ.com XML API or HamQTH) on a worker thread.
// Built against libcurl when available; otherwise available() is false.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gtty {

struct CallbookConfig {
  enum class Provider { None, Qrz, HamQth };
  Provider provider = Provider::None;
  std::string username, password;
};

struct CallbookResult {
  std::string call;
  bool found = false;
  std::string name, qth, state, country, grid;
  std::string error;   // non-empty when the lookup failed (login, network, not found)
  std::string summary() const;   // "Bob, Springfield MA, USA (FN42)"
};

class Callbook {
 public:
  Callbook() = default;
  ~Callbook();
  Callbook(const Callbook&) = delete;
  Callbook& operator=(const Callbook&) = delete;

  static bool available();
  void configure(const CallbookConfig& cfg);   // drops the cached session
  bool enabled() const;
  // Asynchronous; only the most recent request is served if several queue up.
  void lookup(const std::string& call);
  std::function<void(const CallbookResult&)> onResult;   // worker thread

  // Response parsers (exposed for tests). The session id/key is returned when present.
  static std::string xmlText(const std::string& xml, const std::string& tag);
  static CallbookResult parseQrz(const std::string& xml, std::string* sessionKey);
  static CallbookResult parseHamQth(const std::string& xml, std::string* sessionId);

 private:
  void worker();
  CallbookResult query(const std::string& call);
  bool http(const std::string& url, std::string& body, std::string& err);

  CallbookConfig cfg_;
  std::string session_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::string pending_;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace gtty
