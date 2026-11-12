#include "core/net/xmlrpc_server.h"

#include "core/net/socket_compat.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fcntl.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>

namespace gtty::xmlrpc {

// ----------------------------------------------------------------- helpers

namespace {

std::string xmlEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

std::string xmlUnescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') { o += s[i]; continue; }
    size_t semi = s.find(';', i);
    if (semi == std::string::npos) { o += s[i]; continue; }
    std::string ent = s.substr(i + 1, semi - i - 1);
    if (ent == "lt") o += '<';
    else if (ent == "gt") o += '>';
    else if (ent == "amp") o += '&';
    else if (ent == "quot") o += '"';
    else if (ent == "apos") o += '\'';
    else if (!ent.empty() && ent[0] == '#') {
      long cp = ent[1] == 'x' ? strtol(ent.c_str() + 2, nullptr, 16) : strtol(ent.c_str() + 1, nullptr, 10);
      if (cp < 0x80) o += static_cast<char>(cp);
      else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
      else { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    } else { o += s.substr(i, semi - i + 1); }
    i = semi;
  }
  return o;
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// A tiny forward-only XML cursor sufficient for XML-RPC payloads.
class Cursor {
 public:
  explicit Cursor(const std::string& s) : s_(s) {}
  void skipWs() { while (pos_ < s_.size() && isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
  bool atEnd() const { return pos_ >= s_.size(); }
  // If the next thing is <name ...>, consume it and return true.
  bool openTag(const std::string& name) {
    skipWs();
    skipComments();
    if (s_.compare(pos_, name.size() + 1, "<" + name) != 0) return false;
    size_t end = s_.find('>', pos_);
    if (end == std::string::npos) return false;
    char after = s_[pos_ + 1 + name.size()];
    if (after != '>' && after != ' ' && after != '/') return false;
    bool selfClosing = s_[end - 1] == '/';
    pos_ = end + 1;
    if (selfClosing) pendingClose_ = name;
    return true;
  }
  bool closeTag(const std::string& name) {
    if (pendingClose_ == name) { pendingClose_.clear(); return true; }
    skipWs();
    std::string t = "</" + name + ">";
    if (s_.compare(pos_, t.size(), t) != 0) return false;
    pos_ += t.size();
    return true;
  }
  bool peekTag(const std::string& name) {
    size_t save = pos_;
    bool ok = openTag(name);
    pos_ = save;
    pendingClose_.clear();
    return ok;
  }
  // Text up to the next '<'.
  std::string text() {
    size_t end = s_.find('<', pos_);
    if (end == std::string::npos) end = s_.size();
    std::string t = s_.substr(pos_, end - pos_);
    pos_ = end;
    return t;
  }
  void skipComments() {
    while (s_.compare(pos_, 4, "<!--") == 0) {
      size_t end = s_.find("-->", pos_);
      pos_ = end == std::string::npos ? s_.size() : end + 3;
      skipWs();
    }
    if (s_.compare(pos_, 2, "<?") == 0) {
      size_t end = s_.find("?>", pos_);
      pos_ = end == std::string::npos ? s_.size() : end + 2;
      skipWs();
    }
  }

 private:
  const std::string& s_;
  size_t pos_ = 0;
  std::string pendingClose_;
};

bool parseValue(Cursor& c, Value& out);

bool parseTyped(Cursor& c, Value& out) {
  if (c.openTag("string")) { out = Value::string(xmlUnescape(c.text())); return c.closeTag("string"); }
  if (c.openTag("int")) { out = Value::integer(strtoll(c.text().c_str(), nullptr, 10)); return c.closeTag("int"); }
  if (c.openTag("i4")) { out = Value::integer(strtoll(c.text().c_str(), nullptr, 10)); return c.closeTag("i4"); }
  if (c.openTag("i8")) { out = Value::integer(strtoll(c.text().c_str(), nullptr, 10)); return c.closeTag("i8"); }
  if (c.openTag("double")) { out = Value::number(strtod(c.text().c_str(), nullptr)); return c.closeTag("double"); }
  if (c.openTag("boolean")) { std::string t = c.text(); out = Value::boolean(t.find('1') != std::string::npos || t.find("true") != std::string::npos); return c.closeTag("boolean"); }
  if (c.openTag("base64")) { out = Value::base64(base64Decode(c.text())); return c.closeTag("base64"); }
  if (c.openTag("nil")) { out = Value::nil(); return c.closeTag("nil"); }
  if (c.openTag("dateTime.iso8601")) { out = Value::string(c.text()); return c.closeTag("dateTime.iso8601"); }
  if (c.openTag("array")) {
    out = Value::array({});
    if (!c.openTag("data")) return false;
    while (c.peekTag("value")) {
      Value v;
      if (!parseValue(c, v)) return false;
      out.arr.push_back(std::move(v));
    }
    return c.closeTag("data") && c.closeTag("array");
  }
  if (c.openTag("struct")) {
    out = Value::strct({});
    while (c.openTag("member")) {
      if (!c.openTag("name")) return false;
      std::string name = xmlUnescape(c.text());
      if (!c.closeTag("name")) return false;
      Value v;
      if (!parseValue(c, v)) return false;
      out.members.emplace_back(name, std::move(v));
      if (!c.closeTag("member")) return false;
    }
    return c.closeTag("struct");
  }
  // Untyped: plain string content.
  out = Value::string(xmlUnescape(c.text()));
  return true;
}

bool parseValue(Cursor& c, Value& out) {
  if (!c.openTag("value")) return false;
  if (!parseTyped(c, out)) return false;
  return c.closeTag("value");
}

}  // namespace

// ------------------------------------------------------------------ Value

int64_t Value::asInt() const {
  switch (type) {
    case Type::Int: return i;
    case Type::Double: return static_cast<int64_t>(d);
    case Type::Bool: return b ? 1 : 0;
    case Type::String: return strtoll(s.c_str(), nullptr, 10);
    default: return 0;
  }
}
double Value::asDouble() const {
  switch (type) {
    case Type::Int: return static_cast<double>(i);
    case Type::Double: return d;
    case Type::Bool: return b ? 1 : 0;
    case Type::String: return strtod(s.c_str(), nullptr);
    default: return 0;
  }
}
bool Value::asBool() const {
  switch (type) {
    case Type::Int: return i != 0;
    case Type::Double: return d != 0;
    case Type::Bool: return b;
    case Type::String: return s == "1" || s == "true" || s == "TRUE";
    default: return false;
  }
}
std::string Value::asString() const {
  switch (type) {
    case Type::Int: return std::to_string(i);
    case Type::Double: { char buf[64]; snprintf(buf, sizeof buf, "%g", d); return buf; }
    case Type::Bool: return b ? "1" : "0";
    case Type::String: case Type::Base64: return s;
    default: return "";
  }
}

// -------------------------------------------------------------- (de)serialize

std::string base64Encode(const std::string& raw) {
  std::string o;
  size_t i = 0;
  while (i + 2 < raw.size()) {
    uint32_t v = (static_cast<unsigned char>(raw[i]) << 16) | (static_cast<unsigned char>(raw[i + 1]) << 8) | static_cast<unsigned char>(raw[i + 2]);
    o += kB64[(v >> 18) & 63]; o += kB64[(v >> 12) & 63]; o += kB64[(v >> 6) & 63]; o += kB64[v & 63];
    i += 3;
  }
  if (i + 1 == raw.size()) {
    uint32_t v = static_cast<unsigned char>(raw[i]) << 16;
    o += kB64[(v >> 18) & 63]; o += kB64[(v >> 12) & 63]; o += "==";
  } else if (i + 2 == raw.size()) {
    uint32_t v = (static_cast<unsigned char>(raw[i]) << 16) | (static_cast<unsigned char>(raw[i + 1]) << 8);
    o += kB64[(v >> 18) & 63]; o += kB64[(v >> 12) & 63]; o += kB64[(v >> 6) & 63]; o += '=';
  }
  return o;
}

std::string base64Decode(const std::string& text) {
  std::string o;
  uint32_t acc = 0;
  int bits = 0;
  for (char c : text) {
    const char* p = strchr(kB64, c);
    if (!p || c == '\0') { if (c == '=') break; continue; }
    acc = (acc << 6) | static_cast<uint32_t>(p - kB64);
    bits += 6;
    if (bits >= 8) { bits -= 8; o += static_cast<char>((acc >> bits) & 0xFF); }
  }
  return o;
}

std::string serializeValue(const Value& v) {
  switch (v.type) {
    case Value::Type::Nil: return "<value><nil/></value>";
    case Value::Type::Int: return "<value><i4>" + std::to_string(v.i) + "</i4></value>";
    case Value::Type::Double: { char buf[64]; snprintf(buf, sizeof buf, "%.10g", v.d); return std::string("<value><double>") + buf + "</double></value>"; }
    case Value::Type::Bool: return std::string("<value><boolean>") + (v.b ? "1" : "0") + "</boolean></value>";
    // Untyped, as fldigi's XmlRpc++ writes strings. Some clients do not parse the reply but cut
    // the text between <value> and </value> (CQRLOG), or read the element's text and reject child
    // elements (QLog's fllog server), so a <string> wrapper would end up in their fields.
    case Value::Type::String: return "<value>" + xmlEscape(v.s) + "</value>";
    case Value::Type::Base64: return "<value><base64>" + base64Encode(v.s) + "</base64></value>";
    case Value::Type::Array: {
      std::string o = "<value><array><data>";
      for (const auto& e : v.arr) o += serializeValue(e);
      return o + "</data></array></value>";
    }
    case Value::Type::Struct: {
      std::string o = "<value><struct>";
      for (const auto& m : v.members) o += "<member><name>" + xmlEscape(m.first) + "</name>" + serializeValue(m.second) + "</member>";
      return o + "</struct></value>";
    }
  }
  return "<value><nil/></value>";
}

std::string serializeResponse(const Value& v) {
  return "<?xml version=\"1.0\"?>\n<methodResponse><params><param>" + serializeValue(v) + "</param></params></methodResponse>\n";
}

std::string serializeFault(int code, const std::string& message) {
  return "<?xml version=\"1.0\"?>\n<methodResponse><fault><value><struct>"
         "<member><name>faultCode</name><value><int>" + std::to_string(code) + "</int></value></member>"
         "<member><name>faultString</name><value><string>" + xmlEscape(message) + "</string></value></member>"
         "</struct></value></fault></methodResponse>\n";
}

std::string serializeRequest(const std::string& method, const std::vector<Value>& params) {
  std::string o = "<?xml version=\"1.0\"?>\n<methodCall><methodName>" + xmlEscape(method) + "</methodName><params>";
  for (const auto& p : params) o += "<param>" + serializeValue(p) + "</param>";
  return o + "</params></methodCall>\n";
}

bool parseRequest(const std::string& xml, std::string& method, std::vector<Value>& params, std::string& err) {
  Cursor c(xml);
  params.clear();
  if (!c.openTag("methodCall")) { err = "missing methodCall"; return false; }
  if (!c.openTag("methodName")) { err = "missing methodName"; return false; }
  method = xmlUnescape(c.text());
  while (!method.empty() && isspace(static_cast<unsigned char>(method.back()))) method.pop_back();
  if (!c.closeTag("methodName")) { err = "bad methodName"; return false; }
  if (c.openTag("params")) {
    while (c.openTag("param")) {
      Value v;
      if (!parseValue(c, v)) { err = "bad param value"; return false; }
      params.push_back(std::move(v));
      if (!c.closeTag("param")) { err = "bad param"; return false; }
    }
    if (!c.closeTag("params")) { err = "bad params"; return false; }
  }
  return true;
}

// ------------------------------------------------------------------ Server

Server::~Server() { stop(); }

bool Server::start(const std::string& bindAddr, uint16_t port, std::string& err) {
  stop();
  fd_ = net::socketFd(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) { err = strerror(errno); return false; }
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
    err = std::string("bind ") + bindAddr + ":" + std::to_string(port) + ": " + strerror(errno);
    ::close(fd_); fd_ = -1; return false;
  }
  if (::listen(fd_, 8) < 0) { err = strerror(errno); ::close(fd_); fd_ = -1; return false; }
  run_ = true;
  thread_ = std::thread([this] { loop(); });
  return true;
}

void Server::stop() {
  run_ = false;
  if (thread_.joinable()) thread_.join();   // the loop looks at run_ at least every 200 ms
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

namespace {

struct Conn {
  int fd = -1;
  std::string in, out;
  std::chrono::steady_clock::time_point lastActive;
  bool closeAfterWrite = false;
  bool dead = false;
};

constexpr size_t kMaxConns = 32;
constexpr size_t kMaxRequest = 1u << 20;
constexpr auto kIdle = std::chrono::seconds(60);   // a kept-alive connection nobody uses

void flushConn(Conn& c) {
  while (!c.out.empty()) {
    ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), net::sendFlags());
    if (n > 0) { c.out.erase(0, static_cast<size_t>(n)); continue; }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    c.dead = true;
    return;
  }
  if (c.closeAfterWrite) c.dead = true;
}

}  // namespace

// One thread, many connections. Loggers differ: CQRLOG opens a connection per
// call and reads until it is closed, xmlrpc-c and Python keep one open between
// calls. Every connection is served as its requests arrive, so a program that
// holds its connection open does not keep the others waiting.
void Server::loop() {
  std::vector<Conn> conns;
  while (run_) {
    std::vector<pollfd> pfds;
    pfds.push_back({fd_, POLLIN, 0});
    for (const auto& c : conns) pfds.push_back({c.fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
    int r = ::poll(pfds.data(), pfds.size(), 200);
    if (r < 0 && errno != EINTR) break;
    auto now = std::chrono::steady_clock::now();

    if (r > 0 && (pfds[0].revents & POLLIN)) {
      int client = ::accept(fd_, nullptr, nullptr);
      if (client >= 0) {
        if (conns.size() >= kMaxConns) {
          ::close(client);
        } else {
          net::noSigPipe(client);
          ::fcntl(client, F_SETFD, FD_CLOEXEC);
          ::fcntl(client, F_SETFL, ::fcntl(client, F_GETFL) | O_NONBLOCK);
          Conn c;
          c.fd = client;
          c.lastActive = now;
          conns.push_back(std::move(c));
        }
      }
    }

    for (size_t i = 0; i < conns.size() && i + 1 < pfds.size(); ++i) {
      Conn& c = conns[i];
      short ev = r > 0 && pfds[i + 1].fd == c.fd ? pfds[i + 1].revents : 0;
      if (ev & POLLIN) {
        char tmp[4096];
        ssize_t n = ::recv(c.fd, tmp, sizeof tmp, 0);
        if (n > 0) {
          c.in.append(tmp, static_cast<size_t>(n));
          c.lastActive = now;
          if (c.in.size() > kMaxRequest) c.dead = true;
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
          c.dead = true;
        }
      } else if (ev & (POLLERR | POLLHUP | POLLNVAL)) {
        c.dead = true;
      }
      while (!c.dead && !c.closeAfterWrite && serveOne(c.in, c.out, c.closeAfterWrite)) {}
      if (!c.dead) flushConn(c);
      if (!c.dead && now - c.lastActive > kIdle) c.dead = true;
    }
    for (auto it = conns.begin(); it != conns.end();) {
      if (it->dead) { ::close(it->fd); it = conns.erase(it); }
      else ++it;
    }
  }
  for (auto& c : conns) ::close(c.fd);
}

// Takes one complete HTTP request off the front of `in`, if there is one, and
// appends the reply to `out`. `close` is set when the connection is to be closed
// once the reply has gone out (HTTP/1.0, or "Connection: close").
bool Server::serveOne(std::string& in, std::string& out, bool& close) {
  size_t hdrEnd = in.find("\r\n\r\n");
  if (hdrEnd == std::string::npos) return false;
  std::string headers = in.substr(0, hdrEnd);
  std::string lower;
  for (char ch : headers) lower += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
  size_t contentLength = 0;
  size_t cl = lower.find("content-length:");
  if (cl != std::string::npos) contentLength = strtoul(lower.c_str() + cl + 15, nullptr, 10);
  size_t bodyStart = hdrEnd + 4;
  if (contentLength > kMaxRequest) { in.clear(); close = true; return false; }
  if (in.size() < bodyStart + contentLength) return false;
  std::string body = in.substr(bodyStart, contentLength);
  in.erase(0, bodyStart + contentLength);
  requests_++;

  std::string method, err, response;
  std::vector<Value> params;
  if (!parseRequest(body, method, params, err)) {
    response = serializeFault(-32700, "parse error: " + err);
  } else if (!handler_) {
    response = serializeFault(-32601, "no handler");
  } else {
    try {
      response = serializeResponse(handler_(method, params));
    } catch (const std::exception& e) {
      response = serializeFault(-32500, e.what());
    }
  }
  // Keep the connection only for HTTP/1.1 clients that did not ask to close; an
  // HTTP/1.0 client such as CQRLOG reads until the connection is closed.
  bool keepAlive = headers.find("HTTP/1.1") != std::string::npos && lower.find("connection: close") == std::string::npos;
  out += "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(response.size()) +
         "\r\nServer: gtty\r\n" + (keepAlive ? "" : "Connection: close\r\n") + "\r\n" + response;
  if (!keepAlive) close = true;
  return true;
}

}  // namespace gtty::xmlrpc
