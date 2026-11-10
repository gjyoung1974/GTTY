// WSJT-X UDP protocol client (the interface GridTracker2, JTAlert, logging
// bridges etc. listen on). gtty reports status, decoded callsigns and logged
// QSOs, and accepts Reply / HaltTx / FreeText / Clear from the other side.
// Wire format: QDataStream big-endian, magic 0xadbccbda, schema 2.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gtty {

struct WsjtxStatus {
  uint64_t dialFreqHz = 0;
  std::string mode = "RTTY";
  std::string dxCall, report, txMode = "RTTY";
  bool txEnabled = false, transmitting = false, decoding = true;
  uint32_t rxDf = 0, txDf = 0;
  std::string deCall, deGrid, dxGrid;
  std::string configName = "gtty";
  std::string txMessage;
};

struct WsjtxDecode {
  uint32_t timeMsSinceMidnightUtc = 0;
  int32_t snr = 0;
  double deltaTime = 0;
  uint32_t deltaFreq = 0;
  std::string mode = "RTTY";
  std::string message;
  bool lowConfidence = false;
};

struct WsjtxQso {
  int64_t timeOffUnix = 0, timeOnUnix = 0;
  std::string dxCall, dxGrid;
  uint64_t txFreqHz = 0;
  std::string mode = "RTTY";
  std::string reportSent, reportRcvd, txPower, comments, name;
  std::string operatorCall, myCall, myGrid, exchSent, exchRcvd;
  std::string adif;  // full ADIF record for the LoggedADIF message
};

// Pure encoders / decoders (testable without sockets).
namespace wsjtx {
std::vector<uint8_t> encodeHeartbeat(const std::string& id, const std::string& version);
std::vector<uint8_t> encodeStatus(const std::string& id, const WsjtxStatus& s);
std::vector<uint8_t> encodeDecode(const std::string& id, const WsjtxDecode& d);
std::vector<uint8_t> encodeClear(const std::string& id);
std::vector<uint8_t> encodeQsoLogged(const std::string& id, const WsjtxQso& q);
std::vector<uint8_t> encodeLoggedAdif(const std::string& id, const std::string& adif);
std::vector<uint8_t> encodeClose(const std::string& id);

struct Incoming {
  enum class Type { Unknown, Clear, Reply, Close, HaltTx, FreeText };
  Type type = Type::Unknown;
  std::string id;
  // Reply
  WsjtxDecode reply;
  // HaltTx
  bool autoTxOnly = false;
  // FreeText
  std::string text;
  bool send = false;
};
bool decodeIncoming(const uint8_t* data, size_t n, Incoming& out);
uint32_t msSinceMidnightUtc(int64_t unixTime);
}  // namespace wsjtx

class WsjtxUdpClient {
 public:
  WsjtxUdpClient() = default;
  ~WsjtxUdpClient();
  WsjtxUdpClient(const WsjtxUdpClient&) = delete;
  WsjtxUdpClient& operator=(const WsjtxUdpClient&) = delete;

  bool open(const std::string& host, uint16_t port, const std::string& id, std::string& err);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  const std::string& id() const { return id_; }

  bool send(const std::vector<uint8_t>& datagram);
  // Non-blocking: dispatches any queued incoming datagrams to onIncoming.
  void poll();

  std::function<void(const wsjtx::Incoming&)> onIncoming;

 private:
  int fd_ = -1;
  std::string id_;
  std::vector<uint8_t> dest_;  // sockaddr storage
};

}  // namespace gtty
