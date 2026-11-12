#!/usr/bin/env python3
"""Integration tests: gtty's skimmer spot feed as loggers read it.

With the Skimmer on and "Serve skimmer spots" ticked, gtty is a small telnet
DX cluster: a logger connects, logs in, and gets a "DX de" line for each
station the Skimmer identifies. Here a station calls CQ in RTTY on a virtual
sound device, and three clients that repeat the cluster code of real loggers
must each end up with the spot:

    TLF      src/bandmap.c bm_add(): fixed columns. The frequency is read from
             column 16, the callsign from column 26. TLF sends the callsign
             as soon as it is connected (CLUSTERLOGIN).
    Not1MM   not1mm/clusterwindow.py cluster_receive(): answers a line holding
             "login:", waits for a line holding "HELLO", then sends its filter
             commands; a spot is split on blanks.
    QLog     ui/DxWidget.cpp: answers a line starting with "login"; a spot must
             match its dxSpotRE regular expression.

If one of the programs changes its cluster code, update the copy below.

Run directly, or through ctest (test name: spot_feed, opt-in):

    python3 tests/integration/test_spot_feed.py
    GTTY_INTEGRATION=1 ctest --test-dir build -R spot_feed

Needs the gtty binary, a display, and for the received audio pactl, paplay
and gtty-cli (see test_not1mm_gui.py); without those it is skipped.
"""

import os
import re
import shutil
import socket
import sys
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_not1mm import Station, find_gtty, free_port, skip_all, wait_for  # noqa: E402

DIAL = 14085000.0
MARK = 2125                       # gtty-cli encode's mark tone; no rig, so the receiver counts as LSB
SPOT_KHZ = (DIAL - MARK) / 1000   # RTTY is spotted on its mark frequency

PROFILE_EXTRA = """
[decoders]
skimmer=true

[spots]
enabled=true
bind=127.0.0.1
port={port}

[station]
call=N0CALL
"""


class Telnet:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(0.2)
        self.buf = ""

    def read(self):
        try:
            data = self.sock.recv(4096)
        except OSError:
            return
        self.buf += data.decode("ascii", "replace")

    def lines(self):
        """Complete lines received so far, removed from the buffer."""
        self.read()
        *done, self.buf = self.buf.split("\n")
        return [l.rstrip("\r") for l in done]

    def send(self, text):
        self.sock.sendall((text + "\r\n").encode("ascii"))

    def close(self):
        self.sock.close()


class TlfCluster(Telnet):
    """TLF: the callsign goes out on connecting; every line is offered to bm_add()."""

    def __init__(self, port):
        super().__init__(port)
        self.spots = []
        self.send("N0CALL")

    def poll(self):
        for line in self.lines():
            if not line.startswith("DX de "):          # strncmp(line, "DX de ", 6)
                continue
            tok = line[26:].split()                    # strtok(line + 26, " \t")
            if not tok:
                continue
            m = re.match(r"\s*[0-9.]+", line[16:])     # atof(line + 16)
            khz = float(m.group()) if m else 0.0
            # cluster ageing (clusterinfo.c) wants 74 columns and the time at 70..73
            self.spots.append({"call": tok[0], "hz": khz * 1000, "aged_ok": len(line) >= 74 and line[70:74].isdigit()})


class Not1mmCluster(Telnet):
    """Not1MM's cluster window."""

    def __init__(self, port):
        super().__init__(port)
        self.spots, self.hello, self.sent = [], False, []

    def _line(self, data):
        data = data.replace("\x07", "").strip()
        low = data.lower()
        if "login:" in low or "call:" in low or "callsign:" in low:
            self.send("N0CALL")
            self.sent.append("N0CALL")
        if "HELLO" in data.upper():
            self.hello = True
            for cmd in ("", "set dx extension Section", "set dx mode OPEN", "sh wwv 1"):
                self.send(cmd)
                self.sent.append(cmd)
        if "DX de" in data:
            parts = data.split()
            try:
                self.spots.append({"spotter": parts[2], "freq": float(parts[3]), "dx": parts[4],
                                   "time": parts[-1], "comment": " ".join(parts[5:-1])})
            except (ValueError, IndexError):
                pass

    def poll(self):
        if not self.hello:
            # Before the greeting Not1MM reads whatever has arrived, prompt included.
            self.read()
            if self.buf and not self.buf.endswith("\n"):
                *done, last = self.buf.split("\n")
                self.buf = "\n".join(done) + ("\n" if done else "")
                self._line(last)
        for line in self.lines():
            self._line(line)


class QlogCluster(Telnet):
    """QLog's DX cluster widget."""

    SPOT = re.compile(r"^DX de ([a-zA-Z0-9\/]+).*:\s+([0-9|.]+)\s+([a-zA-Z0-9\/]+)[^\s]*\s+(.*)\s+(\d{4}Z)", re.I)

    def __init__(self, port):
        super().__init__(port)
        self.spots, self.state = [], "CONNECTED"

    def poll(self):
        self.read()
        # QLog splits what has arrived on line ends; the prompt needs no newline.
        chunks = [c for c in re.split(r"[\a\n\r]+", self.buf) if c]
        self.buf = ""
        for line in chunks:
            if self.state == "CONNECTED" and (line.lower().startswith("login") or re.search(r"enter your call(sign)?:", line)):
                self.send("N0CALL")
                self.state = "LOGIN_SENT"
                continue
            m = self.SPOT.match(line)
            if m:
                self.spots.append({"spotter": m.group(1), "freq": float(m.group(2)), "call": m.group(3),
                                   "comment": m.group(4).strip()})


station = None
band = None
port = None


def setUpModule():
    global station, band, port
    if os.environ.get("GTTY_CTEST") and not os.environ.get("GTTY_INTEGRATION"):
        skip_all("integration tests are opt-in under ctest (set GTTY_INTEGRATION=1)")
    gtty_bin = find_gtty()
    if not gtty_bin:
        skip_all("gtty binary not found (set GTTY_BIN)")
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip_all("no display for gtty")
    cli = os.environ.get("GTTY_CLI") or str(Path(gtty_bin).parents[1] / "cli" / "gtty-cli")
    if not (shutil.which("pactl") and shutil.which("paplay") and os.access(cli, os.X_OK)):
        skip_all("no virtual sound device (needs pactl, paplay and gtty-cli)")
    port = free_port()
    station = Station(gtty_bin, None, PROFILE_EXTRA.format(port=port))
    try:
        from test_not1mm_gui import VirtualBand
        band = VirtualBand(station.gtty.pid, cli)
    except Exception as e:
        station.stop()
        skip_all(f"no virtual sound device ({e})")
    station.rpc().rig.set_frequency(DIAL)


def tearDownModule():
    if band:
        band.close()
    if station:
        station.stop()


class SpotFeed(unittest.TestCase):
    def test_a_cq_reaches_every_logger(self):
        tlf, n1mm, qlog = TlfCluster(port), Not1mmCluster(port), QlogCluster(port)
        clients = (tlf, n1mm, qlog)
        try:
            # Everyone logs in before the station calls.
            self.assertTrue(wait_for(lambda: [c.poll() for c in clients] and n1mm.hello and qlog.state == "LOGIN_SENT", 5),
                            f"login did not complete: Not1MM hello={n1mm.hello}, QLog {qlog.state}")
            time.sleep(0.5)

            player = band.transmit("CQ CQ TEST DE K1ABC K1ABC K1ABC CQ")
            got = wait_for(lambda: [c.poll() for c in clients] and all(c.spots for c in clients), 40, 0.2)
            player.wait(30)
            self.assertTrue(got, f"spots: TLF {tlf.spots}, Not1MM {n1mm.spots}, QLog {qlog.spots}")

            t = tlf.spots[0]
            self.assertEqual(t["call"], "K1ABC")
            self.assertAlmostEqual(t["hz"] / 1000, SPOT_KHZ, delta=0.1)
            self.assertTrue(t["aged_ok"], "TLF could not age the spot: time not in columns 70 to 73")

            n = n1mm.spots[0]
            self.assertEqual(n["dx"], "K1ABC")
            self.assertEqual(n["spotter"], "N0CALL-#:")
            self.assertAlmostEqual(n["freq"], SPOT_KHZ, delta=0.1)
            self.assertIn("RTTY", n["comment"])
            self.assertIn("CQ", n["comment"])
            self.assertRegex(n["time"], r"^\d{4}Z$")

            q = qlog.spots[0]
            self.assertEqual(q["call"], "K1ABC")
            self.assertEqual(q["spotter"], "N0CALL")
            self.assertAlmostEqual(q["freq"], SPOT_KHZ, delta=0.1)
            self.assertIn("RTTY", q["comment"])

            # The station keeps calling: one spot, not one per line of text.
            player = band.transmit("CQ CQ TEST DE K1ABC K1ABC K1ABC CQ")
            player.wait(30)
            time.sleep(2)
            for c in clients:
                c.poll()
                self.assertEqual(len(c.spots), 1, f"{type(c).__name__} was sent the same station again: {c.spots}")

            # Someone who connects later is shown what has been heard.
            late = QlogCluster(port)
            try:
                self.assertTrue(wait_for(lambda: late.poll() or late.spots, 5), "no replay for a late client")
                self.assertEqual(late.spots[0]["call"], "K1ABC")
            finally:
                late.close()
        finally:
            for c in clients:
                c.close()

    def test_our_own_call_is_not_spotted(self):
        qlog = QlogCluster(port)
        try:
            wait_for(lambda: qlog.poll() or qlog.state == "LOGIN_SENT", 5)
            time.sleep(0.5)
            qlog.poll()
            before = len(qlog.spots)
            player = band.transmit("CQ CQ DE N0CALL N0CALL N0CALL K")
            player.wait(30)
            time.sleep(3)
            qlog.poll()
            self.assertEqual([s["call"] for s in qlog.spots[before:] if s["call"] == "N0CALL"], [])
        finally:
            qlog.close()


if __name__ == "__main__":
    unittest.main(verbosity=2, warnings="ignore")
