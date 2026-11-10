#include "core/log/logbook.h"

#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <fstream>

namespace gtty {

Logbook::~Logbook() { close(); }

bool Logbook::open(const std::string& path, std::string& err) {
  close();
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    err = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  const char* ddl =
      "CREATE TABLE IF NOT EXISTS qsos ("
      " id INTEGER PRIMARY KEY AUTOINCREMENT, time_utc TEXT NOT NULL, call TEXT NOT NULL,"
      " band TEXT, mode TEXT, freq_hz REAL, rst_sent TEXT, rst_rcvd TEXT, exch_sent TEXT,"
      " exch_rcvd TEXT, name TEXT, qth TEXT, grid TEXT, comment TEXT);"
      "CREATE INDEX IF NOT EXISTS qsos_call ON qsos(call);";
  char* msg = nullptr;
  if (sqlite3_exec(db_, ddl, nullptr, nullptr, &msg) != SQLITE_OK) {
    err = msg ? msg : "ddl failed";
    sqlite3_free(msg);
    return false;
  }
  return true;
}

void Logbook::close() {
  if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

std::string Logbook::nowUtc() {
  auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

bool Logbook::add(Qso& q, std::string& err) {
  if (!db_) { err = "logbook not open"; return false; }
  if (q.timeUtc.empty()) q.timeUtc = nowUtc();
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "INSERT INTO qsos(time_utc,call,band,mode,freq_hz,rst_sent,rst_rcvd,exch_sent,exch_rcvd,"
      "name,qth,grid,comment) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
  int i = 1;
  auto bind = [&](const std::string& s) { sqlite3_bind_text(st, i++, s.c_str(), -1, SQLITE_TRANSIENT); };
  bind(q.timeUtc); bind(q.call); bind(q.band); bind(q.mode);
  sqlite3_bind_double(st, i++, q.freqHz);
  bind(q.rstSent); bind(q.rstRcvd); bind(q.exchSent); bind(q.exchRcvd);
  bind(q.name); bind(q.qth); bind(q.grid); bind(q.comment);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  if (!ok) err = sqlite3_errmsg(db_);
  sqlite3_finalize(st);
  if (ok) q.id = sqlite3_last_insert_rowid(db_);
  return ok;
}

namespace {
Qso rowToQso(sqlite3_stmt* st) {
  Qso q;
  auto txt = [&](int c) { const unsigned char* s = sqlite3_column_text(st, c); return s ? std::string(reinterpret_cast<const char*>(s)) : std::string(); };
  q.id = sqlite3_column_int64(st, 0);
  q.timeUtc = txt(1); q.call = txt(2); q.band = txt(3); q.mode = txt(4);
  q.freqHz = sqlite3_column_double(st, 5);
  q.rstSent = txt(6); q.rstRcvd = txt(7); q.exchSent = txt(8); q.exchRcvd = txt(9);
  q.name = txt(10); q.qth = txt(11); q.grid = txt(12); q.comment = txt(13);
  return q;
}
const char* kSelect = "SELECT id,time_utc,call,band,mode,freq_hz,rst_sent,rst_rcvd,exch_sent,exch_rcvd,name,qth,grid,comment FROM qsos";
}  // namespace

std::vector<Qso> Logbook::recent(int limit) const {
  std::vector<Qso> v;
  if (!db_) return v;
  std::string sql = std::string(kSelect) + " ORDER BY id DESC LIMIT ?";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return v;
  sqlite3_bind_int(st, 1, limit);
  while (sqlite3_step(st) == SQLITE_ROW) v.push_back(rowToQso(st));
  sqlite3_finalize(st);
  return v;
}

std::vector<Qso> Logbook::find(const std::string& call) const {
  std::vector<Qso> v;
  if (!db_) return v;
  std::string sql = std::string(kSelect) + " WHERE call = ? ORDER BY id DESC";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return v;
  sqlite3_bind_text(st, 1, call.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(st) == SQLITE_ROW) v.push_back(rowToQso(st));
  sqlite3_finalize(st);
  return v;
}

bool Logbook::isDupe(const std::string& call, const std::string& band, const std::string& mode) const {
  if (!db_) return false;
  sqlite3_stmt* st = nullptr;
  const char* sql = "SELECT COUNT(*) FROM qsos WHERE call=? AND band=? AND mode=?";
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_text(st, 1, call.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, band.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, mode.c_str(), -1, SQLITE_TRANSIENT);
  int n = 0;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n > 0;
}

int Logbook::count() const {
  if (!db_) return 0;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM qsos", -1, &st, nullptr) != SQLITE_OK) return 0;
  int n = 0;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n;
}

bool Logbook::exportAdif(const std::string& path, std::string& err) const {
  std::ofstream f(path);
  if (!f) { err = "cannot write " + path; return false; }
  f << "gtty ADIF export\n<ADIF_VER:5>3.1.4\n<PROGRAMID:4>gtty\n<EOH>\n";
  auto field = [&](const char* name, const std::string& v) {
    if (!v.empty()) f << '<' << name << ':' << v.size() << '>' << v;
  };
  for (const Qso& q : recent(1 << 30)) {
    std::string date = q.timeUtc.substr(0, 4) + q.timeUtc.substr(5, 2) + q.timeUtc.substr(8, 2);
    std::string time = q.timeUtc.size() >= 19 ? q.timeUtc.substr(11, 2) + q.timeUtc.substr(14, 2) + q.timeUtc.substr(17, 2) : "";
    field("CALL", q.call);
    field("QSO_DATE", date);
    field("TIME_ON", time);
    field("BAND", q.band);
    field("MODE", q.mode);
    if (q.freqHz > 0) {
      char buf[32];
      snprintf(buf, sizeof buf, "%.6f", q.freqHz / 1e6);
      field("FREQ", buf);
    }
    field("RST_SENT", q.rstSent);
    field("RST_RCVD", q.rstRcvd);
    field("STX_STRING", q.exchSent);
    field("SRX_STRING", q.exchRcvd);
    field("NAME", q.name);
    field("QTH", q.qth);
    field("GRIDSQUARE", q.grid);
    field("COMMENT", q.comment);
    f << "<EOR>\n";
  }
  return true;
}

}  // namespace gtty
