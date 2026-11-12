// Exercises the socket layer over a real loopback connection: the portability
// shims in socket_compat.h (close-on-exec, non-blocking, SIGPIPE suppression)
// and the XML-RPC server's accept/serve path. The rest of the XML-RPC tests
// only cover parsing and serialization.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

#include "core/net/socket_compat.h"
#include "core/net/xmlrpc_server.h"
#include "check.h"

using namespace gtty;

namespace {

// Connect to 127.0.0.1:port, or -1.
int connectLoopback(uint16_t port) {
  int fd = net::socketFd(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) { ::close(fd); return -1; }
  return fd;
}

}  // namespace

int main() {
  // ---------------------------------------------------- socketFd() flag setup
  {
    int fd = net::socketFd(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    if (fd >= 0) {
      CHECK((::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
      CHECK((::fcntl(fd, F_GETFL) & O_NONBLOCK) == 0);  // not requested
      ::close(fd);
    }
    int nb = net::socketFd(AF_INET, SOCK_DGRAM, 0, /*nonblock=*/true);
    CHECK(nb >= 0);
    if (nb >= 0) {
      CHECK((::fcntl(nb, F_GETFD) & FD_CLOEXEC) != 0);
      CHECK((::fcntl(nb, F_GETFL) & O_NONBLOCK) != 0);
      ::close(nb);
    }
  }

  // ------------------------------------- a real request through the XML-RPC server
  xmlrpc::Server server;
  server.setHandler([](const std::string& method, const std::vector<xmlrpc::Value>& params) {
    if (method == "main.get_frequency") return xmlrpc::Value::number(14080.5);
    if (method == "text.add_tx" && !params.empty())
      return xmlrpc::Value::string("got:" + params[0].asString());
    return xmlrpc::Value::nil();
  });

  uint16_t port = 0;
  std::string err;
  for (uint16_t p = 18362; p < 18382; ++p) {
    if (server.start("127.0.0.1", p, err)) { port = p; break; }
  }
  CHECK(port != 0);
  if (port == 0) { std::fprintf(stderr, "could not bind a loopback port: %s\n", err.c_str()); TEST_MAIN_END(); }
  CHECK(server.running());

  int fd = -1;
  for (int attempt = 0; attempt < 50 && fd < 0; ++attempt) {
    fd = connectLoopback(port);
    if (fd < 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(fd >= 0);
  if (fd >= 0) {
    std::string body =
        "<?xml version=\"1.0\"?><methodCall><methodName>text.add_tx</methodName>"
        "<params><param><value><string>CQ DE N0CALL</string></value></param></params></methodCall>";
    std::string req = "POST /RPC2 HTTP/1.1\r\nContent-Type: text/xml\r\nContent-Length: " +
                      std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    size_t off = 0;
    while (off < req.size()) {
      ssize_t n = ::send(fd, req.data() + off, req.size() - off, net::sendFlags());
      if (n <= 0) break;
      off += static_cast<size_t>(n);
    }
    CHECK_EQ(off, req.size());

    std::string resp;
    char buf[2048];
    for (int i = 0; i < 100; ++i) {
      pollfd p{fd, POLLIN, 0};
      if (::poll(&p, 1, 100) <= 0) continue;
      ssize_t n = ::recv(fd, buf, sizeof buf, 0);
      if (n <= 0) break;
      resp.append(buf, static_cast<size_t>(n));
      if (resp.find("</methodResponse>") != std::string::npos) break;
    }
    CHECK(resp.find("200 OK") != std::string::npos);
    CHECK(resp.find("got:CQ DE N0CALL") != std::string::npos);
    CHECK_EQ(server.requestCount(), static_cast<uint64_t>(1));
    ::close(fd);
  }

  // ---------------- two programs at once. One keeps its connection open between
  // calls (xmlrpc-c, Python); the other opens one per call, speaks HTTP/1.0 and
  // reads until the server closes (CQRLOG). The second must not wait for the first.
  {
    auto request = [](const char* version, const char* method) {
      std::string body = std::string("<?xml version=\"1.0\"?><methodCall><methodName>") + method +
                         "</methodName><params></params></methodCall>";
      return std::string("POST /RPC2 ") + version + "\r\nContent-Type: text/xml\r\nContent-Length: " +
             std::to_string(body.size()) + "\r\n\r\n" + body;
    };
    // Reads until `until` shows up, the peer closes (sets *closed) or `ms` pass.
    auto readReply = [](int s, const char* until, int ms, bool* closed) {
      std::string resp;
      char buf[2048];
      auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
      if (closed) *closed = false;
      while (std::chrono::steady_clock::now() < end) {
        pollfd p{s, POLLIN, 0};
        if (::poll(&p, 1, 20) <= 0) continue;
        ssize_t n = ::recv(s, buf, sizeof buf, 0);
        if (n <= 0) { if (closed) *closed = true; break; }
        resp.append(buf, static_cast<size_t>(n));
        if (until && resp.find(until) != std::string::npos) break;
      }
      return resp;
    };
    int keep = connectLoopback(port);
    CHECK(keep >= 0);
    std::string r1 = request("HTTP/1.1", "main.get_frequency");
    CHECK(::send(keep, r1.data(), r1.size(), net::sendFlags()) == static_cast<ssize_t>(r1.size()));
    CHECK(readReply(keep, "</methodResponse>", 2000, nullptr).find("14080.5") != std::string::npos);
    // `keep` stays open and idle.

    auto t0 = std::chrono::steady_clock::now();
    int once = connectLoopback(port);
    CHECK(once >= 0);
    std::string r2 = request("HTTP/1.0", "main.get_frequency");
    CHECK(::send(once, r2.data(), r2.size(), net::sendFlags()) == static_cast<ssize_t>(r2.size()));
    bool closed = false;
    std::string resp = readReply(once, nullptr, 3000, &closed);   // until the server closes
    auto took = std::chrono::steady_clock::now() - t0;
    CHECK(resp.find("14080.5") != std::string::npos);
    CHECK(closed);                                   // HTTP/1.0: closed once answered
    CHECK(took < std::chrono::milliseconds(1000));   // not held up by the idle connection
    ::close(once);

    // The kept connection still works, also for two requests sent back to back.
    std::string two = r1 + r1;
    CHECK(::send(keep, two.data(), two.size(), net::sendFlags()) == static_cast<ssize_t>(two.size()));
    std::string both = readReply(keep, nullptr, 500, nullptr);
    size_t first = both.find("</methodResponse>");
    CHECK(first != std::string::npos && both.find("</methodResponse>", first + 1) != std::string::npos);
    ::close(keep);
    CHECK_EQ(server.requestCount(), static_cast<uint64_t>(5));
  }

  // -------------------------------- writing to a peer that went away must not
  // raise SIGPIPE: MSG_NOSIGNAL on Linux, SO_NOSIGPIPE on macOS/BSD. Without
  // the shim this kills the process and the test fails by not finishing.
  int dead = connectLoopback(port);
  CHECK(dead >= 0);
  server.stop();
  CHECK(!server.running());
  if (dead >= 0) {
    std::string junk(4096, 'x');
    for (int i = 0; i < 200; ++i) {
      ssize_t n = ::send(dead, junk.data(), junk.size(), net::sendFlags());
      if (n <= 0) break;  // EPIPE/ECONNRESET reported, not signaled: what we want
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ::close(dead);
  }
  std::printf("survived writing to a closed peer\n");

  TEST_MAIN_END();
}
