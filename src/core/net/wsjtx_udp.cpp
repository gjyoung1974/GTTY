#include "core/net/wsjtx_udp.h"

#include "core/net/socket_compat.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <ctime>

namespace gtty {

namespace {
constexpr uint32_t kMagic = 0xadbccbda;
constexpr uint32_t kSchema = 2;

enum MsgType : uint32_t {
  Heartbeat = 0, Status = 1, Decode = 2, Clear = 3, Reply = 4, QsoLogged = 5, Close = 6,
  Replay = 7, HaltTx = 8, FreeText = 9, WsprDecode = 10, Location = 11, LoggedAdif = 12
};

class Writer {
 public:
  std::vector<uint8_t> buf;
  void u8(uint8_t v) { buf.push_back(v); }
  void u32(uint32_t v) { for (int i = 3; i >= 0; --i) buf.push_back(static_cast<uint8_t>(v >> (8 * i))); }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void u64(uint64_t v) { for (int i = 7; i >= 0; --i) buf.push_back(static_cast<uint8_t>(v >> (8 * i))); }
  void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }
  void boolean(bool v) { u8(v ? 1 : 0); }
  void f64(double v) { uint64_t bits; std::memcpy(&bits, &v, 8); u64(bits); }
  void utf8(const std::string& s) { u32(static_cast<uint32_t>(s.size())); buf.insert(buf.end(), s.begin(), s.end()); }
  void header(uint32_t type, const std::string& id) { u32(kMagic); u32(kSchema); u32(type); utf8(id); }
  // QDateTime: julian day (i64), ms since midnight (u32), timespec (u8: 1 = UTC)
  void dateTime(int64_t unix) {
    int64_t days = unix >= 0 ? unix / 86400 : (unix - 86399) / 86400;
    i64(days + 2440588);
    u32(wsjtx::msSinceMidnightUtc(unix));
    u8(1);
  }
};

class Reader {
 public:
  Reader(const uint8_t* d, size_t n) : p_(d), end_(d + n) {}
  bool ok() const { return ok_; }
  uint8_t u8() { if (!need(1)) return 0; return *p_++; }
  uint32_t u32() { if (!need(4)) return 0; uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | *p_++; return v; }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  uint64_t u64() { if (!need(8)) return 0; uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | *p_++; return v; }
  double f64() { uint64_t b = u64(); double v; std::memcpy(&v, &b, 8); return v; }
  bool boolean() { return u8() != 0; }
  std::string utf8() {
    uint32_t n = u32();
    if (n == 0xffffffffu) return "";  // null string
    if (!need(n)) return "";
    std::string s(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    return s;
  }
 private:
  bool need(size_t n) { if (!ok_ || static_cast<size_t>(end_ - p_) < n) { ok_ = false; return false; } return true; }
  const uint8_t* p_;
  const uint8_t* end_;
  bool ok_ = true;
};
}  // namespace

namespace wsjtx {

uint32_t msSinceMidnightUtc(int64_t unixTime) {
  int64_t s = ((unixTime % 86400) + 86400) % 86400;
  return static_cast<uint32_t>(s * 1000);
}

std::vector<uint8_t> encodeHeartbeat(const std::string& id, const std::string& version) {
  Writer w;
  w.header(Heartbeat, id);
  w.u32(3);           // max schema we understand
  w.utf8(version);
  w.utf8("");         // revision
  return w.buf;
}

std::vector<uint8_t> encodeStatus(const std::string& id, const WsjtxStatus& s) {
  Writer w;
  w.header(Status, id);
  w.u64(s.dialFreqHz);
  w.utf8(s.mode);
  w.utf8(s.dxCall);
  w.utf8(s.report);
  w.utf8(s.txMode);
  w.boolean(s.txEnabled);
  w.boolean(s.transmitting);
  w.boolean(s.decoding);
  w.u32(s.rxDf);
  w.u32(s.txDf);
  w.utf8(s.deCall);
  w.utf8(s.deGrid);
  w.utf8(s.dxGrid);
  w.boolean(false);   // tx watchdog
  w.utf8("");         // sub mode
  w.boolean(false);   // fast mode
  w.u8(0);            // special operation mode
  w.u32(0);           // frequency tolerance
  w.u32(0);           // T/R period
  w.utf8(s.configName);
  w.utf8(s.txMessage);
  return w.buf;
}

std::vector<uint8_t> encodeDecode(const std::string& id, const WsjtxDecode& d) {
  Writer w;
  w.header(Decode, id);
  w.boolean(true);    // new
  w.u32(d.timeMsSinceMidnightUtc);
  w.i32(d.snr);
  w.f64(d.deltaTime);
  w.u32(d.deltaFreq);
  w.utf8(d.mode);
  w.utf8(d.message);
  w.boolean(d.lowConfidence);
  w.boolean(false);   // off air
  return w.buf;
}

std::vector<uint8_t> encodeClear(const std::string& id) {
  Writer w;
  w.header(Clear, id);
  w.u8(0);  // band activity window
  return w.buf;
}

std::vector<uint8_t> encodeQsoLogged(const std::string& id, const WsjtxQso& q) {
  Writer w;
  w.header(QsoLogged, id);
  w.dateTime(q.timeOffUnix);
  w.utf8(q.dxCall);
  w.utf8(q.dxGrid);
  w.u64(q.txFreqHz);
  w.utf8(q.mode);
  w.utf8(q.reportSent);
  w.utf8(q.reportRcvd);
  w.utf8(q.txPower);
  w.utf8(q.comments);
  w.utf8(q.name);
  w.dateTime(q.timeOnUnix);
  w.utf8(q.operatorCall);
  w.utf8(q.myCall);
  w.utf8(q.myGrid);
  w.utf8(q.exchSent);
  w.utf8(q.exchRcvd);
  w.utf8("");  // ADIF propagation mode
  return w.buf;
}

std::vector<uint8_t> encodeLoggedAdif(const std::string& id, const std::string& adif) {
  Writer w;
  w.header(LoggedAdif, id);
  w.utf8(adif);
  return w.buf;
}

std::vector<uint8_t> encodeClose(const std::string& id) {
  Writer w;
  w.header(Close, id);
  return w.buf;
}

bool decodeIncoming(const uint8_t* data, size_t n, Incoming& out) {
  Reader r(data, n);
  if (r.u32() != kMagic) return false;
  uint32_t schema = r.u32();
  if (schema < 2) return false;
  uint32_t type = r.u32();
  out.id = r.utf8();
  switch (type) {
    case Clear: out.type = Incoming::Type::Clear; break;
    case Close: out.type = Incoming::Type::Close; break;
    case Reply:
      out.type = Incoming::Type::Reply;
      out.reply.timeMsSinceMidnightUtc = r.u32();
      out.reply.snr = r.i32();
      out.reply.deltaTime = r.f64();
      out.reply.deltaFreq = r.u32();
      out.reply.mode = r.utf8();
      out.reply.message = r.utf8();
      out.reply.lowConfidence = r.boolean();
      r.u8();  // modifiers
      break;
    case HaltTx:
      out.type = Incoming::Type::HaltTx;
      out.autoTxOnly = r.boolean();
      break;
    case FreeText:
      out.type = Incoming::Type::FreeText;
      out.text = r.utf8();
      out.send = r.boolean();
      break;
    default: out.type = Incoming::Type::Unknown; break;
  }
  return r.ok();
}

}  // namespace wsjtx

WsjtxUdpClient::~WsjtxUdpClient() { close(); }

bool WsjtxUdpClient::open(const std::string& host, uint16_t port, const std::string& id, std::string& err) {
  close();
  id_ = id;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
  if (rc != 0 || !res) { err = std::string("resolve ") + host + ": " + gai_strerror(rc); return false; }
  fd_ = net::socketFd(res->ai_family, SOCK_DGRAM, 0, /*nonblock=*/true);
  if (fd_ < 0) { err = strerror(errno); freeaddrinfo(res); return false; }
  dest_.assign(reinterpret_cast<uint8_t*>(res->ai_addr), reinterpret_cast<uint8_t*>(res->ai_addr) + res->ai_addrlen);
  // Multicast destinations (GridTracker can listen on 224.0.0.x): set TTL 1.
  if (res->ai_family == AF_INET) {
    auto* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
    if ((ntohl(sin->sin_addr.s_addr) >> 28) == 0xE) {
      unsigned char ttl = 1;
      setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    }
  }
  freeaddrinfo(res);
  return true;
}

void WsjtxUdpClient::close() {
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool WsjtxUdpClient::send(const std::vector<uint8_t>& datagram) {
  if (fd_ < 0) return false;
  ssize_t n = ::sendto(fd_, datagram.data(), datagram.size(), 0,
                       reinterpret_cast<const sockaddr*>(dest_.data()), static_cast<socklen_t>(dest_.size()));
  return n == static_cast<ssize_t>(datagram.size());
}

void WsjtxUdpClient::poll() {
  if (fd_ < 0) return;
  uint8_t buf[65536];
  for (int i = 0; i < 32; ++i) {
    ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
    if (n <= 0) break;
    wsjtx::Incoming in;
    if (wsjtx::decodeIncoming(buf, static_cast<size_t>(n), in) && onIncoming) onIncoming(in);
  }
}

}  // namespace gtty
