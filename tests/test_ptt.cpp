// PTT on a serial control line of its own port: when gtty keys the line itself
// rather than through Hamlib, and how a port that cannot be keyed is reported.
// Keying itself needs a real serial port and is not covered here: a
// pseudo-terminal has no DTR or RTS.
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "core/rig/rig_control.h"
#include "core/rig/serial_ptt.h"
#include "check.h"

using namespace gtty;

int main() {
  // ----------------------------------------------- who keys the line
  RigConfig c;
  c.model = 2;                       // NET rigctl, the rig shared through rigctld
  c.device = "";                     // rigctld's default address
  c.pttType = "RTS";
  c.pttDevice = "/dev/ttyS0";
  CHECK(c.pttOnOwnPort());           // rigctld would not key this line: gtty does
  c.pttType = "DTR";
  CHECK(c.pttOnOwnPort());
  c.pttType = "RIG";
  CHECK(!c.pttOnOwnPort());          // CAT PTT goes through Hamlib
  c.pttType = "NONE";
  CHECK(!c.pttOnOwnPort());
  c.pttType = "RTS";
  c.pttDevice = "";
  CHECK(!c.pttOnOwnPort());          // blank: the rig's own port, which Hamlib has open
  c.device = "/dev/ttyUSB0";
  c.pttDevice = "/dev/ttyUSB0";
  CHECK(!c.pttOnOwnPort());
  c.pttDevice = "/dev/ttyUSB1";
  CHECK(c.pttOnOwnPort());

  // ----------------------------------------------- a port that is not there
  SerialPtt ptt;
  std::string err;
  CHECK(!ptt.open("/dev/gtty-no-such-port", SerialPtt::Line::Rts, err));
  CHECK(err.find("/dev/gtty-no-such-port") != std::string::npos);
  CHECK(!ptt.isOpen());
  CHECK(!ptt.set(true, &err));
  CHECK(!ptt.get());

  // ----------------------------------------------- a device without control lines
  // A pseudo-terminal opens but has no modem lines: refused at open with the reason,
  // not accepted and then silently not keyed.
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0) {
    std::string slave = ptsname(master);
    err.clear();
    bool opened = ptt.open(slave, SerialPtt::Line::Rts, err);
    if (opened) {
      // A kernel that does accept the ioctl on a pty: then keying must report success and release again.
      CHECK(ptt.set(true, &err));
      CHECK(ptt.set(false, &err));
      ptt.close();
    } else {
      CHECK(err.find(slave) != std::string::npos);
      CHECK(err.find("control lines") != std::string::npos);
    }
    CHECK(!ptt.isOpen());
  }
  if (master >= 0) ::close(master);

  // ----------------------------------------------- RigControl without a rig
  // The PTT port stands apart from the rig connection: with rig control off there is
  // nothing to key unless the port opens, and a failure names the port.
  RigControl rig;
  RigConfig none;
  none.pttType = "RIG";
  CHECK(rig.openPttPort(none, err));         // nothing to open
  CHECK(!rig.canPtt());
  CHECK(!rig.setPtt(true));
  RigConfig bad;
  bad.pttType = "RTS";
  bad.pttDevice = "/dev/gtty-no-such-port";
  err.clear();
  CHECK(!rig.openPttPort(bad, err));
  CHECK(err.find("/dev/gtty-no-such-port") != std::string::npos);
  // PTT stays this port's job, so that keying fails with the reason instead of
  // quietly doing nothing or going to the rig.
  CHECK(rig.canPtt());
  CHECK(!rig.setPtt(true));
  CHECK(rig.state().error.find("/dev/gtty-no-such-port") != std::string::npos);
  CHECK(!rig.isOpen());
  rig.close();
  CHECK(!rig.canPtt());
  TEST_MAIN_END();
}
