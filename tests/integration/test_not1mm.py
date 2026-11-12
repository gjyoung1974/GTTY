#!/usr/bin/env python3
"""Integration tests: gtty driven by Not1MM's own client code.

Not1MM (https://github.com/mbridak/not1mm) talks to a RTTY modem two ways:

  * F-key messages go out over fldigi's XML-RPC interface through its
    FlDigi_Comm class: main.tx(), then text.add_tx("\\n<message>^r").
  * The radio is shared through rigctld, using its RigctldCAT class.

These tests start a private gtty (own configuration, own ports, a Hamlib dummy
radio behind its own rigctld) and drive it with those two classes, loaded from
a Not1MM checkout, so a change on either side that breaks the pairing shows up
here. Not1MM's GUI is not started.

Run directly, or through ctest (test name: not1mm_integration), where it is
opt-in because it opens a gtty window and takes a couple of minutes:

    python3 tests/integration/test_not1mm.py
    GTTY_INTEGRATION=1 ctest --test-dir build -R not1mm

What it needs, and where it looks:

    gtty binary     $GTTY_BIN, else build/src/gui/gtty
    Not1MM source   $NOT1MM_SRC, else ../not1mm beside this repository,
                    else an importable not1mm package
    rigctld         $RIGCTLD, else PATH (Debian: libhamlib-utils). Without it
                    the radio-sharing and PTT tests are skipped.
    a display       gtty is a GTK program; its window opens while the tests run.

When gtty, Not1MM or a display is missing the script exits with status 77,
which ctest reports as "skipped". The transmit audio is played very quietly on
the default sound device; nothing is keyed except the dummy radio.
"""

import importlib
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import xmlrpc.client
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SKIP = 77


def skip_all(reason):
    print(f"SKIP: {reason}")
    sys.exit(SKIP)


def find_gtty():
    cand = os.environ.get("GTTY_BIN") or str(REPO / "build" / "src" / "gui" / "gtty")
    return cand if os.access(cand, os.X_OK) else None


def load_not1mm():
    """Import Not1MM's fldigi and rigctld clients. Returns (FlDigi_Comm, RigctldCAT)."""
    for src in (os.environ.get("NOT1MM_SRC"), str(REPO.parent / "not1mm")):
        if src and (Path(src) / "not1mm" / "lib" / "fldigi_sendstring.py").is_file():
            sys.path.insert(0, src)
            break
    try:
        fl = importlib.import_module("not1mm.lib.fldigi_sendstring")
        cat = importlib.import_module("not1mm.lib.cat_rigctld")
    except ImportError:
        return None
    return fl.FlDigi_Comm, cat.RigctldCAT


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for(cond, timeout, step=0.1):
    """Poll cond() until it returns something truthy; return that, or None on time-out."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return None


def port_open(port):
    with socket.socket() as s:
        s.settimeout(0.2)
        return s.connect_ex(("127.0.0.1", port)) == 0


PROFILE = """\
[rig]
enabled={rig_enabled}
model=2
device=127.0.0.1:{rig_port}
ptt_type=RIG
poll_ms=100

[tx]
method=0
afsk_level=0.02
ptt_via_rig=true
diddle=true
auto_rx=false
word_mode=true
cwid_at_end=false

[rx]
timestamps=false
log_to_file=true
log_dir={session_dir}

[xmlrpc]
enabled=true
bind=127.0.0.1
port={rpc_port}

[station]
call=N0CALL

[gridtracker]
enabled=false

[cluster]
enabled=false

[ui]
modem_only=true
"""


class Station:
    """A private gtty and (when available) a dummy radio behind rigctld."""

    def __init__(self, gtty_bin, rigctld_bin):
        self.tmp = Path(tempfile.mkdtemp(prefix="gtty-not1mm-"))
        self.rpc_port = free_port()
        self.rig_port = free_port() if rigctld_bin else 0
        self.session_dir = self.tmp / "sessions"
        self.rigctld = None
        self.gtty = None

        if rigctld_bin:
            self.rigctld = subprocess.Popen(
                [rigctld_bin, "-m", "1", "-P", "RIG", "-T", "127.0.0.1", "-t", str(self.rig_port)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if not wait_for(lambda: port_open(self.rig_port), 5):
                self.stop()
                raise RuntimeError("rigctld did not start")

        conf = self.tmp / "config" / "gtty"
        (conf / "profiles").mkdir(parents=True)
        (conf / "gtty.conf").write_text("[profiles]\ncurrent=Test\n")
        (conf / "profiles" / "Test.conf").write_text(PROFILE.format(
            rig_enabled="true" if rigctld_bin else "false", rig_port=self.rig_port,
            session_dir=self.session_dir, rpc_port=self.rpc_port))

        env = dict(os.environ, XDG_CONFIG_HOME=str(self.tmp / "config"),
                   XDG_DATA_HOME=str(self.tmp / "data"))
        self.log = open(self.tmp / "gtty.log", "w")
        self.gtty = subprocess.Popen([gtty_bin], env=env, stdout=self.log, stderr=subprocess.STDOUT)
        if not wait_for(lambda: port_open(self.rpc_port), 20):
            self.stop()
            raise RuntimeError("gtty did not open its XML-RPC port")
        self.url = f"http://127.0.0.1:{self.rpc_port}"

    def rpc(self):
        return xmlrpc.client.ServerProxy(self.url)

    def trx(self):
        return self.rpc().main.get_trx_state()

    def sent_text(self):
        """Everything gtty has transmitted, from its session file."""
        out = []
        for f in sorted(self.session_dir.glob("gtty-*.txt")):
            for line in f.read_text(errors="replace").splitlines(keepends=True):
                if not line.startswith("[") or line[9:13] not in (" TX]", " RX]"):
                    continue
                tx = line[9:13] == " TX]"
                rest = line[14:]
                while rest:
                    i = min((p for p in (rest.find(" <TX> "), rest.find(" <RX> ")) if p >= 0), default=-1)
                    if i < 0:
                        if tx:
                            out.append(rest)
                        break
                    if tx:
                        out.append(rest[:i])
                    tx = rest[i + 2] == "T"
                    rest = rest[i + 6:]
        return "".join(out)

    def stop(self):
        for p in (self.gtty, self.rigctld):
            if p and p.poll() is None:
                p.terminate()
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()
        if getattr(self, "log", None):
            self.log.close()
        shutil.rmtree(self.tmp, ignore_errors=True)


station = None
FlDigi_Comm = None
RigctldCAT = None


def setUpModule():
    global station, FlDigi_Comm, RigctldCAT
    if os.environ.get("GTTY_CTEST") and not os.environ.get("GTTY_INTEGRATION"):
        skip_all("integration tests are opt-in under ctest (set GTTY_INTEGRATION=1)")
    gtty_bin = find_gtty()
    if not gtty_bin:
        skip_all("gtty binary not found (set GTTY_BIN)")
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip_all("no display for gtty")
    classes = load_not1mm()
    if not classes:
        skip_all("Not1MM source not found (set NOT1MM_SRC)")
    FlDigi_Comm, RigctldCAT = classes
    rigctld = os.environ.get("RIGCTLD") or shutil.which("rigctld")
    station = Station(gtty_bin, rigctld)


def tearDownModule():
    if station:
        station.stop()


def not1mm_fldigi():
    """Not1MM's fldigi client. It hard-codes port 7362; point it at the test gtty."""
    comm = FlDigi_Comm()
    comm.target = station.url
    return comm


class Not1MMCase(unittest.TestCase):
    def setUp(self):
        # Every test starts from receive with nothing queued.
        rpc = station.rpc()
        if rpc.main.get_trx_state() != "RX":
            rpc.main.abort()
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 10), "gtty did not return to receive")
        rpc.text.clear_tx()
        self.sent_before = len(station.sent_text())

    def new_sent_text(self):
        return station.sent_text()[self.sent_before:]

    def wait_sent(self, needle, timeout=10):
        return wait_for(lambda: needle in self.new_sent_text(), timeout, 0.25)

    def assert_goes_tx_then_rx(self, timeout=40):
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3), "gtty did not go to transmit")
        self.assertTrue(wait_for(lambda: station.trx() == "RX", timeout),
                        "gtty stayed in transmit after the message")


class FunctionKeys(Not1MMCase):
    """Not1MM's F-keys: FlDigi_Comm.send_string()."""

    def test_modem_answers_as_fldigi(self):
        rpc = station.rpc()
        self.assertEqual(rpc.fldigi.name(), "gtty")
        self.assertEqual(rpc.modem.get_name(), "RTTY")
        self.assertEqual(rpc.main.get_trx_state(), "RX")

    def test_message_is_sent_and_gtty_returns_to_receive(self):
        not1mm_fldigi().send_string("CQ TEST N0CALL N0CALL TEST")
        self.assert_goes_tx_then_rx()
        self.assertTrue(self.wait_sent("CQ TEST N0CALL N0CALL TEST"), self.new_sent_text())

    def test_return_to_receive_code_is_not_transmitted(self):
        # Not1MM ends every message with fldigi's "^r". Neither the caret nor a
        # stray R may go out, and the last word must not be held back.
        not1mm_fldigi().send_string("DE N0CALL")
        self.assert_goes_tx_then_rx()
        self.assertTrue(self.wait_sent("DE N0CALL"), self.new_sent_text())
        sent = self.new_sent_text()
        self.assertNotIn("^", sent)
        self.assertNotIn("N0CALLR", sent.replace(" ", "").replace("\n", ""))

    def test_caret_m_in_a_macro_is_a_new_line(self):
        not1mm_fldigi().send_string("W1AW 599^MTU N0CALL")
        self.assert_goes_tx_then_rx()
        self.assertTrue(self.wait_sent("TU N0CALL"), self.new_sent_text())
        lines = [ln.strip() for ln in self.new_sent_text().splitlines()]
        self.assertIn("W1AW 599", lines)
        self.assertIn("TU N0CALL", lines)

    def test_two_keys_in_a_row_are_both_sent(self):
        comm = not1mm_fldigi()
        comm.send_string("W1AW 599 001")
        comm.send_string("W1AW TU N0CALL")
        self.assert_goes_tx_then_rx(60)
        self.assertTrue(self.wait_sent("W1AW TU N0CALL"), self.new_sent_text())
        sent = self.new_sent_text()
        self.assertIn("W1AW 599 001", sent)
        self.assertLess(sent.index("W1AW 599 001"), sent.index("W1AW TU N0CALL"))
        self.assertEqual(station.trx(), "RX")

    def test_message_without_return_code_stays_in_transmit(self):
        # rxafter=False: Not1MM leaves the modem keyed for more text.
        not1mm_fldigi().send_string("N0CALL TEST ", rxafter=False)
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3))
        self.assertTrue(self.wait_sent("N0CALL TEST"), self.new_sent_text())
        time.sleep(3)
        self.assertEqual(station.trx(), "TX", "gtty dropped to receive without being asked")
        station.rpc().main.rx()
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 5))

    def test_abort_stops_a_transmission(self):
        not1mm_fldigi().send_string("VVV " * 40)
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3))
        station.rpc().main.abort()
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 5), "abort did not stop the transmission")
        # The rest of the message is dropped, not sent on the next key-up.
        not1mm_fldigi().send_string("QRL")
        self.assert_goes_tx_then_rx()
        self.assertLess(self.new_sent_text().count("VVV"), 40)


class SharedRadio(Not1MMCase):
    """Not1MM and gtty on one radio through rigctld: RigctldCAT."""

    def setUp(self):
        if not station.rigctld:
            self.skipTest("rigctld not found (set RIGCTLD, or install libhamlib-utils)")
        super().setUp()
        self.cat = RigctldCAT("127.0.0.1", station.rig_port)
        self.assertTrue(self.cat.online, "Not1MM could not reach rigctld")

    def test_not1mm_qsy_is_seen_by_gtty(self):
        for hz in (14091000, 7043000):
            self.cat.set_vfo(str(hz))
            self.assertTrue(wait_for(lambda: station.rpc().main.get_frequency() == hz, 5),
                            f"gtty did not follow Not1MM to {hz}")

    def test_gtty_qsy_is_seen_by_not1mm(self):
        station.rpc().main.set_frequency(21085000.0)
        self.assertTrue(wait_for(lambda: self.cat.get_vfo() == "21085000", 5),
                        "Not1MM did not see gtty's frequency change")

    def test_not1mm_mode_change_is_seen_by_gtty(self):
        for mode in ("CW", "RTTY"):
            self.cat.set_mode(mode)
            self.assertTrue(wait_for(lambda: station.rpc().rig.get_mode() == mode, 5),
                            f"gtty did not see the radio go to {mode}")

    def test_radio_is_keyed_for_the_message_only(self):
        self.assertEqual(self.cat.get_ptt(), "0")
        not1mm_fldigi().send_string("CQ TEST N0CALL N0CALL TEST")
        self.assertTrue(wait_for(lambda: self.cat.get_ptt() == "1", 3), "the radio was not keyed")
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 40))
        self.assertTrue(wait_for(lambda: self.cat.get_ptt() == "0", 3), "the radio stayed keyed")


if __name__ == "__main__":
    unittest.main(verbosity=2, warnings="ignore")
