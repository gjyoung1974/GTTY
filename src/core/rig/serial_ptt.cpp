#include "core/rig/serial_ptt.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace gtty {

SerialPtt::~SerialPtt() { close(); }

bool SerialPtt::open(const std::string& device, Line line, std::string& err) {
  close();
  // O_NONBLOCK: do not wait for carrier detect on a port without CLOCAL.
  int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) { err = device + ": " + std::strerror(errno); return false; }
  // Opening a serial port asserts DTR and RTS; let go of both at once so the
  // transmitter is keyed for no longer than it takes to get here.
  int both = TIOCM_DTR | TIOCM_RTS;
  if (ioctl(fd, TIOCMBIC, &both) < 0) {
    err = device + ": cannot set the control lines (" + std::strerror(errno) + ")";
    ::close(fd);
    return false;
  }
  fd_ = fd;
  bit_ = line == Line::Rts ? TIOCM_RTS : TIOCM_DTR;
  device_ = device;
  return true;
}

void SerialPtt::close() {
  if (fd_ < 0) return;
  set(false);
  ::close(fd_);
  fd_ = -1;
  device_.clear();
}

bool SerialPtt::set(bool on, std::string* err) {
  if (fd_ < 0) { if (err) *err = "PTT port not open"; return false; }
  // One bit at a time: the other line may be keying FSK from the same port.
  int bit = bit_;
  if (ioctl(fd_, on ? TIOCMBIS : TIOCMBIC, &bit) < 0) {
    if (err) *err = device_ + ": " + std::strerror(errno);
    return false;
  }
  return true;
}

bool SerialPtt::get() const {
  int bits = 0;
  if (fd_ < 0 || ioctl(fd_, TIOCMGET, &bits) < 0) return false;
  return (bits & bit_) != 0;
}

}  // namespace gtty
