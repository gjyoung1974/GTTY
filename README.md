# gtty : amateur radio RTTY for the Linux and macOS

gtty is a GTK4/libadwaita RTTY terminal with a modern contest-style feature set.
It builds and runs natively on Linux and on macOS (Apple silicon and Intel).  

**User manual: <https://github.com/gjyoung1974/GTTY/wiki>** (source in [`docs/`](docs/), also reachable from Help in the application menu or **Ctrl+F1**).

## Why another RTTY Terminal?  
I (WA8Q) wanted to get into RTTY, and I live on Linux. The field here is thin. fldigi is excellent and covers dozens of modes, but as a general-purpose multimode program its RTTY workflow is a compromise, and its GTK 2 interface is showing its age. The programs RTTY contesters actually reach for, MMTTY, 2Tone and Gritty, are Windows only, and running them under Wine with a sound card and a serial port is not something I wanted to depend on in the middle of a contest.

So Gtty is the program I wanted to use: one mode done properly, on a current Linux desktop. That meant a GTK4/libadwaita interface that looks like it belongs in 2026, including dark mode; a contest-style layout with band buttons, macros and the log row always in view; a demodulator written from the published MMTTY design rather than a port of someone else's code; and the ideas that make the Windows tools good, several decoders running in parallel with a vote on their output, a Gritty-style correlator, and a skimmer that decodes the whole passband at once. True FSK keying through an ordinary USB serial adapter, Hamlib rig control, and GridTracker and XML-RPC logger integration round it out, because a terminal that does not fit into the rest of the station does not get used.  

![gtty in dark mode decoding a 20 m RTTY band: waterfall with the shaded tone window, XY scope, best-guess vote pane over the four decoder engines with highlighted callsigns, the Skimmer list of stations found across the passband, band buttons, macros and the log entry row](docs/screenshot.png)

Features:

- **Four decoder engines in parallel** on the same audio, each with its own RX
  pane: two MMTTY-style matched-filter decoders (A and B) with selectable
  **Normal / Selective / Flutter** modes or a custom filter length, window and ATC
  time constant; a **joint-timing character correlator** in the Gritty style that
  correlates whole 7-bit characters against all 32 Baudot templates and picks the
  best start instant; and, when built with libcodec2, that library's FSK
  demodulator as a comparison engine.
- **Best-guess pane**: the engines' characters are aligned in time and each one is
  decided by a confidence-weighted vote. The merged stream is what GridTracker,
  XML-RPC and the session file see.
- **Skimmer**: a decoder every 50 Hz across the waterfall decodes every RTTY signal
  in the passband; stations appear in the sidebar's Skimmer list with callsign and
  RF frequency, click one to tune the decoders to it and take the call.
- **Contest-grade demodulator** under all of it: complex mixers at mark and space,
  matched filters, per-tone automatic threshold control (ATC), soft start/stop-bit
  integration, AFC from a baseband frequency discriminator, squelch.
- **Waterfall + spectrum** that follows the rig's sideband, so in RTTY-L / LSB it
  reads the same way as the rig's own scope (switchable in Preferences), with
  mark/space cursors, click or drag to tune (grab the
  shaded tone window and slide it), mouse wheel nudges 10 Hz, right-click to
  retune the rig so the signal lands on the nominal tones, and a classic
  crossed-ellipse **XY tuning scope**.
- **Transmit**: AFSK audio, true FSK via a serial UART (custom 45.45 baud,
  5N2 on TXD), FSK via DTR/RTS bit-banging, or the rig's own CAT text keyer
  (Elecraft K3/K4/KX3, Kenwood TS-890). Type-ahead TX buffer, diddle, LTRS
  preamble, PTT lead time, "Net" (TX follows the AFC-tracked RX tones).
- **Band buttons** for the customary RTTY segments on 160–6 m (editable in
  Preferences ==> Bands); they QSY the rig, or set the manual dial when no rig is connected.
- **Hamlib** rig control: frequency/mode display, PTT (CAT, DTR, RTS), TX mode
  switching, NET rigctld support.
- **Logging**: SQLite logbook, ADIF export, dupe check, Super Check Partial
  (MASTER.SCP) with callsign highlighting in the RX text, click a word to fill
  the call / RST / exchange fields.
- **Operating conveniences**: Auto CQ repeat with a configurable pause, word-mode
  type-ahead, clear-TX-on-RX, timestamps on received lines, a daily session file of
  everything received and sent, and up to four manual **notch filters** (middle-click
  the waterfall to place or remove one).
- **Logger integration** through an **fldigi-compatible XML-RPC server** (port 7362):
  CQRLOG, TLF and anything else that drives fldigi can read decoded text, send text,
  key TX/RX, set the frequency and fill the log fields.
- **Light / dark appearance** (follow system, light, or dark) under Preferences ==> Station.
- **DX cluster band map**: connect to a DXSpider / AR-Cluster / CC Cluster node
  (Preferences ==> Cluster), RTTY spots fill a band map beside the main window,
  colour-coded as new, worked or dupe from your log; click a spot to QSY and take
  the call. A command line sends cluster commands (filters, `sh/dx`).
- **Callbook lookup and worked-before**: typing a complete callsign shows how
  often and when you last worked it (and fills the Name field), then queries
  QRZ.com XML or HamQTH for name, QTH and grid (Preferences ==> Callbook).
- **Rig receiver filter**: a Filter width dropdown and IF shift control under the
  tuning scope drive the rig through Hamlib; a width can be applied on connect.
- **Tune**, **CW ID** (at the end of every transmission, from the menu or the
  `<CWID>` macro tag) and a **TX monitor** that plays an AFSK copy of FSK/CAT keyed
  text on the sound card.
- **Configurations** (as in WSJT-X): named, complete sets of settings in the
  menu under *Configurations*. Switch between them (rig, audio, tones, macros,
  station details, everything), clone the active one under a new name, rename,
  reset it to defaults or delete it. Handy for a contest setup, a second rig
  or a portable station. Each lives in `~/.config/gtty/profiles/<name>.conf`.
- **Auto RX** (optional): return to receive once the TX buffer has been empty
  for a set delay, or stay keyed and idle until you press RX.
- **GridTracker2 integration** over the WSJT-X UDP protocol: every callsign
  heard is plotted on the GridTracker map, logged QSOs are forwarded (QSO Logged
  and ADIF messages, so GridTracker can push them to your online logs), and
  clicking a station in GridTracker fills gtty's Call field. Halt-TX and free
  text from the other side are honoured too.
- **fldigi-compatible macros** on F1–F12 (`<TX> <RX> <MYCALL> <CALL> <RST> <CNTR> <LOG> …`),
  right-click a button to edit.
- 45.45 / 50 / 75 / 100 baud, mark and space tones set directly (presets for
  standard and reversed high/low tones), ITA2 or US-TTY, unshift-on-space.
  Defaults: mark 2125 Hz, space 2295 Hz (170 Hz shift).

## Building

### Dependencies, Linux (Debian 13 / trixie)

```
sudo apt install build-essential cmake ninja-build pkg-config \
     libgtk-4-dev libadwaita-1-dev libhamlib-dev portaudio19-dev \
     libfftw3-dev libsamplerate0-dev libsndfile1-dev libsqlite3-dev
```

### Dependencies, macOS (Homebrew)

```
brew install cmake ninja pkg-config gtk4 libadwaita hamlib portaudio \
     fftw libsamplerate libsndfile codec2
```

`sqlite3` and `libcurl` come from the macOS SDK, so no Homebrew package is
needed for them. Audio goes through PortAudio's CoreAudio backend and the GUI
through GTK4's macOS backend; there is no XQuartz or X11 involved. There is no
`.app` bundle yet, so run gtty from the build tree or from the prefix you
install it into. See [macOS notes](#macos-notes) for serial ports, microphone
permission and what differs in FSK keying.

### Build

The same on either platform:

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/src/gui/gtty
```

Always configure from the top-level directory; the `src/*` directories are not
standalone CMake projects.

## Command-line tool

`gtty-cli` exercises the modem without the GUI:

```
gtty-cli encode "CQ CQ DE N0CALL" cq.wav        # make an AFSK RTTY WAV
gtty-cli decode cq.wav                          # decode a recording (both decoders)
gtty-cli listen --in 7                          # decode live audio to stdout
gtty-cli record out.wav --in 7 --seconds 10     # capture what PortAudio delivers
gtty-cli devices                                # list audio devices
gtty-cli rigs icom                              # list Hamlib models
gtty-cli rig --model 3073 --device /dev/ttyUSB0 # read frequency/mode
```

Options: `--baud 45.45 --shift 170 --mark 2125 --reverse --rate 48000 --no-afc --no-atc --ustty`.

## GridTracker2

In GridTracker2, Settings ==> General: set the WSJT-X/JTDX receive port (default
UDP 2237, IP 127.0.0.1). In gtty, Preferences ==> GridTracker: enable and point
at the same host/port. gtty then behaves like a WSJT-X instance named `gtty`:

| gtty ==> GridTracker | GridTracker ==> gtty |
|---|---|
| Heartbeat every 15 s | Reply (station clicked) ==> Call field |
| Status: dial frequency, RTTY, DX call, TX state | Halt TX ==> abort transmission |
| Decode: `CQ <CALL> [GRID]` per callsign heard (once a minute per call) | Free text ==> TX buffer (and transmit if asked) |
| QSO Logged + Logged ADIF when you log | |

GridTracker only plots stations it can place on a band, so without a connected
rig set Preferences ==> Rig ==> "Dial frequency without rig" to the frequency you are
listening on (default 14080 kHz); gtty reports it in Status and uses it for the
band in the log. Set your callsign and grid in Preferences ==> Station so GridTracker
knows who is reporting.

If WSJT-X is also running, GridTracker accepts several senders on the same port.
A multicast address such as 224.0.0.1 works as well.

## Example: Yaesu FT-710 / FTDX10 setup

The radio's USB port presents two serial ports (CP2105): interface 00
(`/dev/ttyUSB0`, "Enhanced COM") carries CAT; interface 01 (`/dev/ttyUSB1`,
"Standard COM") provides the DTR/RTS lines the radio can use for FSK and PTT.

- Preferences ==> Rig: model **Yaesu FT-710 (1049)**, device `/dev/ttyUSB0`,
  speed matching the radio's CAT RATE menu (38400), PTT method **CAT command**.
  Set the radio's CAT RTS menu to OFF.
- **True FSK**: Preferences ==> Transmit: method *FSK via DTR*, FSK device
  `/dev/ttyUSB1`, Rig mode on TX **RTTY** (RTTY-L). On the radio set
  RTTY SHIFT PORT = DTR, POLARITY TX = NOR (flip gtty's "Invert FSK polarity"
  if the far end prints garbage), MARK FREQUENCY 2125, SHIFT 170. Receive in
  RTTY-L with gtty's Rev off, or RTTY-U (Hamlib "RTTYR") with Rev on.
- **AFSK**: the FT-710 ignores audio in RTTY mode, so use Rig mode on TX
  **PKTUSB** (DATA-U) with Rev on, or **LSB** with Rev off, and feed the USB
  codec. Keep ALC just barely moving.
- Only one program may use the CAT port at a time; run `gtty-cli rig` tests
  with the GUI closed, or point both at a shared `rigctld` (model 2).

## Logger integration (fldigi-compatible XML-RPC)

Enable it under Preferences ==> Integration. gtty then answers the fldigi XML-RPC
methods loggers use: `text.get_rx_length` / `text.get_rx` / `text.add_tx`,
`main.tx` / `main.rx` / `main.abort` / `main.get_trx_status`, `main.get_frequency`
/ `main.set_frequency`, `modem.get_carrier` / `modem.set_carrier`, the `log.*`
field accessors, `rig.*` mode queries and `main.run_macro`. Point CQRLOG's or
TLF's fldigi settings at `127.0.0.1:7362`. If fldigi itself is running, give
gtty a different port.

## Installing from packages

There are no prebuilt macOS packages; build from source as above.

- **Debian / Ubuntu**: download the `.deb` for your release from the releases
  page and `sudo apt install ./gtty_*.deb`. Packages are built for Debian 13
  (trixie), Ubuntu 24.04 LTS, 25.04 and 25.10, on amd64 and arm64, named
  `gtty_<version>_<distro>_<arch>.deb`; dependencies are resolved by apt. On a
  PipeWire desktop the `pipewire-alsa` package must be installed for the
  `default` and `pipewire` audio devices to open; apt pulls it in as a
  recommendation, `dpkg -i` does not. gtty shows a banner across the top of the
  window if its audio input could not be opened.
- **AppImage**: download `gtty-x86_64.AppImage` or `gtty-aarch64.AppImage`,
  `chmod +x` it, run. Bundles GTK4, libadwaita and Hamlib 4.6.2.
- Packages are produced by `.github/workflows/build.yml` on every push and
  attached to GitHub releases for tags `v*`. Build a `.deb` locally with
  `cd build && cpack -G DEB`; build an AppImage with `packaging/build-appimage.sh`.

## Decoder benchmark

`tools/bench_rtty.py` decodes off-air recordings with known content and reports the
character error rate (CER) per decoder. Put recordings in `resources/` (ignored by
git). The default set uses W6IWI's "quick brown fox" beacon captured on WebSDR
(40 m with deep QSB and 20 m), a clean Wikipedia text, and a DWD Hamburg 50 baud /
450 Hz shift maritime bulletin recorded as SDR I/Q. Typical results:

| Recording | Fast (0.7 bit) | Narrow (1.0 bit) |
|---|---|---|
| 40 m W6IWI, first 14 min, QSB | CER ≈ 11% | ≈ 14% |
| 20 m W6IWI, 39 min, fades | CER ≈ 2% | ≈ 2% |
| Wikipedia 10 s, clean | 0 errors | 0 errors |
| DWD I/Q, 50 baud, 6.7 min | 5 framing errors | 2 framing errors |

The demodulator knobs are exposed on `gtty-cli decode` for tuning:
`--filter-bits`, `--window 0|1`, `--atc-tau`, `--bit-window`, `--afc-gain`.
`gtty-cli decode --iq --center 1000 file.wav` treats a stereo file as I/Q baseband
and shifts its centre to 1000 Hz, so SDR recordings can be decoded directly.
The remaining errors on HF are concentrated in deep fades; a maximum-likelihood
character decoder is the planned next step for those.

## FSK keying notes

- **UART**: the serial port is set to the RTTY baud rate, 5 data bits, 2 stop
  bits. TXD idles at mark. The non-standard 45.45 baud rate is set with termios2
  `BOTHER` on Linux and with the `IOSSIOSPEED` ioctl on macOS. Works with FTDI,
  CP210x, PL2303 and native ports on Linux; on macOS FTDI adapters take the rate
  reliably, while some CP210x and CDC-ACM drivers refuse it : gtty reports the
  error, and FSK via DTR/RTS or AFSK works regardless. Your interface's
  transistor/opto normally inverts, which is what rigs expect.
- **DTR/RTS**: timed in a dedicated thread against absolute deadlines, so the
  sleeps cannot accumulate drift. Use the "Invert FSK polarity" switch if the
  rig prints garbage.
- **CAT text**: uses Hamlib `rig_send_morse`, which maps to the `KY` command on
  Elecraft and Kenwood rigs. In FSK/RTTY mode those rigs key FSK from the text.
- On Linux the user must be in the `dialout` group for serial access. On macOS
  `/dev/cu.*` is world-accessible and no group membership is needed.

## macOS notes

- **Serial ports** are named `/dev/cu.usbserial-XXXXXXXX` (FTDI),
  `/dev/cu.SLAB_USBtoUART` (Silicon Labs) or `/dev/cu.usbmodemXXXX` (CDC), not
  `/dev/ttyUSB0`. `ls /dev/cu.*` lists them. Always use the `cu.` name, not the
  `tty.` one: `tty.` blocks on carrier detect. Everywhere this README says
  `/dev/ttyUSB0`, read the `cu.` name of your adapter instead.
- **Microphone permission**: macOS gates audio input. The first time gtty opens
  the sound card, macOS asks the application that launched it : your terminal,
  since gtty is not a bundled `.app` : for permission. If you dismissed the
  prompt, re-enable it under System Settings ==> Privacy & Security ==> Microphone;
  without it PortAudio opens the device but delivers silence, and gtty's
  waterfall stays empty.
- **Audio routing**: pick the rig's USB codec explicitly in
  Preferences ==> Audio rather than relying on the system default, and turn off
  any "ambient noise reduction" on the input. `gtty-cli devices` lists what
  CoreAudio offers.
- **True FSK** over a UART needs a driver that accepts 45 baud; see
  [FSK keying notes](#fsk-keying-notes). FSK via DTR/RTS is bit-banged from a
  normal thread, so on a loaded machine it jitters more than the FTDI UART path.
- Everything else : the four decoders, the skimmer, Hamlib rig control, the
  logbook, GridTracker and the XML-RPC server : behaves as it does on Linux.

## Layout

```
src/core     GUI-independent library: Baudot codec, modulator, demodulators,
             decoder bank, PortAudio engine, Hamlib wrapper, FSK keyer,
             logbook, callsign/SCP tools, macros, TX/RX orchestration
src/cli      gtty-cli
src/gui      GTK4/libadwaita application
tests        ctest unit tests (Baudot, modem loopback with noise/offset/resampling,
             macros, callsign recognition)
docs         architecture notes
```

See `docs/ARCHITECTURE.md` for the signal-processing design and the roadmap.


## License

GPL-3.0-or-later.

---


