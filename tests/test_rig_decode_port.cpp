// The radio's built-in decoder read from a serial port. A pseudo-terminal stands in
// for the radio's port: what is written to its master side is what the radio would
// send. The real thing (an Icom's "RTTY Decode" port) is not covered here.
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "core/rig/rig_decode_port.h"
#include "check.h"

using namespace gtty;

namespace {

struct Sink {
  std::mutex mu;
  std::string text, error;
  std::string textNow() { std::lock_guard<std::mutex> lk(mu); return text; }
  std::string errorNow() { std::lock_guard<std::mutex> lk(mu); return error; }
};

// Waits up to two seconds for done() to hold.
template <typename F>
bool waitFor(F done) {
  for (int i = 0; i < 200; ++i) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}

void send(int fd, const std::string& s) { CHECK_EQ(::write(fd, s.data(), s.size()), static_cast<ssize_t>(s.size())); }

}  // namespace

int main() {
  // ----------------------------------------------- what is printed
  const char raw[] = {'C', 'Q', ' ', '\0', 'D', 'E', '\x07', '\r', '\n', '\xff', 'K', '\x7f'};
  CHECK_EQ(RigDecodePort::clean(raw, sizeof raw), std::string("CQ DE\r\nK"));
  CHECK_EQ(RigDecodePort::clean(nullptr, 0), std::string());

  CHECK(RigDecodePort::supportedBaud(9600));
  CHECK(RigDecodePort::supportedBaud(4800));
  CHECK(!RigDecodePort::supportedBaud(45));
  CHECK(!RigDecodePort::supportedBaud(0));

  // ----------------------------------------------- ports that cannot be read
  RigDecodePort port;
  std::string err;
  CHECK(!port.open("/dev/gtty-no-such-port", 9600, err));
  CHECK(err.find("/dev/gtty-no-such-port") != std::string::npos);
  CHECK(!port.isOpen());
  err.clear();
  CHECK(!port.open("/dev/null", 9600, err));          // opens, but is no serial port
  CHECK(err.find("/dev/null") != std::string::npos);
  CHECK(!port.isOpen());
  err.clear();
  CHECK(!port.open("/dev/null", 1234, err));
  CHECK(err.find("1234") != std::string::npos);

  // ----------------------------------------------- reading
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
    std::printf("no pseudo-terminal here: reading not tested\n");
    TEST_MAIN_END();
  }
  std::string slave = ptsname(master);

  Sink sink;
  port.onText = [&](const std::string& t) { std::lock_guard<std::mutex> lk(sink.mu); sink.text += t; };
  port.onError = [&](const std::string& m) { std::lock_guard<std::mutex> lk(sink.mu); sink.error = m; };
  CHECK(port.open(slave, 9600, err));
  CHECK(port.isOpen());
  CHECK_EQ(port.device(), slave);

  // Line ends arrive as the radio sent them (the receive pane makes one break of them),
  // and nothing is echoed back to the radio.
  send(master, "CQ CQ DE DF7CB\r\n");
  CHECK(waitFor([&] { return sink.textNow() == "CQ CQ DE DF7CB\r\n"; }));
  send(master, std::string("5\x00" "99\x80 001", 9));
  CHECK(waitFor([&] { return sink.textNow() == "CQ CQ DE DF7CB\r\n599 001"; }));
  int flags = fcntl(master, F_GETFL);
  fcntl(master, F_SETFL, flags | O_NONBLOCK);
  char back[16];
  CHECK(::read(master, back, sizeof back) <= 0);
  fcntl(master, F_SETFL, flags);

  // Muted (while transmitting): read and dropped, not kept for later.
  port.setMuted(true);
  send(master, "OWN TEXT");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  port.setMuted(false);
  send(master, " TU");
  CHECK(waitFor([&] { return sink.textNow() == "CQ CQ DE DF7CB\r\n599 001 TU"; }));
  CHECK(sink.errorNow().empty());

  // Reopening starts clean.
  port.close();
  CHECK(!port.isOpen());
  CHECK(port.open(slave, 19200, err));
  send(master, "AGN");
  CHECK(waitFor([&] { return sink.textNow() == "CQ CQ DE DF7CB\r\n599 001 TUAGN"; }));

  // The radio goes away: reported once, with the port's name.
  ::close(master);
  CHECK(waitFor([&] { return !sink.errorNow().empty(); }));
  CHECK(sink.errorNow().find(slave) != std::string::npos);
  port.close();
  CHECK(!port.isOpen());

  TEST_MAIN_END();
}
