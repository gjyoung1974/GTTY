#include "core/net/xmlrpc_server.h"

#include "core/net/socket_compat.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

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

// -------------------------------------------------------------- (de)serialise

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
    case Value::Type::String: return "<value><string>" + xmlEscape(v.s) + "</string></value>";
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
  if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; }
  if (thread_.joinable()) thread_.join();
}

void Server::loop() {
  while (run_) {
    pollfd p{fd_, POLLIN, 0};
    int r = ::poll(&p, 1, 200);
    if (r <= 0) continue;
    int client = ::accept(fd_, nullptr, nullptr);
    if (client < 0) continue;
    serve(client);
    ::close(client);
  }
}

void Server::serve(int client) {
  net::noSigPipe(client);
  timeval tv{5, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  // Some clients (xmlrpc-c) reuse the connection: serve requests until it closes.
  std::string buf;
  char tmp[4096];
  for (;;) {
    size_t hdrEnd;
    while ((hdrEnd = buf.find("\r\n\r\n")) == std::string::npos) {
      ssize_t n = ::recv(client, tmp, sizeof tmp, 0);
      if (n <= 0) return;
      buf.append(tmp, static_cast<size_t>(n));
      if (buf.size() > (1u << 20)) return;
    }
    std::string headers = buf.substr(0, hdrEnd);
    size_t contentLength = 0;
    {
      std::string lower;
      for (char ch : headers) lower += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
      size_t cl = lower.find("content-length:");
      if (cl != std::string::npos) contentLength = strtoul(lower.c_str() + cl + 15, nullptr, 10);
    }
    size_t bodyStart = hdrEnd + 4;
    while (buf.size() < bodyStart + contentLength) {
      ssize_t n = ::recv(client, tmp, sizeof tmp, 0);
      if (n <= 0) return;
      buf.append(tmp, static_cast<size_t>(n));
    }
    std::string body = buf.substr(bodyStart, contentLength);
    buf.erase(0, bodyStart + contentLength);
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
    std::string http = "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " +
                       std::to_string(response.size()) + "\r\nServer: gtty\r\n\r\n" + response;
    size_t off = 0;
    while (off < http.size()) {
      ssize_t n = ::send(client, http.data() + off, http.size() - off, net::sendFlags());
      if (n <= 0) return;
      off += static_cast<size_t>(n);
    }
    bool keepAlive = headers.find("HTTP/1.1") != std::string::npos && headers.find("Connection: close") == std::string::npos;
    if (!keepAlive) return;
  }
}

}  // namespace gtty::xmlrpc
