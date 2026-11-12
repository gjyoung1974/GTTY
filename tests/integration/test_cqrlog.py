#!/usr/bin/env python3
"""Integration tests: gtty polled the way CQRLOG polls fldigi.

CQRLOG (https://github.com/ok2cqr/cqrlog) is a Lazarus program with a MySQL
log, so its client cannot be loaded here. CqrlogClient below repeats what its
"fldigi XmlRpc remote" does in src/fXfldigi.pas:

    PollFldigi()   one TCP connection per call; an HTTP/1.0 POST of a
                   methodCall without parameters; read until nothing more
                   comes; the answer is whatever stands between the first
                   "<value>" and "</value>" of the reply. A reply holding
                   the word "fault" counts as no answer.
    TimTime()      every few seconds: log.get_frequency (kHz), modem.get_mode,
                   modem.get_submode (modem.get_name when get_mode gives
                   nothing), log.get_date_on, log.get_time_on, log.get_time_off,
                   log.get_call, log.get_name, log.get_rst_in, log.get_rst_out,
                   log.get_state, log.get_province, log.get_qth, log.get_notes,
                   log.get_locator, main.get_trx_state
    btSaveQSO      log.clear, after CQRLOG has saved the QSO

CQRLOG does not parse the XML: a reply written as <value><string>K1ABC</string>
</value> puts "<string>K1ABC</string>" in its Call field. fldigi writes strings
without the <string> element, and so must gtty. If CQRLOG changes how it talks
to fldigi, update CqrlogClient to match.

Run directly, or through ctest (test name: cqrlog_integration, opt-in):

    python3 tests/integration/test_cqrlog.py
    GTTY_INTEGRATION=1 ctest --test-dir build -R cqrlog

Needs the gtty binary and a display (see test_not1mm.py).
"""

import os
import re
import socket
import sys
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_not1mm import Station, find_gtty, skip_all, wait_for  # noqa: E402

HEADER = "POST /RPC2 HTTP/1.0\r\nHost: localhost:7362\r\nContent-Type: text/xml\r\nContent-Length: "
XMLSTART = '<?xml version="1.0"?>\r\n<methodCall>\r\n<methodName>'
XMLEND = "</methodName>\r\n<params>\r\n</params>\r\n</methodCall>\r\n"

# TimTime's calls with CQRLOG's defaults for "frequency from fldigi" and "mode from fldigi".
FIELDS = [("frequency", "log.get_frequency"), ("mode", "modem.get_mode"), ("submode", "modem.get_submode"),
          ("startdate", "log.get_date_on"), ("time_on", "log.get_time_on"), ("time_off", "log.get_time_off"),
          ("call", "log.get_call"), ("name", "log.get_name"), ("rst_in", "log.get_rst_in"),
          ("rst_out", "log.get_rst_out"), ("state", "log.get_state"), ("county", "log.get_province"),
          ("qth", "log.get_qth"), ("notes", "log.get_notes"), ("locator", "log.get_locator"),
          ("trx_state", "main.get_trx_state")]


class CqrlogClient:
    """CQRLOG's PollFldigi and TimTime."""

    def __init__(self, port):
        self.port = port
        self.slowest = 0.0

    def poll(self, method):
        """PollFldigi(): returns (socket_ok, answer). answer is "" on a fault."""
        t0 = time.monotonic()
        try:
            sock = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        except OSError:
            return False, ""
        data = XMLSTART + method + XMLEND
        sock.sendall((HEADER + str(len(data)) + "\r\n\r\n" + data).encode("ascii"))
        resp = b""
        sock.settimeout(2)                             # RecvPacket(2000)
        try:
            while True:
                buf = sock.recv(4096)
                if not buf:
                    break
                resp += buf
        except socket.timeout:
            pass
        sock.close()
        self.slowest = max(self.slowest, time.monotonic() - t0)
        resp = resp.decode("latin-1")
        if not resp or "fault" in resp:
            return True, ""
        start, end = resp.find("<value>"), resp.find("</value>")
        if start < 0 or end < 0:
            return True, ""
        return True, resp[start + 7:end]

    def tim_time(self):
        """One timer round: the record CQRLOG copies into its New QSO window."""
        rec = {}
        for field, method in FIELDS:
            ok, rec[field] = self.poll(method)
            if not ok:
                raise AssertionError(f"socket error on {method}")
        if rec["mode"] == "":                          # an fldigi without modem.get_mode
            rec["mode"] = self.poll("modem.get_name")[1]
        mhz = rec["frequency"].strip().replace(",", ".")
        rec["frequency"] = f"{float(mhz) / 1000:.5f}"  # FloatToStrF(tmp/1000, ffFixed, 8, 5)
        return rec

    @staticmethod
    def new_qso_fields(rec):
        """SetCqrFields(): the date and times are cut up by position."""
        d, on, off = rec["startdate"], rec["time_on"], rec["time_off"]
        return {"date": f"{d[0:4]}-{d[4:6]}-{d[6:8]}", "start": f"{on[0:2]}:{on[2:4]}", "end": f"{off[0:2]}:{off[2:4]}"}


station = None


def setUpModule():
    global station
    if os.environ.get("GTTY_CTEST") and not os.environ.get("GTTY_INTEGRATION"):
        skip_all("integration tests are opt-in under ctest (set GTTY_INTEGRATION=1)")
    gtty_bin = find_gtty()
    if not gtty_bin:
        skip_all("gtty binary not found (set GTTY_BIN)")
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip_all("no display for gtty")
    station = Station(gtty_bin, None)                  # CQRLOG has its own rig control; none is needed here


def tearDownModule():
    if station:
        station.stop()


class CqrlogCase(unittest.TestCase):
    def setUp(self):
        rpc = station.rpc()
        if rpc.main.get_trx_state() != "RX":
            rpc.main.abort()
        self.assertTrue(wait_for(lambda: station.trx() == "RX", 10), "gtty did not return to receive")
        rpc.text.clear_tx()
        rpc.log.clear()
        self.cqr = CqrlogClient(station.rpc_port)


class Polling(CqrlogCase):
    def test_every_answer_is_bare_text(self):
        rpc = station.rpc()
        rpc.log.set_call("dl1abc")
        rpc.log.set_name("Hans")
        rpc.log.set_qth("Berlin")
        rpc.log.set_locator("JO62")
        for field, method in FIELDS:
            ok, value = self.cqr.poll(method)
            self.assertTrue(ok, method)
            self.assertNotIn("<", value, f"{method} would put markup into CQRLOG's {field} field: {value!r}")
            self.assertNotIn(">", value, method)
        rec = self.cqr.tim_time()
        self.assertEqual(rec["call"], "DL1ABC")
        self.assertEqual(rec["name"], "Hans")
        self.assertEqual(rec["qth"], "Berlin")
        self.assertEqual(rec["locator"], "JO62")
        self.assertEqual(rec["rst_in"], "599")
        self.assertEqual(rec["rst_out"], "599")
        self.assertEqual(rec["trx_state"], "RX")

    def test_mode_comes_from_get_mode(self):
        ok, mode = self.cqr.poll("modem.get_mode")     # the ADIF mode; CQRLOG maps it to its own mode list
        self.assertEqual(mode, "RTTY")
        self.assertEqual(self.cqr.poll("modem.get_submode")[1], "")
        self.assertEqual(self.cqr.tim_time()["mode"], "RTTY")

    def test_frequency_is_khz_and_becomes_mhz(self):
        station.rpc().rig.set_frequency(14085500.0)
        self.assertTrue(wait_for(lambda: station.rpc().rig.get_frequency() == 14085500.0, 5))
        ok, khz = self.cqr.poll("log.get_frequency")
        self.assertRegex(khz, r"^\d+\.\d+$")
        self.assertAlmostEqual(float(khz), 14085.5, places=3)
        self.assertEqual(self.cqr.tim_time()["frequency"], "14.08550")

    def test_date_and_times_have_the_widths_cqrlog_cuts(self):
        rec = self.cqr.tim_time()
        self.assertRegex(rec["startdate"], r"^\d{8}$")
        self.assertRegex(rec["time_on"], r"^\d{4}$")
        self.assertRegex(rec["time_off"], r"^\d{4}$")
        shown = self.cqr.new_qso_fields(rec)
        self.assertEqual(shown["date"], time.strftime("%Y-%m-%d", time.gmtime()))
        self.assertTrue(re.match(r"^\d\d:\d\d$", shown["start"]) and re.match(r"^\d\d:\d\d$", shown["end"]), shown)

    def test_unknown_method_reads_as_no_answer(self):
        ok, value = self.cqr.poll("log.get_no_such_field")
        self.assertTrue(ok)
        self.assertEqual(value, "")

    def test_a_round_does_not_wait_for_time_outs(self):
        # CQRLOG reads until the connection closes; a server that kept an HTTP/1.0
        # connection open would cost it two seconds per call and stall its window.
        t0 = time.monotonic()
        self.cqr.tim_time()
        self.assertLess(self.cqr.slowest, 1.0, "gtty left the connection open after answering")
        self.assertLess(time.monotonic() - t0, 5.0)


    def test_another_logger_with_an_open_connection_does_not_hold_cqrlog_up(self):
        # A contest logger beside CQRLOG: Python and xmlrpc-c clients keep their
        # connection to gtty open between calls.
        other = station.rpc()
        other.main.get_trx_state()
        t0 = time.monotonic()
        rec = self.cqr.tim_time()
        self.assertEqual(rec["trx_state"], "RX")
        self.assertLess(time.monotonic() - t0, 2.0, "CQRLOG waited for the other program's connection")
        self.assertEqual(other.main.get_trx_state(), "RX")   # and that connection is still good


class Qso(CqrlogCase):
    def test_trx_state_follows_the_transmitter(self):
        rpc = station.rpc()
        rpc.log.set_call("K1ABC")
        rpc.text.add_tx("K1ABC DE N0CALL 599 599 K^r")
        rpc.main.tx()
        # CQRLOG offers "Save QSO" once it has seen a call together with TX.
        seen = wait_for(lambda: self.cqr.tim_time() if self.cqr.poll("main.get_trx_state")[1] == "TX" else None, 5, 0.1)
        self.assertTrue(seen, "CQRLOG never saw gtty transmitting")
        self.assertEqual(seen["call"], "K1ABC")
        self.assertTrue(wait_for(lambda: self.cqr.poll("main.get_trx_state")[1] == "RX", 40), "gtty stayed in transmit")

    def test_save_clears_the_log_row(self):
        rpc = station.rpc()
        rpc.log.set_call("K1ABC")
        rpc.log.set_name("Al")
        self.assertEqual(self.cqr.tim_time()["call"], "K1ABC")
        ok, _ = self.cqr.poll("log.clear")             # btSaveQSOClick
        self.assertTrue(ok)
        rec = self.cqr.tim_time()
        self.assertEqual(rec["call"], "")
        self.assertEqual(rec["name"], "")
        self.assertEqual(rec["rst_in"], "599")


if __name__ == "__main__":
    unittest.main(verbosity=2, warnings="ignore")
