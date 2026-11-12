#!/usr/bin/env python3
"""Integration tests: gtty driven the way TLF drives fldigi.

TLF (https://github.com/Tlf/tlf) is a C program, so its client cannot be
loaded here as Not1MM's is in test_not1mm.py. Instead TlfClient below repeats,
call for call, what TLF's src/fldigixmlrpc.c does:

    fldigi_send_text()         look at main.get_trx_state; if still sending,
                               main.rx + text.clear_tx and wait 2 s; then
                               text.add_tx("<message>^r") and main.tx
    fldigi_to_rx()             main.rx + text.clear_tx   (Escape in TLF)
    fldigi_get_rx_text()       text.get_rx_length, text.get_rx(from, count)
    fldigi_get_log_frequency() log.get_frequency, a string in kHz
    fldigi_set_log_frequency() rig.get_frequency, rig.set_frequency
    fldigi_auto_qsy()          rig.get_mode, modem.get_carrier, modem.set_carrier
    fldigi_get_log_call()      log.get_call, log.set_call("")

TLF reads each reply as one fixed type and does not check it, so the tests also
pin the types. If TLF changes how it talks to fldigi, update TlfClient to match.

Run directly, or through ctest (test name: tlf_integration, opt-in):

    python3 tests/integration/test_tlf.py
    GTTY_INTEGRATION=1 ctest --test-dir build -R tlf

Needs the gtty binary and a display (see test_not1mm.py). rigctld is used when
found; pactl, paplay and gtty-cli are needed for the receive test only.
"""

import os
import shutil
import sys
import time
import unittest
import xmlrpc.client
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_not1mm import Station, find_gtty, skip_all, wait_for  # noqa: E402

SWEET_SPOT = 2125 + 170 // 2      # TLF's DEFAULT_FLDIGI_RTTY_SWEET_SPOT
RTTY_MAX_RIT = 20


class TlfClient:
    """TLF's fldigi calls, in TLF's order."""

    def __init__(self, url):
        self.url = url
        self.lastpos = 0

    def _rpc(self):
        return xmlrpc.client.ServerProxy(self.url)

    def to_rx(self):                                   # fldigi_to_rx()
        rpc = self._rpc()
        rpc.main.rx()
        rpc.text.clear_tx()

    def send_text(self, line):                         # fldigi_send_text(), normal (not keyboard) mode
        rpc = self._rpc()
        if rpc.main.get_trx_state() == "TX":           # cancel_tx()
            self.to_rx()
            time.sleep(2)
        rpc.text.add_tx(line + "^r")
        rpc.main.tx()

    def get_rx_text(self, maxlen=256):                 # fldigi_get_rx_text()
        rpc = self._rpc()
        textlen = rpc.text.get_rx_length()
        if self.lastpos == 0:
            self.lastpos = textlen
        available = min(textlen - self.lastpos, maxlen - 1)
        out = ""
        if available > 0:
            out = rpc.text.get_rx(self.lastpos, available).data.decode("ascii", "replace")
        self.lastpos = textlen
        return out

    def get_log_frequency(self):                       # fldigi_get_log_frequency(): kHz string -> Hz
        return float(self._rpc().log.get_frequency().replace(",", ".")) * 1000.0

    def set_log_frequency(self, hz):                   # fldigi_set_log_frequency()
        rpc = self._rpc()
        offset = self.get_log_frequency() - rpc.rig.get_frequency()
        rpc.rig.set_frequency(float(hz - offset))

    def auto_qsy(self):                                # fldigi_auto_qsy(); returns the rig correction in Hz
        rpc = self._rpc()
        if rpc.rig.get_mode() != "RTTY":
            return None
        offset = rpc.modem.get_carrier() - SWEET_SPOT
        if abs(offset) <= RTTY_MAX_RIT:
            return 0
        rpc.modem.set_carrier(SWEET_SPOT)
        return -offset


station = None
band = None


def setUpModule():
    global station, band
    if os.environ.get("GTTY_CTEST") and not os.environ.get("GTTY_INTEGRATION"):
        skip_all("integration tests are opt-in under ctest (set GTTY_INTEGRATION=1)")
    gtty_bin = find_gtty()
    if not gtty_bin:
        skip_all("gtty binary not found (set GTTY_BIN)")
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip_all("no display for gtty")
    station = Station(gtty_bin, os.environ.get("RIGCTLD") or shutil.which("rigctld"))
    cli = os.environ.get("GTTY_CLI") or str(Path(gtty_bin).parents[1] / "cli" / "gtty-cli")
    if shutil.which("pactl") and shutil.which("paplay") and os.access(cli, os.X_OK):
        try:
            from test_not1mm_gui import VirtualBand
            band = VirtualBand(station.gtty.pid, cli)
        except Exception as e:
            print(f"note: no virtual band, the receive test will be skipped ({e})")


def tearDownModule():
    if band:
        band.close()
    if station:
        station.stop()


class TlfCase(unittest.TestCase):
    def setUp(self):
        rpc = station.rpc()
        if rpc.main.get_trx_state() != "RX":
            rpc.main.abort()
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 10), "gtty did not return to receive")
        rpc.text.clear_tx()
        self.tlf = TlfClient(station.url)
        self.sent_before = len(station.sent_text())

    def new_sent_text(self):
        return station.sent_text()[self.sent_before:]

    def wait_sent(self, needle, timeout=10):
        return wait_for(lambda: needle in self.new_sent_text(), timeout, 0.25)


class Messages(TlfCase):
    """F-keys and Escape."""

    def test_message_is_sent_and_gtty_returns_to_receive(self):
        # TLF queues the text first and keys afterwards.
        self.tlf.send_text("CQ TEST N0CALL N0CALL CQ")
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3), "gtty did not go to transmit")
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 40), "gtty stayed in transmit")
        self.assertTrue(self.wait_sent("CQ TEST N0CALL N0CALL CQ"), self.new_sent_text())
        sent = self.new_sent_text()
        self.assertNotIn("^", sent)
        self.assertNotIn("CQR", sent.replace(" ", "").replace("\n", "")[-4:])

    def test_escape_stops_the_message(self):
        self.tlf.send_text("VVV " * 30)
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3))
        time.sleep(3)
        t0 = time.monotonic()
        self.tlf.to_rx()
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 3), "Escape did not stop the transmission")
        self.assertLess(time.monotonic() - t0, 3)
        time.sleep(1.5)
        self.assertLess(self.new_sent_text().count("VVV"), 15)
        # Nothing of the stopped message is left over for the next key-up.
        self.sent_before = len(station.sent_text())
        self.tlf.send_text("QRL")
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3))
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 20))
        self.assertTrue(self.wait_sent("QRL"), self.new_sent_text())
        self.assertNotIn("VVV", self.new_sent_text())

    def test_new_message_replaces_the_one_going_out(self):
        self.tlf.send_text("VVV " * 30)
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3))
        time.sleep(3)
        self.tlf.send_text("W1AW 599 001")           # TLF cancels, waits 2 s, then sends
        self.assertTrue(wait_for(lambda: station.trx() == "TX", 3))
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 30), "gtty stayed in transmit")
        self.assertTrue(self.wait_sent("W1AW 599 001"), self.new_sent_text())
        sent = self.new_sent_text()
        self.assertLess(sent.count("VVV"), 15)
        self.assertNotIn("VVV", sent[sent.index("W1AW 599 001"):])


class Polling(TlfCase):
    """What TLF's background loop reads."""

    def test_reply_types_are_the_ones_tlf_reads(self):
        rpc = station.rpc()
        for name in ("main.get_trx_state", "rig.get_mode", "log.get_frequency", "log.get_call", "log.get_exchange"):
            self.assertIsInstance(getattr(rpc, name)(), str, name)
        self.assertIsInstance(rpc.text.get_rx_length(), int)
        self.assertIsInstance(rpc.modem.get_carrier(), int)
        self.assertIsInstance(rpc.rig.get_frequency(), float)
        self.assertIsInstance(rpc.text.get_rx(0, 0), xmlrpc.client.Binary)

    def test_log_frequency_is_in_khz(self):
        station.rpc().rig.set_frequency(14085000.0)
        self.assertTrue(wait_for(lambda: station.rpc().rig.get_frequency() == 14085000.0, 5))
        self.assertAlmostEqual(self.tlf.get_log_frequency(), 14085000.0, delta=1)

    def test_tlf_qsy_lands_on_the_requested_frequency(self):
        self.tlf.set_log_frequency(7043500)
        self.assertTrue(wait_for(lambda: abs(self.tlf.get_log_frequency() - 7043500) < 1, 5),
                        f"TLF reads {self.tlf.get_log_frequency()} Hz")
        self.assertEqual(station.rpc().rig.get_frequency(), 7043500.0)

    def test_auto_qsy_brings_the_tones_back_once(self):
        rpc = station.rpc()
        if rpc.rig.get_mode() != "RTTY":
            rpc.rig.set_mode("RTTY")
            if not wait_for(lambda: station.rpc().rig.get_mode() == "RTTY", 5):
                self.skipTest("the radio is not in RTTY (no rigctld)")
        rpc.modem.set_carrier(SWEET_SPOT + 300)        # the operator clicked a signal 300 Hz up
        self.assertEqual(self.tlf.auto_qsy(), -300)
        time.sleep(0.5)
        self.assertLessEqual(abs(rpc.modem.get_carrier() - SWEET_SPOT), RTTY_MAX_RIT)
        self.assertEqual(self.tlf.auto_qsy(), 0, "TLF would retune the radio a second time")

    def test_call_clicked_in_gtty_reaches_tlf_and_is_cleared(self):
        rpc = station.rpc()
        rpc.log.set_call("k1abc")                      # stands in for a click on a call in gtty
        self.assertEqual(rpc.log.get_call(), "K1ABC")
        rpc.log.set_call("")                           # TLF, once the operator has cleared its call field
        self.assertEqual(rpc.log.get_call(), "")

    def test_received_text_arrives_in_pieces(self):
        if not band:
            self.skipTest("no virtual sound device (needs pactl, paplay and gtty-cli)")
        self.tlf.get_rx_text()                         # TLF starts from the current end of the text
        player = band.transmit("N0CALL N0CALL DE K1ABC K1ABC K1ABC")
        got = []
        heard = wait_for(lambda: got.append(self.tlf.get_rx_text()) or "K1ABC" in "".join(got), 30, 0.2)
        player.wait(30)
        self.assertTrue(heard, f"TLF would have seen: {''.join(got)!r}")


if __name__ == "__main__":
    unittest.main(verbosity=2, warnings="ignore")
