// SQLite logbook with ADIF export and dupe checking.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace gtty {

struct Qso {
  int64_t id = 0;
  std::string timeUtc;   // YYYY-MM-DD HH:MM:SS
  std::string call;
  std::string band;
  std::string mode = "RTTY";
  double freqHz = 0;
  std::string rstSent = "599";
  std::string rstRcvd = "599";
  std::string exchSent;
  std::string exchRcvd;
  std::string name;
  std::string qth;
  std::string grid;
  std::string comment;
};

class Logbook {
 public:
  Logbook() = default;
  ~Logbook();
  Logbook(const Logbook&) = delete;
  Logbook& operator=(const Logbook&) = delete;

  bool open(const std::string& path, std::string& err);
  void close();
  bool isOpen() const { return db_ != nullptr; }

  bool add(Qso& q, std::string& err);  // fills q.id
  std::vector<Qso> recent(int limit) const;
  std::vector<Qso> find(const std::string& call) const;
  bool isDupe(const std::string& call, const std::string& band, const std::string& mode) const;
  int count() const;
  bool exportAdif(const std::string& path, std::string& err) const;

  static std::string nowUtc();

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace gtty
