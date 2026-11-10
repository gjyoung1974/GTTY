// True-FSK keying of the rig's FSK input through a serial port:
//   Uart  - the UART itself sends 5N2 at the RTTY baud rate on TXD (custom
//           baud via termios2 BOTHER on Linux, the IOSSIOSPEED ioctl on macOS;
//           works with FTDI/CP210x/PL2303 and native ports on Linux, and with
//           drivers that accept the rate on macOS). Lowest jitter.
//   Dtr / Rts - bit-banged handshake line with a timed thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "core/ring_buffer.h"
#include "core/rtty_params.h"

namespace gtty {

class FskKeyer {
 public:
  enum class Method { Uart, Dtr, Rts };

  FskKeyer() = default;
  ~FskKeyer();
  FskKeyer(const FskKeyer&) = delete;
  FskKeyer& operator=(const FskKeyer&) = delete;

  // invert: swap mark/space line polarity (bit-bang only; the UART polarity
  // is fixed by RS-232: idle/mark = negative).
  bool open(const std::string& device, Method m, const RttyParams& p, bool invert, std::string& err);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  void setParams(const RttyParams& p);

  // Keying session: while active and the queue is empty, LTRS diddles are sent
  // (if enabled) so the signal keeps its bit clock.
  void start();
  void stop();           // finish queued characters, then idle
  bool active() const { return active_.load(); }
  void setDiddle(bool on) { diddle_.store(on); }

  bool sendCode(uint8_t code) { return queue_.push(&code, 1) == 1; }
  size_t pending() const { return queue_.size(); }
  bool busy() const { return busy_.load(); }
  void clearQueue() { queue_.clear(); }

  std::function<void(uint8_t)> onCodeSent;  // from the keyer thread

 private:
  void uartLoop();
  void bitbangLoop();
  void setLine(bool mark);
  bool configureUart(std::string& err);

  int fd_ = -1;
  Method method_ = Method::Uart;
  RttyParams params_;
  bool invert_ = false;
  std::thread thread_;
  std::atomic<bool> run_{false};
  std::atomic<bool> active_{false};
  std::atomic<bool> busy_{false};
  std::atomic<bool> diddle_{true};
  std::atomic<int> paramsVersion_{0};
  SpscRing<uint8_t> queue_{1 << 12};
};

}  // namespace gtty
