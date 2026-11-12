// Logged QSOs handed to a logbook server the way fldigi hands them to fllog.
// The server here reads the request the way QLog's FldigiTCPServer does: one
// read, everything after the blank line is the XML, and the ADIF record is the
// text of the first <value> element, which must not hold a child element.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>
#include <string>
#include <thread>

#include "core/log/adif.h"
#include "core/net/fllog_client.h"
#include "core/net/socket_compat.h"
#include "check.h"

using namespace gtty;

namespace {

struct FakeLogbook {
  int fd = -1;
  uint16_t port = 0;
  std::thread thread;
  std::atomic<bool> run{true};
  std::mutex mu;
  std::vector<std::string> records;   // the <value> text of each log.add_record, XML entities undone
  std::string answer = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: 96\r\n\r\n"
                       "<?xml version=\"1.0\" encoding=\"UTF-8\"?><methodResponse><params><param><value/></param></params></methodResponse>";

  bool start() {
    fd = net::socketFd(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    for (uint16_t p = 18421; p < 18441; ++p) {
      sockaddr_in sa{};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(p);
      inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
      if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) == 0 && ::listen(fd, 4) == 0) { port = p; break; }
    }
    if (!port) return false;
    thread = std::thread([this] { loop(); });
    return true;
  }
  void stop() {
    run = false;
    if (thread.joinable()) thread.join();
    if (fd >= 0) ::close(fd);
  }
  static std::string unescape(std::string s) {
    for (auto [from, to] : {std::pair<const char*, const char*>{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&amp;", "&"}}) {
      size_t at = 0;
      while ((at = s.find(from, at)) != std::string::npos) { s.replace(at, strlen(from), to); at += strlen(to); }
    }
    return s;
  }
  void loop() {
    while (run) {
      pollfd p{fd, POLLIN, 0};
      if (::poll(&p, 1, 50) <= 0) continue;
      int c = ::accept(fd, nullptr, nullptr);
      if (c < 0) continue;
      pollfd cp{c, POLLIN, 0};
      std::string data;
      if (::poll(&cp, 1, 2000) > 0) {
        // Let the whole request arrive, then read once, as readAll() on the first readyRead does.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        char buf[8192];
        ssize_t n = ::recv(c, buf, sizeof buf, 0);
        if (n > 0) data.assign(buf, static_cast<size_t>(n));
      }
      size_t split = data.find("\r\n\r\n");
      std::string xml = split == std::string::npos ? "" : data.substr(split + 4);
      std::string value;
      bool ok = xml.find("<methodName>log.add_record</methodName>") != std::string::npos;
      size_t a = xml.find("<value>"), b = xml.find("</value>");
      if (ok && a != std::string::npos && b != std::string::npos) {
        value = xml.substr(a + 7, b - a - 7);
        if (value.find('<') != std::string::npos) ok = false;   // QXmlStreamReader::readElementText() refuses child elements
      } else {
        ok = false;
      }
      std::string out = ok ? answer : std::string("HTTP/1.1 400 Internal Error\r\n");
      ::send(c, out.data(), out.size(), net::sendFlags());
      ::close(c);
      if (ok) {
        std::lock_guard<std::mutex> lk(mu);
        records.push_back(unescape(value));
      }
    }
  }
};

}  // namespace

int main() {
  Qso q;
  q.timeUtc = "2026-11-12 18:04:59";
  q.call = "DL1ABC";
  q.band = "20m";
  q.freqHz = 14083500;
  q.rstSent = "599";
  q.rstRcvd = "599";
  q.exchSent = "012";
  q.exchRcvd = "34 & <5>";
  q.name = "Hans";
  q.grid = "JO62";

  // ------------------------------------------------------------ ADIF record
  std::string fl = adifRecord(q, "WA8Q", "EN82", AdifStyle::Fldigi);
  CHECK(fl.find("<CALL:6>DL1ABC") != std::string::npos);
  CHECK(fl.find("<QSO_DATE:8>20261112") != std::string::npos);
  CHECK(fl.find("<TIME_ON:6>180459") != std::string::npos);
  CHECK(fl.find("<FREQ:9>14.083500") != std::string::npos);
  CHECK(fl.find("<BAND:3>20m") != std::string::npos);
  CHECK(fl.find("<MODE:4>RTTY") != std::string::npos);
  CHECK(fl.find("<STX_STRING:3>012") != std::string::npos);
  CHECK(fl.find("<SRX_STRING:8>34 & <5>") != std::string::npos);
  CHECK(fl.find("<STATION_CALLSIGN:4>WA8Q") != std::string::npos);
  CHECK(fl.find("<MY_GRIDSQUARE:4>EN82") != std::string::npos);
  CHECK(fl.find("<GRIDSQUARE:4>JO62") != std::string::npos);
  CHECK(fl.find("QTH") == std::string::npos);       // empty fields are left out
  CHECK(fl.size() > 5 && fl.substr(fl.size() - 5) == "<eor>");
  std::string ws = adifRecord(q, "WA8Q", "EN82", AdifStyle::Wsjtx);
  CHECK(ws.find("<call:6>DL1ABC") != std::string::npos);
  CHECK(ws.find("<srx_string:8>34 & <5>") != std::string::npos);
  CHECK(ws.size() > 5 && ws.substr(ws.size() - 5) == "<EOR>");
  Qso bare;
  bare.call = "K1ABC";
  CHECK(adifRecord(bare, "", "", AdifStyle::Wsjtx).find("<freq") == std::string::npos);

  // ------------------------------------------------------------ over the wire
  FakeLogbook book;
  CHECK(book.start());
  if (!book.port) TEST_MAIN_END();

  std::string err;
  CHECK(fllogAddRecord("127.0.0.1", book.port, fl, err));
  CHECK_EQ(err, std::string(""));
  {
    std::lock_guard<std::mutex> lk(book.mu);
    CHECK_EQ(book.records.size(), size_t(1));
    if (!book.records.empty()) CHECK_EQ(book.records[0], fl);   // escaped on the way, intact on arrival
  }

  // Nobody listening: an error, promptly, and no crash.
  uint16_t dead = static_cast<uint16_t>(book.port + 100);
  auto t0 = std::chrono::steady_clock::now();
  CHECK(!fllogAddRecord("127.0.0.1", dead, fl, err));
  CHECK(!err.empty());
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));

  // The forwarder: records go out in order from its own thread and each one is reported.
  {
    FllogForwarder fwd;
    std::mutex rm;
    std::vector<std::string> results;
    fwd.onResult = [&](bool ok, const std::string& label, const std::string& e) {
      std::lock_guard<std::mutex> lk(rm);
      results.push_back(label + (ok ? ":ok" : ":failed " + e));
    };
    fwd.setTarget("127.0.0.1", book.port);
    fwd.add("K1AAA", "<CALL:5>K1AAA<eor>");
    fwd.add("K1BBB", "<CALL:5>K1BBB<eor>");
    fwd.setTarget("127.0.0.1", dead);
    fwd.add("K1CCC", "<CALL:5>K1CCC<eor>");
    for (int i = 0; i < 500; ++i) {
      { std::lock_guard<std::mutex> lk(rm); if (results.size() >= 3) break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::lock_guard<std::mutex> lk(rm);
    CHECK_EQ(results.size(), size_t(3));
    if (results.size() == 3) {
      CHECK_EQ(results[0], std::string("K1AAA:ok"));
      CHECK_EQ(results[1], std::string("K1BBB:ok"));
      CHECK(results[2].compare(0, 13, "K1CCC:failed ") == 0);
    }
    std::lock_guard<std::mutex> lk2(book.mu);
    CHECK_EQ(book.records.size(), size_t(3));
    if (book.records.size() == 3) CHECK_EQ(book.records[2], std::string("<CALL:5>K1BBB<eor>"));
  }

  // A server that answers with an error status is reported as a failure.
  book.answer = "HTTP/1.1 500 Oops\r\n\r\n";
  CHECK(!fllogAddRecord("127.0.0.1", book.port, fl, err));
  CHECK(err.find("500") != std::string::npos);

  book.stop();
  TEST_MAIN_END();
}
