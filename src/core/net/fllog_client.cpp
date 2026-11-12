#include "core/net/fllog_client.h"

#include "core/net/socket_compat.h"
#include "core/net/xmlrpc_server.h"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace gtty {

namespace {

constexpr int kTimeoutMs = 4000;

int connectTo(const std::string& host, uint16_t port, std::string& err) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
  if (rc != 0) { err = gai_strerror(rc); return -1; }
  int fd = -1;
  for (addrinfo* a = res; a; a = a->ai_next) {
    fd = net::socketFd(a->ai_family, a->ai_socktype, a->ai_protocol, /*nonblock=*/true);
    if (fd < 0) { err = std::strerror(errno); continue; }
    if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
    if (errno == EINPROGRESS) {
      pollfd p{fd, POLLOUT, 0};
      int soerr = ETIMEDOUT;
      if (::poll(&p, 1, kTimeoutMs) > 0) {
        socklen_t len = sizeof soerr;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
      }
      if (soerr == 0) break;
      err = std::strerror(soerr);
    } else {
      err = std::strerror(errno);
    }
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

}  // namespace

bool fllogAddRecord(const std::string& host, uint16_t port, const std::string& adifRecord, std::string& err) {
  int fd = connectTo(host, port, err);
  if (fd < 0) {
    if (err.empty()) err = "cannot connect";
    return false;
  }
  std::string body = xmlrpc::serializeRequest("log.add_record", {xmlrpc::Value::string(adifRecord)});
  // HTTP/1.0 and one write: QLog's server reads what has arrived when the socket
  // first becomes readable and answers that, then closes the connection.
  std::string req = "POST /RPC2 HTTP/1.0\r\nHost: " + host + ":" + std::to_string(port) +
                    "\r\nUser-Agent: gtty\r\nContent-Type: text/xml\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body;
  bool ok = true;
  size_t off = 0;
  while (off < req.size()) {
    ssize_t n = ::send(fd, req.data() + off, req.size() - off, net::sendFlags());
    if (n > 0) { off += static_cast<size_t>(n); continue; }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd p{fd, POLLOUT, 0};
      if (::poll(&p, 1, kTimeoutMs) > 0) continue;
    }
    err = n < 0 ? std::strerror(errno) : "connection closed";
    ok = false;
    break;
  }
  std::string resp;
  while (ok) {
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, kTimeoutMs) <= 0) {
      if (resp.empty()) { err = "no answer"; ok = false; }
      break;
    }
    char tmp[2048];
    ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (n <= 0) break;  // the server closes when it has answered
    resp.append(tmp, static_cast<size_t>(n));
    if (resp.find("</methodResponse>") != std::string::npos || resp.size() > (1u << 16)) break;
  }
  ::close(fd);
  if (!ok) return false;
  if (resp.empty()) { err = "connection closed without an answer"; return false; }
  size_t eol = resp.find("\r\n");
  std::string statusLine = resp.substr(0, eol);
  if (statusLine.find(" 200") == std::string::npos) { err = statusLine; return false; }
  if (resp.find("<fault>") != std::string::npos) { err = "the logbook refused the record"; return false; }
  return true;
}

FllogForwarder::~FllogForwarder() { stop(); }

void FllogForwarder::setTarget(const std::string& host, uint16_t port) {
  std::lock_guard<std::mutex> lk(mu_);
  host_ = host.empty() ? std::string("127.0.0.1") : host;
  port_ = port;
}

void FllogForwarder::add(const std::string& label, const std::string& adifRecord) {
  std::lock_guard<std::mutex> lk(mu_);
  jobs_.push_back({host_, port_, label, adifRecord});
  if (!run_) {   // first record, or first after stop(), which joined the old thread
    run_ = true;
    thread_ = std::thread([this] { loop(); });
  }
  cv_.notify_one();
}

void FllogForwarder::stop() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    run_ = false;
    jobs_.clear();
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void FllogForwarder::loop() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [this] { return !run_ || !jobs_.empty(); });
      if (!run_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    std::string err;
    bool ok = fllogAddRecord(job.host, job.port, job.adif, err);
    if (onResult) onResult(ok, job.label, err);
  }
}

}  // namespace gtty
