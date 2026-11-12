#include "core/log/adif.h"

#include <cctype>
#include <cstdio>

namespace gtty {

std::string adifRecord(const Qso& q, const std::string& myCall, const std::string& myGrid, AdifStyle style) {
  const bool upper = style == AdifStyle::Fldigi;
  std::string out;
  auto f = [&](const char* name, const std::string& v) {
    if (v.empty()) return;
    out += '<';
    for (const char* p = name; *p; ++p)
      out += static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(*p)) : *p);
    out += ':' + std::to_string(v.size()) + '>' + v;
  };
  // timeUtc is "YYYY-MM-DD HH:MM:SS".
  std::string date = q.timeUtc.size() >= 10 ? q.timeUtc.substr(0, 4) + q.timeUtc.substr(5, 2) + q.timeUtc.substr(8, 2) : "";
  std::string time = q.timeUtc.size() >= 19 ? q.timeUtc.substr(11, 2) + q.timeUtc.substr(14, 2) + q.timeUtc.substr(17, 2) : "";
  f("call", q.call);
  f("gridsquare", q.grid);
  f("mode", q.mode);
  f("rst_sent", q.rstSent);
  f("rst_rcvd", q.rstRcvd);
  f("qso_date", date);
  f("time_on", time);
  f("qso_date_off", date);
  f("time_off", time);
  f("band", q.band);
  if (q.freqHz > 0) {
    char freq[32];
    snprintf(freq, sizeof freq, "%.6f", q.freqHz / 1e6);
    f("freq", freq);
  }
  f("station_callsign", myCall);
  f("my_gridsquare", myGrid);
  f("stx_string", q.exchSent);
  f("srx_string", q.exchRcvd);
  f("name", q.name);
  f("qth", q.qth);
  f("comment", q.comment);
  out += upper ? "<eor>" : "<EOR>";
  return out;
}

}  // namespace gtty
