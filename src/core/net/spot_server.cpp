#include "core/net/spot_server.h"

#include "core/log/callsign.h"
#include "core/net/socket_compat.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace gtty {

namespace {

constexpr size_t kMaxClients = 16;
constexpr size_t kHistory = 40;
constexpr size_t kMaxBacklog = 256 * 1024;   // a client that stopped reading is dropped past this

std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string hhmmNowUtc() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[8];
  snprintf(buf, sizeof buf, "%02d%02d", tm.tm_hour, tm.tm_min);
  return buf;
}

}  // namespace

std::string formatSpotLine(const std::string& spotter, double freqHz, const std::string& call,
                           const std::string& comment, const std::string& hhmmUtc) {
  std::string line = "DX de " + spotter + ":";
  char freq[32];
  snprintf(freq, sizeof freq, "%.1f", freqHz / 1000.0);
  size_t flen = std::strlen(freq);
  // Right-align the frequency so it ends in column 24, with at least one space before it.
  size_t pad = line.size() + flen < 24 ? 24 - line.size() - flen : 1;
  line.append(pad, ' ');
  line += freq;
  line += "  ";
  char rest[80];
  snprintf(rest, sizeof rest, "%-12s %-30.30s %.4sZ", call.c_str(), comment.c_str(), hhmmUtc.c_str());
  return line + rest;
}

std::string spottableCall(const std::string& text, const std::string& myCall, bool* cq) {
  std::string t = upper(text);
  for (auto& c : t)
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
  const std::string my = upper(myCall);
  auto spans = findCallsigns(t);
  auto wordBefore = [&](size_t pos) {
    size_t e = pos;
    while (e > 0 && t[e - 1] == ' ') --e;
    size_t s = e;
    while (s > 0 && t[s - 1] != ' ') --s;
    return t.substr(s, e - s);
  };
  std::string best;
  for (const auto& [pos, len] : spans) {
    if (pos + len == t.size()) continue;   // still being received: "K1A" may become K1ABC
    std::string c = t.substr(pos, len);
    if (c == my) continue;
    std::string w = wordBefore(pos);
    long seen = std::count_if(spans.begin(), spans.end(),
                              [&](const auto& sp) { return t.compare(sp.first, sp.second, c) == 0 && sp.second == len; });
    if (w == "DE" || w == "CQ" || seen >= 2) best = c;
  }
  if (cq) {
    std::string padded = " " + t + " ";
    *cq = padded.find(" CQ ") != std::string::npos || padded.find(" TEST ") != std::string::npos;
  }
  return best;
}

SpotServer::~SpotServer() { stop(); }

bool SpotServer::start(const std::string& bindAddr, uint16_t port, const std::string& spotter, std::string& err) {
  stop();
  spotter_ = upper(spotter.empty() ? std::string("GTTY") : spotter) + "-#";
  fd_ = net::socketFd(AF_INET, SOCK_STREAM, 0, /*nonblock=*/true);
  if (fd_ < 0) { err = std::strerror(errno); return false; }
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, bindAddr.empty() ? "127.0.0.1" : bindAddr.c_str(), &sa.sin_addr) != 1) {
    err = "bad bind address " + bindAddr;
    ::close(fd_); fd_ = -1; return false;
  }
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) {
    err = std::string("bind ") + bindAddr + ":" + std::to_string(port) + ": " + std::strerror(errno);
    ::close(fd_); fd_ = -1; return false;
  }
  if (::listen(fd_, 8) < 0) { err = std::strerror(errno); ::close(fd_); fd_ = -1; return false; }
  run_ = true;
  thread_ = std::thread([this] { loop(); });
  return true;
}

void SpotServer::stop() {
  run_ = false;
  if (thread_.joinable()) thread_.join();
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  clients_ = 0;
  std::lock_guard<std::mutex> lk(mu_);
  pending_.clear();
}

void SpotServer::spot(const std::string& call, double freqHz, const std::string& comment) {
  if (!run_ || call.empty() || freqHz <= 0) return;
  std::string line = formatSpotLine(spotter_, freqHz, upper(call), comment, hhmmNowUtc());
  std::lock_guard<std::mutex> lk(mu_);
  pending_.push_back(std::move(line));
}

void SpotServer::flush(Client& c) {
  while (!c.out.empty()) {
    ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), net::sendFlags());
    if (n > 0) { c.out.erase(0, static_cast<size_t>(n)); continue; }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (c.out.size() > kMaxBacklog) c.closing = true;
      return;
    }
    c.closing = true;
    c.out.clear();
    return;
  }
}

void SpotServer::onLine(Client& c, const std::string& raw) {
  std::string line = raw;
  while (!line.empty() && line.back() == ' ') line.pop_back();
  if (!c.loggedIn) {
    if (line.empty()) { c.out += "login: "; return; }
    // Whatever is sent first is the login; a cluster asks for a callsign and nothing else.
    c.loggedIn = true;
    c.out += "Hello " + upper(line) + ", this is the gtty skimmer of " + spotter_ +
             ": RTTY stations decoded across the receiver passband.\r\n";
    for (const auto& h : history_) c.out += h + "\r\n";
    return;
  }
  std::string cmd = upper(line);
  if (cmd == "BYE" || cmd == "QUIT" || cmd == "EXIT" || cmd == "B" || cmd == "Q") {
    c.out += "73\r\n";
    c.closing = true;
  } else if (cmd.compare(0, 5, "SH/DX") == 0 || cmd.compare(0, 5, "SH DX") == 0 || cmd.compare(0, 7, "SHOW/DX") == 0) {
    for (const auto& h : history_) c.out += h + "\r\n";
  }
  // Everything else (filters, "set dx ...", keep-alive blank lines) is accepted and ignored.
}

void SpotServer::loop() {
  std::vector<Client> clients;
  while (run_) {
    std::vector<pollfd> pfds;
    pfds.push_back({fd_, POLLIN, 0});
    for (const auto& c : clients) pfds.push_back({c.fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
    int r = ::poll(pfds.data(), pfds.size(), 100);
    if (r < 0 && errno != EINTR) break;

    if (r > 0 && (pfds[0].revents & POLLIN)) {
      int fd = ::accept(fd_, nullptr, nullptr);
      if (fd >= 0) {
        if (clients.size() >= kMaxClients) {
          ::close(fd);
        } else {
          net::noSigPipe(fd);
          ::fcntl(fd, F_SETFD, FD_CLOEXEC);
          ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
          Client c;
          c.fd = fd;
          c.out = "gtty skimmer spot feed\r\nlogin: ";
          clients.push_back(std::move(c));
        }
      }
    }

    for (size_t i = 0; r > 0 && i + 1 < pfds.size() && i < clients.size(); ++i) {
      Client& c = clients[i];
      if (pfds[i + 1].fd != c.fd) continue;
      if (pfds[i + 1].revents & (POLLERR | POLLHUP | POLLNVAL)) c.closing = true;
      if (!(pfds[i + 1].revents & POLLIN)) continue;
      char tmp[1024];
      ssize_t n = ::recv(c.fd, tmp, sizeof tmp, 0);
      if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { c.closing = true; c.out.clear(); continue; }
      for (ssize_t k = 0; k < n; ++k) {
        unsigned char ch = static_cast<unsigned char>(tmp[k]);
        if (ch == '\n') { onLine(c, c.in); c.in.clear(); }
        else if (ch >= 0x20 && ch < 0x7f && c.in.size() < 200) c.in.push_back(static_cast<char>(ch));
        // CR, telnet option bytes and other controls are dropped.
      }
    }

    std::vector<std::string> lines;
    {
      std::lock_guard<std::mutex> lk(mu_);
      lines.swap(pending_);
    }
    for (const auto& l : lines) {
      history_.push_back(l);
      if (history_.size() > kHistory) history_.pop_front();
      spots_++;
      for (auto& c : clients)
        if (c.loggedIn && !c.closing) c.out += l + "\r\n";
    }

    for (auto& c : clients) flush(c);
    for (auto it = clients.begin(); it != clients.end();) {
      if (it->closing) { ::close(it->fd); it = clients.erase(it); }
      else ++it;
    }
    clients_ = clients.size();
  }
  for (auto& c : clients) ::close(c.fd);
  clients_ = 0;
}

}  // namespace gtty
