#include "core/net/cluster_client.h"

#include "core/net/socket_compat.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstring>

namespace gtty {

namespace {
std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
int64_t nowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

std::string toDisplayText(const std::string& in) {
  // Valid UTF-8 passes; anything else is read as Latin-1, which is what older nodes send.
  bool valid = true;
  for (size_t i = 0; i < in.size() && valid;) {
    unsigned char c = static_cast<unsigned char>(in[i]);
    size_t n = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    if (n == 0 || i + n > in.size()) { valid = false; break; }
    for (size_t k = 1; k < n; ++k)
      if ((static_cast<unsigned char>(in[i + k]) & 0xC0) != 0x80) valid = false;
    if (n == 3 && c == 0xE0 && static_cast<unsigned char>(in[i + 1]) < 0xA0) valid = false;   // overlong
    if (n == 3 && c == 0xED && static_cast<unsigned char>(in[i + 1]) > 0x9F) valid = false;   // surrogates
    if (n == 4 && c == 0xF0 && static_cast<unsigned char>(in[i + 1]) < 0x90) valid = false;   // overlong
    if (n == 4 && c == 0xF4 && static_cast<unsigned char>(in[i + 1]) > 0x8F) valid = false;   // past U+10FFFF
    i += n;
  }
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(in[i]);
    if (c < 0x20 || c == 0x7f) continue;                       // controls, DEL
    if (c < 0x80 || valid) {
      // C1 controls (U+0080..U+009F, encoded C2 80..C2 9F) print nothing either.
      if (valid && c == 0xC2 && i + 1 < in.size() && static_cast<unsigned char>(in[i + 1]) < 0xA0) { ++i; continue; }
      out.push_back(static_cast<char>(c));
    } else if (c >= 0xA0) {                                    // Latin-1 printable
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

bool parseSpotLine(const std::string& raw, Spot& out) {
  std::string line = trim(raw);
  if (line.size() < 20 || upper(line.substr(0, 6)) != "DX DE ") return false;
  size_t i = 6;
  auto skipSpaces = [&] { while (i < line.size() && line[i] == ' ') ++i; };
  auto token = [&] {
    skipSpaces();
    size_t s = i;
    while (i < line.size() && line[i] != ' ') ++i;
    return line.substr(s, i - s);
  };
  std::string spotter = token();
  while (!spotter.empty() && spotter.back() == ':') spotter.pop_back();
  spotter = spotter.substr(0, spotter.find('-'));   // drop "-#" (skimmer) / "-2" (node) suffixes
  std::string freq = token();
  std::string call = token();
  if (spotter.empty() || freq.empty() || call.empty()) return false;
  char* end = nullptr;
  double khz = std::strtod(freq.c_str(), &end);
  if (!end || *end != '\0' || khz <= 0) return false;
  std::string rest = i < line.size() ? line.substr(i) : "";
  // Time stamp: four digits followed by Z, searched from the right.
  std::string timeUtc;
  size_t z = std::string::npos;
  for (size_t k = rest.size(); k >= 5; --k) {
    size_t p = k - 5;
    if ((rest[p + 4] == 'Z' || rest[p + 4] == 'z') && std::isdigit(static_cast<unsigned char>(rest[p])) &&
        std::isdigit(static_cast<unsigned char>(rest[p + 1])) && std::isdigit(static_cast<unsigned char>(rest[p + 2])) &&
        std::isdigit(static_cast<unsigned char>(rest[p + 3])) && (p == 0 || rest[p - 1] == ' ') &&
        (p + 5 == rest.size() || rest[p + 5] == ' ')) {
      z = p;
      break;
    }
    if (k == 5) break;
  }
  if (z != std::string::npos) {
    timeUtc = rest.substr(z, 2) + ":" + rest.substr(z + 2, 2);
    rest = rest.substr(0, z);
  }
  out.spotter = upper(spotter);
  out.call = upper(call);
  out.freqHz = khz * 1000.0;
  out.comment = trim(rest);
  out.timeUtc = timeUtc;
  out.receivedAt = nowUnix();
  return true;
}

bool looksLikeRtty(const Spot& s) {
  std::string c = upper(s.comment);
  if (c.find("RTTY") != std::string::npos) return true;
  static const char* others[] = {"FT8", "FT4", "PSK", "JS8", "JT65", "JT9", "MSK", "OLIVIA", "WSPR",
                                 "SSB", "USB", "LSB", "FM ", "CW ", " CW", "SSTV", "AM ", "FSK441", "Q65"};
  for (const char* o : others) {
    if (c.find(o) != std::string::npos) return false;
    if (c == trim(o)) return false;
  }
  double k = s.freqHz / 1000.0;
  struct Seg { double lo, hi; };
  static const Seg segs[] = {{1800, 1840},   {3570, 3600},   {7025, 7050},   {7080, 7110},
                             {10130, 10150}, {14070, 14112}, {18095, 18110}, {21070, 21110},
                             {24910, 24930}, {28070, 28150}, {50300, 50350}};
  for (const auto& g : segs)
    if (k >= g.lo && k <= g.hi) return true;
  return false;
}

ClusterClient::~ClusterClient() { stop(); }

void ClusterClient::start(const ClusterConfig& cfg) {
  stop();
  cfg_ = cfg;
  if (!cfg_.enabled || cfg_.host.empty()) return;
  run_ = true;
  thread_ = std::thread([this] { loop(); });
}

void ClusterClient::stop() {
  run_ = false;
  if (thread_.joinable()) thread_.join();
  connected_ = false;
}

void ClusterClient::send(const std::string& line) {
  std::lock_guard<std::mutex> lk(mu_);
  outgoing_.push_back(line);
}

void ClusterClient::status(bool c, const std::string& info) {
  connected_ = c;
  if (onStatus) onStatus(c, info);
}

int ClusterClient::connectOnce(std::string& err) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  int rc = getaddrinfo(cfg_.host.c_str(), std::to_string(cfg_.port).c_str(), &hints, &res);
  if (rc != 0) { err = gai_strerror(rc); return -1; }
  int fd = -1;
  for (addrinfo* a = res; a && run_; a = a->ai_next) {
    fd = net::socketFd(a->ai_family, a->ai_socktype, a->ai_protocol, /*nonblock=*/true);
    if (fd < 0) continue;
    if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
    if (errno == EINPROGRESS) {
      pollfd p{fd, POLLOUT, 0};
      bool ok = false;
      for (int w = 0; w < 100 && run_; ++w) {  // up to 10 s
        int r = ::poll(&p, 1, 100);
        if (r > 0) {
          int soerr = 0;
          socklen_t len = sizeof soerr;
          getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
          ok = soerr == 0;
          if (!ok) err = std::strerror(soerr);
          break;
        }
      }
      if (ok) break;
    } else {
      err = std::strerror(errno);
    }
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0 && err.empty()) err = "connect timed out";
  return fd;
}

void ClusterClient::flushOutgoing(int fd) {
  std::vector<std::string> lines;
  {
    std::lock_guard<std::mutex> lk(mu_);
    lines.swap(outgoing_);
  }
  for (const auto& l : lines) {
    std::string s = l + "\r\n";
    size_t off = 0;
    while (off < s.size()) {
      ssize_t n = ::send(fd, s.data() + off, s.size() - off, net::sendFlags());
      if (n <= 0) return;
      off += static_cast<size_t>(n);
    }
  }
}

void ClusterClient::loop() {
  int backoff = 5;
  while (run_) {
    std::string err;
    status(false, "connecting to " + cfg_.host + ":" + std::to_string(cfg_.port));
    int fd = connectOnce(err);
    if (fd < 0) {
      status(false, "cluster: " + err + ", retry in " + std::to_string(backoff) + " s");
      for (int i = 0; i < backoff * 10 && run_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      backoff = std::min(backoff * 2, 120);
      continue;
    }
    backoff = 5;
    status(true, "connected to " + cfg_.host);
    std::string buf;
    bool loggedIn = false;
    auto lastRx = std::chrono::steady_clock::now();
    auto lastKeepalive = lastRx;
    while (run_) {
      if (loggedIn) flushOutgoing(fd);
      pollfd p{fd, POLLIN, 0};
      int r = ::poll(&p, 1, 200);
      auto now = std::chrono::steady_clock::now();
      if (r < 0) break;
      if (r == 0) {
        if (now - lastRx > std::chrono::minutes(20)) { err = "no data for 20 minutes"; break; }
        if (loggedIn && now - lastKeepalive > std::chrono::minutes(5)) {
          std::string nl = "\r\n";
          if (::send(fd, nl.data(), nl.size(), net::sendFlags()) <= 0) break;
          lastKeepalive = now;
        }
        // Login prompts arrive without a newline; answer once the stream goes quiet.
        if (!loggedIn && !buf.empty()) {
          std::string tail = upper(trim(buf));
          if (tail.size() >= 5 && tail.back() == ':' &&
              (tail.find("LOGIN") != std::string::npos || tail.find("CALL") != std::string::npos)) {
            std::string l = cfg_.login + "\r\n";
            if (::send(fd, l.data(), l.size(), net::sendFlags()) <= 0) break;
            loggedIn = true;
            buf.clear();
            if (onLine) onLine(tail + " " + cfg_.login);
            // Initial commands (filters, show/dx) after a short pause for the banner.
            std::lock_guard<std::mutex> lk(mu_);
            std::vector<std::string> cmds;
            size_t s = 0;
            while (s <= cfg_.commands.size()) {
              size_t e = cfg_.commands.find_first_of("\n;", s);
              std::string c = trim(cfg_.commands.substr(s, e == std::string::npos ? std::string::npos : e - s));
              if (!c.empty()) cmds.push_back(c);
              if (e == std::string::npos) break;
              s = e + 1;
            }
            outgoing_.insert(outgoing_.begin(), cmds.begin(), cmds.end());
          }
        }
        continue;
      }
      char tmp[4096];
      ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
      if (n <= 0) { err = n == 0 ? "connection closed" : std::strerror(errno); break; }
      lastRx = now;
      for (ssize_t k = 0; k < n; ++k) {
        unsigned char c = static_cast<unsigned char>(tmp[k]);
        if (c == 0xff) continue;  // telnet IAC sequences are ignored; servers tolerate no reply
        if (c == '\r') continue;
        if (c == '\n') {
          // What goes to the band map and the status line must be printable, valid UTF-8.
          buf = toDisplayText(buf);
          Spot s;
          if (parseSpotLine(buf, s)) { if (onSpot) onSpot(s); }
          else if (onLine && !trim(buf).empty()) onLine(trim(buf));
          buf.clear();
        } else if (c >= 0x20 || c == '\t') {
          buf.push_back(static_cast<char>(c));
          if (buf.size() > 4096) buf.erase(0, 2048);
        }
      }
    }
    ::close(fd);
    status(false, run_ ? "cluster: " + (err.empty() ? std::string("disconnected") : err) + ", reconnecting" : "cluster: off");
    for (int i = 0; i < 50 && run_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  connected_ = false;
}

}  // namespace gtty
