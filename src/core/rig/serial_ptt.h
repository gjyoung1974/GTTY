// PTT on a serial port's DTR or RTS line, keyed by gtty itself.
//
// Used when the PTT line is on a port of its own, not the rig's CAT port.
// Hamlib can key such a line too, but only as part of an open rig, and not
// with every backend: NET rigctl takes its PTT method from the rigctld it
// connects to and drops the one configured here.
#pragma once
#include <string>

namespace gtty {

class SerialPtt {
 public:
  enum class Line { Dtr, Rts };

  SerialPtt() = default;
  ~SerialPtt();
  SerialPtt(const SerialPtt&) = delete;
  SerialPtt& operator=(const SerialPtt&) = delete;

  // Opens the port and releases both lines (the kernel raises them on open).
  bool open(const std::string& device, Line line, std::string& err);
  void close();   // releases the line first
  bool isOpen() const { return fd_ >= 0; }
  const std::string& device() const { return device_; }

  bool set(bool on, std::string* err = nullptr);
  // The line's state as the driver reports it; false when that cannot be read.
  bool get() const;

 private:
  int fd_ = -1;
  int bit_ = 0;
  std::string device_;
};

}  // namespace gtty
