#!/usr/bin/env python3
"""Integration tests: a QSO logged in gtty as other logbooks receive it.

gtty reports a logged QSO two ways:

  * WSJT-X UDP protocol: a "QSO Logged" datagram (type 5) and a "Logged ADIF"
    datagram (type 12). QLog, KLog and Wavelog (through WaveLogGate) log from
    these.
  * fldigi's logbook interface: an XML-RPC log.add_record call carrying the
    QSO as an ADIF record, as fldigi sends to fllog. QLog listens for it on
    port 8421.

None of those programs can be loaded here, so the readers below repeat their
code, including the places where they are strict:

    QLog         core/WsjtxUDPReceiver.cpp: QDataStream >> QByteArray, QDateTime;
                 logformat/AdiFormat.cpp: ADIF text has a header only when it
                 does not start with '<';
                 core/FldigiTCPServer.cpp: one read, the record is the text of
                 the first <value> element, child elements are an error
    KLog         src/udpserver.cpp: QDataStream version Qt 5.4, same field order
    WaveLogGate  internal/udp/wsjtx.go: schema 2 or 3, UTC time stamps only,
                 callsign, frequency and mode required; internal/adif/parser.go

A private gtty is started with those outputs pointed at the test, a QSO is
filled in over XML-RPC and logged with a macro, and each reader must come up
with the same QSO. If one of the programs changes its reader, update the copy.

Run directly, or through ctest (test name: log_forwarding, opt-in):

    python3 tests/integration/test_log_forwarding.py
    GTTY_INTEGRATION=1 ctest --test-dir build -R log_forwarding

Needs the gtty binary and a display (see test_not1mm.py).
"""

import datetime
import os
import re
import socket
import struct
import sys
import threading
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_not1mm import Station, find_gtty, free_port, skip_all, wait_for  # noqa: E402

MAGIC = 0xADBCCBDA
UTC = datetime.timezone.utc


# ----------------------------------------------------------------- QDataStream

class QDataStream:
    """The parts of Qt's QDataStream the loggers use, big-endian."""

    def __init__(self, data):
        self.data, self.pos, self.ok = data, 0, True

    def _take(self, n):
        if self.pos + n > len(self.data):
            self.ok = False                            # QDataStream::ReadPastEnd: zeros from here on
            return b"\0" * n
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def u8(self):
        return self._take(1)[0]

    def u32(self):
        return struct.unpack(">I", self._take(4))[0]

    def u64(self):
        return struct.unpack(">Q", self._take(8))[0]

    def i64(self):
        return struct.unpack(">q", self._take(8))[0]

    def bytearray(self):
        """operator>>(QByteArray&): 0xffffffff is a null array."""
        n = self.u32()
        if n == 0xFFFFFFFF:
            return b""
        return self._take(n) if self.ok else b""

    def datetime(self):
        """operator>>(QDateTime&), stream version >= Qt 5.2: QDate, QTime, time spec."""
        jd, ms, spec = self.i64(), self.u32(), self.u8()
        if spec == 2:                                  # Qt::OffsetFromUTC carries the offset
            struct.unpack(">i", self._take(4))
        if spec == 3:                                  # Qt::TimeZone is not something gtty may send
            self.ok = False
        if not self.ok or ms >= 86400000:
            return None, spec
        day = datetime.datetime(1970, 1, 1, tzinfo=UTC) + datetime.timedelta(days=jd - 2440588, milliseconds=ms)
        return day, spec

    def at_end(self):
        return self.pos == len(self.data)


def header(data):
    s = QDataStream(data)
    magic, schema, mtype = s.u32(), s.u32(), s.u32()
    return s, magic, schema, mtype


# ------------------------------------------------------------------------ QLog

def qlog_read_adif(text):
    """AdiFormat::readField/readContact. Returns (contact, header_fields) or (None, ...) without <eor>."""
    contact, headers = {}, {}
    i, n = 0, len(text)
    in_header = None
    while i < n:
        if in_header is None:                          # state START
            in_header = text[i] != "<"
            if not in_header:
                i += 1
            else:
                while i < n and text[i] != "<":
                    i += 1
                i += 1
        else:                                          # state FIELD: skip to the next tag
            while i < n and text[i] != "<":
                i += 1
            i += 1
        end = text.find(">", i)
        if end < 0:
            break
        parts = text[i:end].split(":")
        i = end + 1
        field = parts[0].lower()
        if len(parts) == 1:
            if in_header and field == "eoh":
                in_header = False
                continue
            if field == "eor":
                return contact, headers
            if field:
                contact[field] = ""
            continue
        length = int(parts[1] or 0)
        value = text[i:i + length]
        i += length
        (headers if in_header else contact)[field] = value
    return None, headers


def qlog_qso_logged(data):
    """case 5 of WsjtxUDPReceiver::readPendingDatagrams plus insertContact(WsjtxLog)."""
    s, magic, schema, mtype = header(data)
    assert magic == MAGIC and mtype == 5
    ident = s.bytearray()
    time_off, _ = s.datetime()
    dx_call, dx_grid = s.bytearray(), s.bytearray()
    tx_freq = s.u64()
    mode, rprt_sent, rprt_rcvd, tx_pwr, comments, name = (s.bytearray() for _ in range(6))
    time_on, _ = s.datetime()
    op_call, my_call, my_grid, exch_sent, exch_rcvd, prop_mode = (s.bytearray() for _ in range(6))
    rec = {"id": ident.decode(), "freq": tx_freq / 1e6}
    for key, raw, up in (("callsign", dx_call, True), ("rst_rcvd", rprt_rcvd, False), ("rst_sent", rprt_sent, False),
                         ("name", name, False), ("gridsquare", dx_grid, True), ("mode", mode, True),
                         ("comment", comments, False), ("stx_string", exch_sent, False),
                         ("srx_string", exch_rcvd, False), ("prop_mode", prop_mode, True), ("tx_pwr", tx_pwr, False),
                         ("operator", op_call, True), ("my_gridsquare", my_grid, True),
                         ("station_callsign", my_call, True)):
        v = raw.decode("latin-1").strip() if up else raw.decode("latin-1")
        if v:                                          # empty fields stay NULL
            rec[key] = v.upper() if up else v
    if time_on:
        rec["start_time"] = time_on
    if time_off:
        rec["end_time"] = time_off
    return rec, s


def qlog_logged_adif(data):
    """case 12 plus insertContact(WsjtxLogADIF)."""
    s, magic, schema, mtype = header(data)
    assert magic == MAGIC and mtype == 12
    ident, adif = s.bytearray(), s.bytearray()
    contact, headers = qlog_read_adif(adif.decode("latin-1"))
    return ident.decode(), contact, headers


class QlogFldigiServer(threading.Thread):
    """QLog's FldigiTCPServer: log.add_record over XML-RPC, port 8421 in QLog."""

    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(4)
        self.sock.settimeout(0.2)
        self.port = self.sock.getsockname()[1]
        self.contacts, self.errors, self.running = [], [], True

    def run(self):
        while self.running:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                continue
            conn.settimeout(2)
            try:
                time.sleep(0.05)
                data = conn.recv(65536).decode("latin-1")   # readAll() on the first readyRead
            except OSError:
                data = ""
            xml = data[data.find("\r\n\r\n") + 4:]
            reply = None
            m = re.search(r"<methodName>(.*?)</methodName>", xml)
            if m and m.group(1) == "log.add_record":
                v = re.search(r"<value>(.*?)</value>", xml, re.S)
                # readElementText(): a child element (<string>) is an error and yields nothing.
                if v and "<" not in v.group(1):
                    text = v.group(1).replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"').replace("&amp;", "&")
                    contact, _ = qlog_read_adif(text)
                    if contact is not None:
                        self.contacts.append(contact)
                        reply = '<?xml version="1.0" encoding="UTF-8"?><methodResponse><params><param><value/></param></params></methodResponse>'
                    else:
                        self.errors.append("ADIF record without <eor>: " + text)
                else:
                    self.errors.append("no usable <value> in: " + xml)
            else:
                self.errors.append("unexpected request: " + data)
            if reply:
                out = f"HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: {len(reply)}\r\n\r\n{reply}"
            else:
                out = "HTTP/1.1 400 Internal Error\r\n"
            try:
                conn.sendall(out.encode())
            except OSError:
                pass
            conn.close()

    def stop(self):
        self.running = False
        self.join(2)
        self.sock.close()


# ------------------------------------------------------------------------ KLog

def klog_qso_logged(data):
    """UDPServer::parse(), case QSOLogged."""
    s, magic, schema, mtype = header(data)
    ident = s.bytearray()
    assert magic == 2914831322 and mtype == 5
    time_off, _ = s.datetime()
    dx_call, dx_grid = s.bytearray(), s.bytearray()
    frequency = s.u64()
    mode, report_sent, report_received, tx_power, comments, name = (s.bytearray() for _ in range(6))
    time_on, _ = s.datetime()
    operator_call, de_call, de_grid, exchange_sent, exchange_received = (s.bytearray() for _ in range(5))
    txt = lambda b: b.decode("latin-1")
    return {"call": txt(dx_call), "mode": txt(mode), "freq": frequency / 1000000, "gridsquare": txt(dx_grid),
            "my_gridsquare": txt(de_grid), "rst_rx": txt(report_received), "rst_tx": txt(report_sent),
            "comment": txt(comments), "station_callsign": txt(de_call), "name": txt(name),
            "operator": txt(operator_call), "datetime_on": time_on, "time_off": time_off,
            "srx_string": txt(exchange_received), "stx_string": txt(exchange_sent)}, s


# ----------------------------------------------------------------- WaveLogGate

FIELD_RE = re.compile(r"<([^:>]+):(\d+)(?::[^>]*)?>", re.I)


def wavelog_adif_parse(adif):
    """adif.Parse()."""
    out = {}
    for m in FIELD_RE.finditer(adif):
        name = m.group(1).upper()
        if name not in ("EOR", "EOH"):
            out[name] = adif[m.end():m.end() + int(m.group(2))]
    return out


def wavelog_parse(data):
    """parseWSJTXQSO(): returns (fields, message type) or raises ValueError as the Go code returns an error."""
    if len(data) < 4 or struct.unpack(">I", data[:4])[0] != MAGIC:
        return None, 0
    schema, mtype = struct.unpack(">II", data[4:12])
    if mtype not in (5, 12):
        return None, mtype
    if schema not in (2, 3):
        raise ValueError(f"unsupported WSJT-X schema {schema}")
    pos = 12

    def read_string():
        nonlocal pos
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        pos += 4
        if n == 0xFFFFFFFF:
            return ""
        if n > len(data) - pos:
            raise ValueError("unexpected EOF")
        pos += n
        return data[pos - n:pos].decode("latin-1")

    read_string()                                      # client id
    if mtype == 12:
        fields = wavelog_adif_parse(read_string())
        fields.pop("ADIF_VER", None)
        fields.pop("PROGRAMID", None)
        if not fields:
            raise ValueError("logged ADIF message carries no QSO fields")
        return fields, mtype
    fields = {}

    def read_date(date_field, time_field):
        nonlocal pos
        jd, ms, spec = struct.unpack(">qIB", data[pos:pos + 13])
        pos += 13
        if spec != 1 or jd < 1721426 or jd > 5373484 or ms >= 86400000:
            raise ValueError("invalid or non-UTC WSJT-X timestamp")
        stamp = datetime.datetime.fromtimestamp((jd - 2440588) * 86400 + ms / 1000, UTC)
        fields[date_field], fields[time_field] = stamp.strftime("%Y%m%d"), stamp.strftime("%H%M%S")

    def read_fields(*names):
        for name in names:
            value = read_string()
            if value:
                fields[name] = value

    read_date("QSO_DATE_OFF", "TIME_OFF")
    read_fields("CALL", "GRIDSQUARE")
    (frequency,) = struct.unpack(">Q", data[pos:pos + 8])
    pos += 8
    fields["FREQ"] = f"{frequency / 1e6:.6f}"
    read_fields("MODE", "RST_SENT", "RST_RCVD", "TX_PWR", "COMMENT", "NAME")
    read_date("QSO_DATE", "TIME_ON")
    read_fields("OPERATOR", "STATION_CALLSIGN", "MY_GRIDSQUARE")
    if not fields.get("CALL") or frequency == 0 or not fields.get("MODE"):
        raise ValueError("QSO logged message is missing callsign, frequency or mode")
    return fields, mtype


def wavelog_key(fields):
    """qsoKey(): the two datagrams of one QSO must give the same key, or Wavelog gets the QSO twice."""
    return "|".join((fields.get("CALL", ""), fields.get("BAND", ""), fields.get("SUBMODE") or fields.get("MODE", ""),
                     fields.get("STATION_CALLSIGN", ""))).upper()


def wavelog_band(mhz):
    for lo, hi, band in ((1.8, 2.0, "160m"), (3.5, 4.0, "80m"), (7.0, 7.3, "40m"), (10.1, 10.15, "30m"),
                         (14.0, 14.35, "20m"), (18.068, 18.168, "17m"), (21.0, 21.45, "15m"),
                         (24.89, 24.99, "12m"), (28.0, 29.7, "10m")):
        if lo <= mhz <= hi:
            return band
    return ""


# ----------------------------------------------------------------------- tests

PROFILE_EXTRA = """
[gridtracker]
enabled=true
host=127.0.0.1
port={udp_port}
send_decodes=true
send_logged=true

[fllog]
enabled=true
host=127.0.0.1
port={fllog_port}

[station]
call=N0CALL
grid=EN82

[macros]
label_1=LOG
text_1=<LOG>
"""

station = None
udp = None
book = None
datagrams = []          # every datagram gtty sent
qso = {}                # the two datagrams of the logged QSO, by message type
logged_at = None


def collect(timeout=0.3):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            datagrams.append(udp.recv(65536))
        except OSError:
            pass


def setUpModule():
    global station, udp, book, logged_at
    if os.environ.get("GTTY_CTEST") and not os.environ.get("GTTY_INTEGRATION"):
        skip_all("integration tests are opt-in under ctest (set GTTY_INTEGRATION=1)")
    gtty_bin = find_gtty()
    if not gtty_bin:
        skip_all("gtty binary not found (set GTTY_BIN)")
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip_all("no display for gtty")
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(("127.0.0.1", 0))
    udp.settimeout(0.1)
    book = QlogFldigiServer()
    book.start()
    station = Station(gtty_bin, None, PROFILE_EXTRA.format(udp_port=udp.getsockname()[1], fllog_port=book.port))

    # Fill in the log row the way a logger or the operator would, and log it.
    rpc = station.rpc()
    rpc.rig.set_frequency(14083500.0)
    rpc.log.set_call("dl1abc")
    rpc.log.set_name("Hans")
    rpc.log.set_rst_in("579")
    rpc.log.set_rst_out("599")
    rpc.log.set_exchange("14 & <DL>")
    rpc.log.set_serial_number(12)
    rpc.log.set_locator("JO62")
    collect(0.5)
    logged_at = datetime.datetime.now(UTC)
    rpc.main.run_macro(0)
    wait_for(lambda: collect() or len([d for d in datagrams if header(d)[3] in (5, 12)]) >= 2, 10)
    wait_for(lambda: book.contacts or book.errors, 10)
    for d in datagrams:
        qso.setdefault(header(d)[3], d)


def tearDownModule():
    if station:
        station.stop()
    if book:
        book.stop()
    if udp:
        udp.close()


class Wire(unittest.TestCase):
    def test_gtty_sent_both_forms_of_the_qso(self):
        self.assertIn(5, qso, "no QSO Logged datagram")
        self.assertIn(12, qso, "no Logged ADIF datagram")
        types = [header(d)[3] for d in datagrams]
        self.assertIn(0, types, "no heartbeat")
        self.assertIn(1, types, "no status")
        self.assertEqual(len([t for t in types if t == 5]), 1)
        self.assertEqual(len([t for t in types if t == 12]), 1)


class QLog(unittest.TestCase):
    def test_qso_logged(self):
        rec, stream = qlog_qso_logged(qso[5])
        self.assertTrue(stream.ok and stream.at_end(), "QLog would read past the end, or leave bytes over")
        self.assertEqual(rec["id"], "gtty")
        self.assertEqual(rec["callsign"], "DL1ABC")
        self.assertAlmostEqual(rec["freq"], 14.0835, places=6)
        self.assertEqual(rec["mode"], "RTTY")
        self.assertEqual(rec["rst_sent"], "599")
        self.assertEqual(rec["rst_rcvd"], "579")
        self.assertEqual(rec["name"], "Hans")
        self.assertEqual(rec["gridsquare"], "JO62")
        self.assertEqual(rec["stx_string"], "012")
        self.assertEqual(rec["srx_string"], "14 & <DL>")
        self.assertEqual(rec["station_callsign"], "N0CALL")
        self.assertEqual(rec["operator"], "N0CALL")
        self.assertEqual(rec["my_gridsquare"], "EN82")
        self.assertNotIn("prop_mode", rec)
        for key in ("start_time", "end_time"):
            self.assertLess(abs((rec[key] - logged_at).total_seconds()), 5, key)

    def test_logged_adif(self):
        ident, contact, headers = qlog_logged_adif(qso[12])
        self.assertEqual(ident, "gtty")
        self.assertIsNotNone(contact, "QLog found no <eor>")
        # The header tags must be read as a header, not as fields of the contact.
        self.assertEqual(headers.get("programid"), "gtty")
        self.assertIn("adif_ver", headers)
        for stray in ("adif_ver", "programid", "eoh"):
            self.assertNotIn(stray, contact)
        self.assertEqual(contact["call"], "DL1ABC")
        self.assertEqual(contact["mode"], "RTTY")
        self.assertEqual(contact["band"], "20m")
        self.assertEqual(contact["freq"], "14.083500")
        self.assertEqual(contact["rst_sent"], "599")
        self.assertEqual(contact["rst_rcvd"], "579")
        self.assertEqual(contact["stx_string"], "012")
        self.assertEqual(contact["srx_string"], "14 & <DL>")
        self.assertEqual(contact["gridsquare"], "JO62")
        self.assertEqual(contact["station_callsign"], "N0CALL")
        self.assertEqual(contact["qso_date"], logged_at.strftime("%Y%m%d"))
        self.assertRegex(contact["time_on"], r"^\d{6}$")

    def test_both_datagrams_describe_one_contact(self):
        # QLog merges the two messages of one client into a single contact.
        rec, _ = qlog_qso_logged(qso[5])
        _, contact, _ = qlog_logged_adif(qso[12])
        self.assertEqual(rec["callsign"], contact["call"])
        self.assertEqual(f"{rec['freq']:.6f}", contact["freq"])
        self.assertEqual(rec["start_time"].strftime("%Y%m%d %H%M%S"), contact["qso_date"] + " " + contact["time_on"])

    def test_fldigi_logbook_interface(self):
        self.assertEqual(book.errors, [])
        self.assertEqual(len(book.contacts), 1, "QLog's fldigi server did not get exactly one record")
        c = book.contacts[0]
        self.assertEqual(c["call"], "DL1ABC")
        self.assertEqual(c["mode"], "RTTY")
        self.assertEqual(c["freq"], "14.083500")
        self.assertEqual(c["band"], "20m")
        self.assertEqual(c["rst_sent"], "599")
        self.assertEqual(c["rst_rcvd"], "579")
        self.assertEqual(c["name"], "Hans")
        self.assertEqual(c["stx_string"], "012")
        self.assertEqual(c["srx_string"], "14 & <DL>")
        self.assertEqual(c["gridsquare"], "JO62")
        self.assertEqual(c["station_callsign"], "N0CALL")
        self.assertEqual(c["qso_date"], logged_at.strftime("%Y%m%d"))


class KLog(unittest.TestCase):
    def test_qso_logged(self):
        rec, stream = klog_qso_logged(qso[5])
        self.assertTrue(stream.ok, "KLog would read past the end")
        self.assertEqual(rec["call"], "DL1ABC")
        self.assertEqual(rec["mode"], "RTTY")
        self.assertAlmostEqual(rec["freq"], 14.0835, places=6)
        self.assertEqual(rec["rst_tx"], "599")
        self.assertEqual(rec["rst_rx"], "579")
        self.assertEqual(rec["name"], "Hans")
        self.assertEqual(rec["gridsquare"], "JO62")
        self.assertEqual(rec["my_gridsquare"], "EN82")
        self.assertEqual(rec["station_callsign"], "N0CALL")
        self.assertEqual(rec["stx_string"], "012")
        self.assertEqual(rec["srx_string"], "14 & <DL>")
        self.assertLess(abs((rec["datetime_on"] - logged_at).total_seconds()), 5)
        self.assertIsNotNone(rec["time_off"])

    def test_status_gives_klog_the_band(self):
        # KLog takes the frequency of what it shows from the Status message.
        status = [d for d in datagrams if header(d)[3] == 1]
        s, _, _, _ = header(status[-1])
        s.bytearray()
        self.assertEqual(s.u64(), 14083500)
        self.assertEqual(s.bytearray(), b"RTTY")


class WaveLogGate(unittest.TestCase):
    def test_qso_logged(self):
        fields, mtype = wavelog_parse(qso[5])
        self.assertEqual(mtype, 5)
        self.assertEqual(fields["CALL"], "DL1ABC")
        self.assertEqual(fields["FREQ"], "14.083500")
        self.assertEqual(fields["MODE"], "RTTY")
        self.assertEqual(fields["RST_SENT"], "599")
        self.assertEqual(fields["RST_RCVD"], "579")
        self.assertEqual(fields["STATION_CALLSIGN"], "N0CALL")
        self.assertEqual(fields["MY_GRIDSQUARE"], "EN82")
        self.assertEqual(fields["QSO_DATE"], logged_at.strftime("%Y%m%d"))

    def test_logged_adif(self):
        fields, mtype = wavelog_parse(qso[12])
        self.assertEqual(mtype, 12)
        self.assertNotIn("ADIF_VER", fields)
        self.assertEqual(fields["CALL"], "DL1ABC")
        self.assertEqual(fields["BAND"], "20m")
        self.assertEqual(fields["SRX_STRING"], "14 & <DL>")
        self.assertEqual(fields["STX_STRING"], "012")

    def test_the_two_datagrams_are_one_qso_to_the_dedupe(self):
        raw, _ = wavelog_parse(qso[5])
        raw.setdefault("BAND", wavelog_band(float(raw["FREQ"])))   # "band enriched from freq"
        adif, _ = wavelog_parse(qso[12])
        self.assertEqual(wavelog_key(raw), wavelog_key(adif))
        self.assertEqual(wavelog_key(adif), "DL1ABC|20M|RTTY|N0CALL")


if __name__ == "__main__":
    unittest.main(verbosity=2, warnings="ignore")
