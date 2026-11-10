#include "core/rig/fsk_keyer.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__linux__)
#include <asm/termbits.h>  // struct termios2, BOTHER (do not include <termios.h> here)
#else
#include <termios.h>
#endif
#if defined(__APPLE__)
#include <IOKit/serial/ioss.h>  // IOSSIOSPEED: non-POSIX baud rates (header-only)
#endif

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>

namespace gtty {

namespace {

// Bytes still waiting in the kernel's transmit queue, or -1 where the platform
// cannot report it. Linux gives a true count for every serial driver; macOS
// defines TIOCOUTQ but its USB serial drivers do not keep a meaningful queue
// depth, so there the keyer paces with tcdrain() instead.
int outputQueued(int fd) {
#if defined(__linux__) && defined(TIOCOUTQ)
  int outq = 0;
  if (ioctl(fd, TIOCOUTQ, &outq) < 0) return -1;
  return outq;
#else
  (void)fd;
  return -1;
#endif
}

// Block until the port has sent everything queued. Only used where
// outputQueued() cannot report, so on Linux it is unreachable -- and tcdrain()
// is not even declared there, since <termios.h> may not be included alongside
// <asm/termbits.h>.
void drainOutput(int fd) {
#if defined(__linux__)
  (void)fd;
#else
  ::tcdrain(fd);
#endif
}

}  // namespace

FskKeyer::~FskKeyer() { close(); }

bool FskKeyer::open(const std::string& device, Method m, const RttyParams& p, bool invert,
                    std::string& err) {
  close();
  method_ = m;
  params_ = p;
  invert_ = invert;
  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) { err = std::string("open ") + device + ": " + strerror(errno); return false; }
  int flags = fcntl(fd_, F_GETFL);
  fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
  if (method_ == Method::Uart) {
    if (!configureUart(err)) { ::close(fd_); fd_ = -1; return false; }
  } else {
    setLine(true);  // idle at mark
  }
  run_ = true;
  thread_ = std::thread([this] {
    if (method_ == Method::Uart) uartLoop(); else bitbangLoop();
  });
  return true;
}

#if defined(__linux__)

// Linux: termios2 carries an arbitrary integer baud rate in c_ispeed/c_ospeed
// once BOTHER replaces the rate constant in c_cflag.
bool FskKeyer::configureUart(std::string& err) {
  struct termios2 tio{};
  if (ioctl(fd_, TCGETS2, &tio) < 0) { err = std::string("TCGETS2: ") + strerror(errno); return false; }
  tio.c_cflag &= ~(CBAUD | CSIZE | PARENB | CRTSCTS);
  tio.c_cflag |= BOTHER | CS5 | CSTOPB | CREAD | CLOCAL;  // 5 data bits, 2 stop bits, no parity
  tio.c_iflag = 0;
  tio.c_oflag = 0;
  tio.c_lflag = 0;
  int speed = static_cast<int>(std::lround(params_.baud));
  tio.c_ispeed = speed;
  tio.c_ospeed = speed;
  if (ioctl(fd_, TCSETS2, &tio) < 0) { err = std::string("TCSETS2: ") + strerror(errno); return false; }
  return true;
}

#else

// macOS/BSD: set the frame format with POSIX termios, then the non-standard
// baud rate with IOSSIOSPEED, which must come after tcsetattr() and which the
// driver may refuse for a rate as low as 45 baud (FTDI accepts it; some CP210x
// and CDC-ACM drivers do not, in which case use FSK via DTR/RTS or AFSK).
bool FskKeyer::configureUart(std::string& err) {
  struct termios tio{};
  if (tcgetattr(fd_, &tio) < 0) { err = std::string("tcgetattr: ") + strerror(errno); return false; }
  cfmakeraw(&tio);
  tio.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
  tio.c_cflag |= CS5 | CSTOPB | CREAD | CLOCAL;  // 5 data bits, 2 stop bits, no parity
  if (tcsetattr(fd_, TCSANOW, &tio) < 0) { err = std::string("tcsetattr: ") + strerror(errno); return false; }
#if defined(IOSSIOSPEED)
  speed_t speed = static_cast<speed_t>(std::lround(params_.baud));
  if (ioctl(fd_, IOSSIOSPEED, &speed) < 0) {
    err = "IOSSIOSPEED " + std::to_string(speed) + " baud: " + strerror(errno) +
          " (this serial driver will not take the RTTY baud rate; use FSK via DTR/RTS or AFSK)";
    return false;
  }
#else
  err = "custom baud rates are not supported on this platform";
  return false;
#endif
  return true;
}

#endif

void FskKeyer::setParams(const RttyParams& p) {
  params_ = p;
  paramsVersion_.fetch_add(1);
}

void FskKeyer::close() {
  run_ = false;
  active_ = false;
  if (thread_.joinable()) thread_.join();
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

void FskKeyer::start() { active_ = true; }
void FskKeyer::stop() { active_ = false; }

void FskKeyer::setLine(bool mark) {
  if (fd_ < 0) return;
  bool asserted = invert_ ? mark : !mark;  // default: mark = line negative/de-asserted (like TXD idle)
  int flag = method_ == Method::Rts ? TIOCM_RTS : TIOCM_DTR;
  ioctl(fd_, asserted ? TIOCMBIS : TIOCMBIC, &flag);
}

void FskKeyer::uartLoop() {
  int seenVersion = paramsVersion_.load();
  while (run_) {
    if (paramsVersion_.load() != seenVersion) {
      seenVersion = paramsVersion_.load();
      std::string err;
      configureUart(err);
    }
    uint8_t code;
    bool have = queue_.pop(&code, 1) == 1;
    bool idleFill = false;
    if (!have && active_ && diddle_) { code = 0x1f; have = true; idleFill = true; }
    if (!have) {
      // Wait for the UART to finish anything in flight. Where no queue depth is
      // available (macOS) each write below has already drained, so nothing is
      // pending and outputQueued()'s -1 correctly reads as "not busy".
      int outq = outputQueued(fd_);
      busy_ = outq > 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    busy_ = !idleFill;  // idle diddles do not count as pending work
    code &= 0x1f;
    if (::write(fd_, &code, 1) == 1 && onCodeSent) onCodeSent(code);
    // Pace: wait until at most one byte remains queued so echo and the
    // end-of-transmission detection track the actual keying.
    if (outputQueued(fd_) < 0) {
      drainOutput(fd_);  // no queue depth to poll: block until the character is out
    } else {
      for (;;) {
        int outq = outputQueued(fd_);
        if (outq < 0 || outq <= 1 || !run_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
  }
}

void FskKeyer::bitbangLoop() {
  auto next = std::chrono::steady_clock::now();
  while (run_) {
    uint8_t code;
    bool have = queue_.pop(&code, 1) == 1;
    bool idleFill = false;
    if (!have && active_ && diddle_) { code = 0x1f; have = true; idleFill = true; }
    if (!have) {
      busy_ = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      next = std::chrono::steady_clock::now();
      continue;
    }
    busy_ = !idleFill;  // idle diddles do not count as pending work
    if (onCodeSent) onCodeSent(code & 0x1f);
    const double bitNs = 1e9 / params_.baud;
    // Absolute deadlines, so the sleeps cannot accumulate drift over a transmission.
    auto sendBit = [&](bool mark, double lengthBits) {
      setLine(mark);
      next += std::chrono::nanoseconds(static_cast<long long>(bitNs * lengthBits));
      std::this_thread::sleep_until(next);
    };
    sendBit(false, 1.0);  // start
    for (int b = 0; b < 5; ++b) sendBit((code >> b) & 1, 1.0);
    sendBit(true, params_.stopBits);  // stop
  }
  setLine(true);
}

}  // namespace gtty
