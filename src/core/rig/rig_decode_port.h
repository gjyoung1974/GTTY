// The radio's own RTTY decoder, read from a serial port.
//
// Some radios print what their built-in decoder reads as plain characters on a
// serial port: the Icom IC-7610 and IC-9700 on the second of their two USB
// serial ports ("USB (B) Function: RTTY Decode"), the IC-7300 on its single
// port in place of CI-V. No CAT is involved, so this stands apart from Hamlib:
// the port is opened here and only read.
//
// The port may be the one whose DTR or RTS line keys FSK or PTT. Opening a
// serial port raises both lines, so open() lets go of them at once; open this
// port before the keyer and the PTT port, which then set their lines as they
// need them.
#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>

namespace gtty {

class RigDecodePort {
 public:
  RigDecodePort() = default;
  ~RigDecodePort();
  RigDecodePort(const RigDecodePort&) = delete;
  RigDecodePort& operator=(const RigDecodePort&) = delete;

  // The serial speeds that can be asked for (the radio's "Decode Baud Rate").
  static bool supportedBaud(int baud);

  // 8N1, no flow control. Starts the reader thread.
  bool open(const std::string& device, int baud, std::string& err);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  const std::string& device() const { return device_; }

  // While muted, what the radio sends is read and dropped (our own transmission).
  void setMuted(bool m) { muted_.store(m); }

  // What to print of the bytes the radio sent: printable ASCII, CR and LF.
  static std::string clean(const char* data, size_t n);

  // Both are called from the reader thread. onError is called once, when the port
  // fails or goes away (the radio switched off, the cable pulled); reading then stops.
  std::function<void(const std::string& text)> onText;
  std::function<void(const std::string& message)> onError;

 private:
  void loop();

  int fd_ = -1;
  std::string device_;
  std::thread thread_;
  std::atomic<bool> run_{false};
  std::atomic<bool> muted_{false};
};

}  // namespace gtty
