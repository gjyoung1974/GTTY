#include "core/rig/rig_decode_port.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace gtty {

namespace {

bool speedFor(int baud, speed_t& out) {
  switch (baud) {
    case 1200: out = B1200; return true;
    case 2400: out = B2400; return true;
    case 4800: out = B4800; return true;
    case 9600: out = B9600; return true;
    case 19200: out = B19200; return true;
    case 38400: out = B38400; return true;
    case 57600: out = B57600; return true;
    case 115200: out = B115200; return true;
    default: return false;
  }
}

}  // namespace

RigDecodePort::~RigDecodePort() { close(); }

bool RigDecodePort::supportedBaud(int baud) {
  speed_t s;
  return speedFor(baud, s);
}

std::string RigDecodePort::clean(const char* data, size_t n) {
  std::string out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = static_cast<unsigned char>(data[i]);
    if ((c >= 0x20 && c < 0x7f) || c == '\r' || c == '\n') out.push_back(static_cast<char>(c));
  }
  return out;
}

bool RigDecodePort::open(const std::string& device, int baud, std::string& err) {
  close();
  speed_t speed;
  if (!speedFor(baud, speed)) { err = "unsupported serial speed " + std::to_string(baud); return false; }
  // O_NONBLOCK: do not wait for carrier detect on a port without CLOCAL.
  int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) { err = device + ": " + std::strerror(errno); return false; }
  // Opening raised DTR and RTS, and one of them may key the transmitter. Not every
  // device has the lines (a pseudo-terminal does not), and reading works without them.
  int both = TIOCM_DTR | TIOCM_RTS;
  ioctl(fd, TIOCMBIC, &both);
  struct termios tio{};
  if (tcgetattr(fd, &tio) < 0) {
    err = device + ": not a serial port (" + std::strerror(errno) + ")";
    ::close(fd);
    return false;
  }
  cfmakeraw(&tio);
  tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS | HUPCL);
  tio.c_cflag |= CS8 | CREAD | CLOCAL;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  cfsetispeed(&tio, speed);
  cfsetospeed(&tio, speed);
  if (tcsetattr(fd, TCSANOW, &tio) < 0) {
    err = device + ": cannot set " + std::to_string(baud) + " baud (" + std::strerror(errno) + ")";
    ::close(fd);
    return false;
  }
  tcflush(fd, TCIFLUSH);   // whatever the radio sent before we listened
  fd_ = fd;
  device_ = device;
  run_ = true;
  thread_ = std::thread([this] { loop(); });
  return true;
}

void RigDecodePort::close() {
  run_ = false;
  if (thread_.joinable()) thread_.join();
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  device_.clear();
}

void RigDecodePort::loop() {
  char buf[256];
  auto fail = [this](const std::string& why) {
    if (onError) onError(device_ + ": " + why);
  };
  while (run_) {
    struct pollfd p{fd_, POLLIN, 0};
    int r = ::poll(&p, 1, 100);
    if (r < 0) {
      if (errno == EINTR) continue;
      fail(std::strerror(errno));
      return;
    }
    if (r == 0) continue;
    if (p.revents & POLLIN) {
      ssize_t n = ::read(fd_, buf, sizeof buf);
      if (n > 0) {
        if (muted_) continue;
        std::string text = clean(buf, static_cast<size_t>(n));
        if (!text.empty() && onText) onText(text);
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      fail(n == 0 ? "the port went away" : std::strerror(errno));
      return;
    }
    if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) {
      fail("the port went away");
      return;
    }
  }
}

}  // namespace gtty
