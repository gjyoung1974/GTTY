#!/usr/bin/env python3
"""End-to-end test: an operator working RTTY in Not1MM with gtty as the modem.

The real Not1MM main window is started (in this process, with its own empty
configuration and log) next to a private gtty in modem-only view. Both share a
Hamlib dummy radio through rigctld. The test then does what an operator does,
by sending keystrokes to Not1MM's entry fields:

    type a frequency and "RTTY" in the call field      -> gtty follows the radio
    press F1 (CQ)                                       -> gtty keys and sends it
    a station answers (RTTY audio played into gtty)     -> gtty prints the call
    type that call, press F2 and F3, fill the exchange  -> gtty sends them
    press Enter                                         -> the QSO is in Not1MM's log

Nothing is mocked on either side except the port Not1MM's fldigi client uses:
Not1MM hard-codes 7362, so the client is pointed at the test gtty instead.

Run directly, or through ctest (test name: not1mm_gui, opt-in):

    python3 tests/integration/test_not1mm_gui.py
    GTTY_INTEGRATION=1 ctest --test-dir build -R not1mm_gui

What it needs, and where it looks (see also test_not1mm.py):

    Not1MM + PyQt6  a Python that can import not1mm: this one, $NOT1MM_PYTHON,
                    or the uv / pipx tool environment for not1mm. The script
                    re-runs itself under that interpreter.
    gtty, gtty-cli  $GTTY_BIN / $GTTY_CLI, else the build tree
    rigctld         $RIGCTLD, else PATH (Debian: libhamlib-utils). Required.
    a display       for gtty. Not1MM itself runs off-screen unless
                    NOT1MM_VISIBLE=1, so it cannot lose the keyboard focus to
                    whatever else is on the desktop.
    pactl, paplay   PulseAudio / PipeWire tools, to play the answering station
                    into gtty through a temporary virtual sound device. Without
                    them the tests that need received audio are skipped.

Missing prerequisites end the script with status 77 ("skipped" under ctest).
"""

import importlib.util
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_not1mm import REPO, SKIP, Station, find_gtty, skip_all  # noqa: E402

MYCALL = "N0CALL"
DXCALL = "K1ABC"


def not1mm_python():
    """An interpreter that can import not1mm and PyQt6, or None."""
    home = Path.home()
    cands = [os.environ.get("NOT1MM_PYTHON"),
             home / ".local/share/uv/tools/not1mm/bin/python",
             home / ".local/share/pipx/venvs/not1mm/bin/python",
             home / ".local/pipx/venvs/not1mm/bin/python"]
    for c in cands:
        if c and os.access(c, os.X_OK):
            return str(c)
    return None


def ensure_not1mm_importable():
    src = os.environ.get("NOT1MM_SRC")
    if src:
        sys.path.insert(0, src)
    # Only look for the packages here. Importing not1mm.fsutils would fix
    # Not1MM's data paths before setUpModule() has pointed them at a temporary
    # directory, and the test would then write to the operator's real log.
    if importlib.util.find_spec("PyQt6") and importlib.util.find_spec("not1mm"):
        return
    py = not1mm_python()
    if not py or os.environ.get("GTTY_NOT1MM_REEXEC"):
        skip_all("no Python with not1mm and PyQt6 found (set NOT1MM_PYTHON)")
    os.environ["GTTY_NOT1MM_REEXEC"] = "1"
    os.execv(py, [py, *sys.argv])


class VirtualBand:
    """A temporary null sink whose monitor gtty records from."""

    def __init__(self, gtty_pid, gtty_cli):
        self.cli = gtty_cli
        self.name = f"gtty_test_{os.getpid()}"
        self.module = subprocess.run(
            ["pactl", "load-module", "module-null-sink", f"sink_name={self.name}"],
            capture_output=True, text=True, check=True).stdout.strip()
        self.dir = Path(tempfile.mkdtemp(prefix="gtty-band-"))
        try:
            self._route(gtty_pid)
        except Exception:
            self.close()
            raise

    def _route(self, pid):
        end = time.monotonic() + 15
        while time.monotonic() < end:
            out = subprocess.run(["pactl", "-f", "json", "list", "source-outputs"],
                                 capture_output=True, text=True).stdout
            # PipeWire's ALSA streams carry no process id, only the program
            # name. The newest one is the gtty this test just started.
            mine = [so["index"] for so in json.loads(out or "[]")
                    if "[gtty]" in so.get("properties", {}).get("application.name", "")
                    or str(so.get("properties", {}).get("application.process.id")) == str(pid)]
            if mine:
                subprocess.run(["pactl", "move-source-output", str(max(mine)),
                                f"{self.name}.monitor"], check=True)
                return
            time.sleep(0.5)
        raise RuntimeError("gtty's recording stream was not found")

    def transmit(self, text):
        """Play text as RTTY into the band. Returns the player process."""
        wav = self.dir / "msg.wav"
        subprocess.run([self.cli, "encode", text, str(wav)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return subprocess.Popen(["paplay", f"--device={self.name}", str(wav)])

    def close(self):
        subprocess.run(["pactl", "unload-module", self.module], capture_output=True)
        shutil.rmtree(self.dir, ignore_errors=True)


station = None
band = None
window = None
n1 = None        # the not1mm.__main__ module
tmp = None
QTest = Qt = None


def seed_not1mm(rig_port):
    """A station, one CQ WW RTTY contest and the radio settings, as the first-run dialogs would leave them."""
    from not1mm import fsutils
    from not1mm.lib.database import DataBase

    for path in (fsutils.USER_DATA_PATH, fsutils.CONFIG_FILE):
        if tmp not in Path(path).parents:
            raise RuntimeError(f"Not1MM would use {path}, outside the test directory {tmp}; refusing to run")
    db = DataBase(str(fsutils.USER_DATA_PATH / "ham.db"), fsutils.APP_DATA_PATH)
    db.add_station({
        "Call": MYCALL, "Name": "Test", "Email": "", "Street1": "", "Street2": "", "City": "",
        "State": "CT", "Zip": "", "Country": "United States", "GridSquare": "FN31pr",
        "LicenseClass": "", "Latitude": 41.7, "Longitude": -72.7, "PacketNode": "N/A",
        "ARRLSection": "CT", "Club": "", "IARUZone": 8, "CQZone": 5, "STXeq": "", "SPowe": "",
        "SAnte": "", "SAntH1": "", "SAntH2": "", "RoverQTH": ""})
    db.add_contest({
        "ContestName": "cq_ww_rtty", "StartDate": "2026-01-01 00:00:00",
        "OperatorCategory": "SINGLE-OP", "BandCategory": "ALL", "PowerCategory": "LOW",
        "ModeCategory": "RTTY", "OverlayCategory": "N/A", "Operators": MYCALL, "Soapbox": "",
        "SentExchange": "5 CT", "ContestNR": 1, "SubType": None, "StationCategory": "FIXED",
        "AssistedCategory": "ASSISTED", "TransmitterCategory": "ONE", "TimeCategory": None})
    fsutils.CONFIG_FILE.write_text(json.dumps({
        "contest": 1, "current_database": "ham.db", "current_op": MYCALL, "run_state": True,
        "userigctld": True, "useflrig": False, "usetci": False,
        "CAT_ip": "127.0.0.1", "CAT_port": rig_port, "cwtype": 0, "use_esm": False,
        "send_n1mm_packets": False, "useqrz": False, "usehamdb": False, "usehamqth": False,
        "bandmapwindow": False, "logwindow": False, "checkwindow": False, "clusterwindow": False,
        "ratewindow": False, "statisticswindow": False, "vfowindow": False}))


def setUpModule():
    global station, band, window, n1, tmp, QTest, Qt
    if os.environ.get("GTTY_CTEST") and not os.environ.get("GTTY_INTEGRATION"):
        skip_all("integration tests are opt-in under ctest (set GTTY_INTEGRATION=1)")
    gtty_bin = find_gtty()
    if not gtty_bin:
        skip_all("gtty binary not found (set GTTY_BIN)")
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip_all("no display for gtty")
    rigctld = os.environ.get("RIGCTLD") or shutil.which("rigctld")
    if not rigctld:
        skip_all("rigctld not found (set RIGCTLD, or install libhamlib-utils)")
    ensure_not1mm_importable()

    station = Station(gtty_bin, rigctld)
    try:
        cli = os.environ.get("GTTY_CLI") or str(Path(gtty_bin).parents[1] / "cli" / "gtty-cli")
        if shutil.which("pactl") and shutil.which("paplay") and os.access(cli, os.X_OK):
            try:
                band = VirtualBand(station.gtty.pid, cli)
            except Exception as e:  # no PulseAudio/PipeWire server, or gtty is not recording
                print(f"note: no virtual band, receive tests will be skipped ({e})")

        # Not1MM reads these when it is imported, so they come first.
        tmp = Path(tempfile.mkdtemp(prefix="not1mm-gtty-"))
        os.environ["XDG_CONFIG_HOME"] = str(tmp / "config")
        os.environ["XDG_DATA_HOME"] = str(tmp / "data")
        if not os.environ.get("NOT1MM_VISIBLE"):
            os.environ["QT_QPA_PLATFORM"] = "offscreen"
        seed_not1mm(station.rig_port)

        from not1mm.lib import fldigi_sendstring, versiontest
        versiontest.VersionTest.test = lambda self: False      # no update check over the network
        real_init = fldigi_sendstring.FlDigi_Comm.__init__

        def init(self):                                        # Not1MM hard-codes port 7362
            real_init(self)
            self.target = station.url
        fldigi_sendstring.FlDigi_Comm.__init__ = init

        import not1mm.__main__ as main
        from PyQt6.QtCore import Qt as _Qt
        from PyQt6.QtTest import QTest as _QTest
        n1, Qt, QTest = main, _Qt, _QTest
        window = main.MainWindow(main.splash)
        window.show()
        window.activateWindow()
        window.callsign.setFocus()
        QTest.qWait(500)
    except BaseException:
        tearDownModule()
        raise


def tearDownModule():
    global window
    if window is not None:
        try:
            window.close()
            QTest.qWait(300)
        except Exception:
            pass
        window = None
    if band:
        band.close()
    if station:
        station.stop()
    if tmp:
        shutil.rmtree(tmp, ignore_errors=True)


def pump_until(cond, timeout, step=50):
    """Run Not1MM's event loop until cond() is truthy; return that, or None on time-out."""
    end = time.monotonic() + timeout
    while True:
        v = cond()
        if v or time.monotonic() >= end:
            return v or None
        QTest.qWait(step)


def type_in(widget, text):
    widget.setFocus()
    QTest.keyClicks(widget, text)
    QTest.qWait(50)


def press(key, widget=None):
    QTest.keyClick(widget or window.focusWidget() or window.callsign, key)
    QTest.qWait(50)


def logged_qsos():
    from not1mm import fsutils
    with sqlite3.connect(fsutils.USER_DATA_PATH / "ham.db") as c:
        c.row_factory = sqlite3.Row
        return [dict(r) for r in c.execute("select * from DXLOG order by TS")]


class OperatorSession(unittest.TestCase):
    """One sitting at the radio. The steps run in order and build on each other."""

    def setUp(self):
        self.sent_before = len(station.sent_text())

    def new_sent_text(self):
        return station.sent_text()[self.sent_before:]

    def assert_gtty_sends(self, needle, timeout=40):
        """gtty keys up, sends text containing needle, and returns to receive by itself."""
        self.assertTrue(pump_until(lambda: station.trx() == "TX", 3), "gtty did not go to transmit")
        self.assertTrue(pump_until(lambda: station.trx() == "RX", timeout), "gtty stayed in transmit")
        self.assertTrue(pump_until(lambda: needle in self.new_sent_text(), 10, 250),
                        f"{needle!r} not in what gtty sent: {self.new_sent_text()!r}")

    def test_01_not1mm_starts_with_the_contest_and_the_radio(self):
        self.assertEqual(window.contest.name, "CQ WW RTTY")
        self.assertTrue(pump_until(lambda: window.rig_control and window.rig_control.online, 10),
                        "Not1MM is not connected to rigctld")
        self.assertEqual(station.rpc().fldigi.name(), "gtty")

    def test_02_typing_a_frequency_moves_the_radio_and_gtty(self):
        type_in(window.callsign, "14085 ")
        self.assertTrue(pump_until(lambda: station.rpc().main.get_frequency() == 14085000, 5),
                        "gtty did not follow Not1MM's QSY")
        self.assertTrue(pump_until(lambda: str(window.radio_state.get("vfoa")) == "14085000", 5))
        self.assertEqual(window.callsign.text(), "")

    def test_03_typing_rtty_puts_the_radio_and_not1mm_in_rtty(self):
        window.rig_control.cat.set_mode("CW")       # as if the radio had been left in CW
        self.assertTrue(pump_until(lambda: window.radio_state.get("mode") == "CW", 5))
        type_in(window.callsign, "RTTY ")
        self.assertTrue(pump_until(lambda: window.radio_state.get("mode") == "RTTY", 5),
                        f"Not1MM mode is {window.radio_state.get('mode')}")
        self.assertTrue(pump_until(lambda: station.rpc().rig.get_mode() == "RTTY", 5),
                        "gtty does not see the radio in RTTY")

    def test_04_f1_calls_cq_through_gtty(self):
        ptt = []
        press(Qt.Key.Key_F1, window.callsign)
        self.assertTrue(pump_until(lambda: station.trx() == "TX", 3), "gtty did not go to transmit")
        ptt.append(pump_until(lambda: window.rig_control.cat.get_ptt() == "1", 3))
        self.assert_gtty_sends(f"CQ TEST {MYCALL} {MYCALL} TEST")
        self.assertTrue(ptt[0], "the radio was not keyed during the CQ")
        self.assertTrue(pump_until(lambda: window.rig_control.cat.get_ptt() == "0", 3), "the radio stayed keyed")

    def test_05_gtty_prints_the_station_that_answers(self):
        if not band:
            self.skipTest("no virtual sound device (needs pactl and paplay)")
        station.rpc().text.clear_rx()
        player = band.transmit(f"{MYCALL} {MYCALL} DE {DXCALL} {DXCALL} {DXCALL}")

        def heard():
            rpc = station.rpc()
            data = rpc.text.get_rx(0, rpc.text.get_rx_length())
            return DXCALL in getattr(data, "data", data).decode("ascii", "replace")
        ok = pump_until(heard, 30, 250)
        player.wait(30)
        self.assertTrue(ok, "gtty did not decode the answering station")

    def test_06_working_the_station_sends_call_and_exchange(self):
        type_in(window.callsign, DXCALL)
        press(Qt.Key.Key_F2)                                   # his call
        self.assert_gtty_sends(DXCALL)
        self.sent_before = len(station.sent_text())
        press(Qt.Key.Key_F3)                                   # our report and exchange
        self.assert_gtty_sends("599")

    def test_07_enter_logs_the_qso_in_not1mm(self):
        self.assertEqual(window.callsign.text(), DXCALL)
        press(Qt.Key.Key_Space, window.callsign)               # on to the exchange fields
        # Not1MM fills in the CQ zone for the call; the operator adds the state.
        self.assertEqual(window.other_1.text(), "5")
        type_in(window.other_2, "MA")
        press(Qt.Key.Key_Return, window.other_2)
        qsos = pump_until(logged_qsos, 5)
        self.assertTrue(qsos, "nothing was logged")
        self.assertEqual(len(qsos), 1)
        q = qsos[0]
        self.assertEqual(q["Call"], DXCALL)
        self.assertEqual(q["Mode"], "RTTY")
        self.assertEqual(q["Band"], 14.0)
        self.assertAlmostEqual(float(q["Freq"]), 14085.0, places=1)
        self.assertEqual(str(q["ZN"]), "5")
        self.assertEqual(window.callsign.text(), "", "the entry fields were not cleared for the next QSO")

    def test_08_gtty_is_back_in_receive_and_ready(self):
        self.assertEqual(station.trx(), "RX")
        press(Qt.Key.Key_F1, window.callsign)                  # next CQ
        self.assert_gtty_sends(f"CQ TEST {MYCALL}")


if __name__ == "__main__":
    unittest.main(verbosity=2, warnings="ignore")
