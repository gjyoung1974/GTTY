---
title: Development
nav_order: 17
---

# Development

## Source layout

| Directory | Contents |
|---|---|
| `src/core` | GUI-independent library: Baudot codec, modulator, demodulators, decoder bank and voter, skimmer, PortAudio engine, Hamlib wrapper, FSK keyer, logbook, callsign and SCP tools, macros, cluster and callbook clients, WSJT-X UDP and XML-RPC, the logbook server client and the skimmer spot feed, TX/RX orchestration |
| `src/cli` | `gtty-cli` |
| `src/gui` | The GTK4/libadwaita application: main window, Preferences dialog, waterfall, XY scope, settings and configurations |
| `tests` | ctest unit tests: Baudot, modem loopback with noise, offset and resampling, voter, correlator, skimmer, macros, callsign recognition, profiles, cluster parsing, XML-RPC, WSJT-X encoding, the spot feed, the logbook server client, CW, callbook |
| `tools` | `bench_rtty.py`, the decoder benchmark |
| `packaging` | Man pages, Debian changelog and copyright, AppImage build script |
| `data` | Desktop entry, AppStream metainfo, icon |
| `docs` | This manual and the architecture notes |

## Building and testing

See [Installation](installation.md#building-from-source) for the dependencies. Then:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/integration/test_not1mm.py` checks gtty against [Not1MM](https://github.com/mbridak/not1mm): it starts a private gtty and a Hamlib dummy radio behind `rigctld`, then drives them with Not1MM's own fldigi and rigctld client classes (F-key messages, the `^r` return to receive, PTT, QSY in both directions). It needs a Not1MM checkout (`NOT1MM_SRC`, or `../not1mm`), a display, and `rigctld` for the radio tests (`RIGCTLD`, or `libhamlib-utils`). Run it directly, or with `GTTY_INTEGRATION=1 ctest --test-dir build -R not1mm`; otherwise ctest reports it as skipped.

`tests/integration/test_tlf.py` does the same for [TLF](https://github.com/Tlf/tlf). TLF is a C program, so the test repeats the sequence of fldigi calls from TLF's `src/fldigixmlrpc.c` instead of loading its code: sending a message, Escape, a new message replacing one that is going out, the log frequency in kHz, QSY, auto-QSY and reading the receive text. Run it with `GTTY_INTEGRATION=1 ctest --test-dir build -R tlf`.

Three more tests cover programs that cannot be loaded or installed for a test run, each by repeating the relevant part of the program's source (named in the test's header) rather than a reading of the protocol:

- `test_cqrlog.py`: gtty polled as [CQRLOG](https://github.com/ok2cqr/cqrlog)'s fldigi remote polls it: one HTTP/1.0 connection per call, the answer cut out of the reply between `<value>` and `</value>`, the date, time and frequency formats it expects, and a second logger holding a connection open at the same time. `-R cqrlog`.
- `test_log_forwarding.py`: a QSO is logged in gtty and the *QSO Logged* and *Logged ADIF* datagrams are read with the readers of [QLog](https://github.com/foldynl/QLog), [KLog](https://github.com/ea4k/klog) and [WaveLogGate](https://github.com/wavelog/WaveLogGate); the `log.add_record` call is received by a server that reads it as QLog's fldigi interface does. `-R log_forwarding`.
- `test_spot_feed.py`: a station calls CQ in RTTY on a virtual sound device with the Skimmer and the spot feed on, and cluster clients that parse as TLF (fixed columns), Not1MM and QLog do must each get the spot, once. Needs `pactl` and `paplay`. `-R spot_feed`.

`tests/integration/test_not1mm_gui.py` goes one step further and plays an operator: it starts Not1MM's real main window (off-screen, with an empty configuration and log of its own) beside gtty in the modem-only view and types into it. It sets the frequency and mode, calls CQ with F1, plays an answering station into gtty as RTTY audio, sends the call and exchange with F2 and F3, and logs the QSO with Enter, checking gtty and Not1MM's log at each step. It additionally needs a Python that can import Not1MM and PyQt6 (`NOT1MM_PYTHON`, or the uv or pipx tool environment) and, for the received audio, `pactl` and `paplay`.

CMake options: `GTTY_BUILD_GUI` (default ON) and `GTTY_BUILD_TESTS` (default ON). libcurl and libcodec2 are picked up when present.

The same commands build on Linux and on macOS. Platform differences are confined to two places: `src/core/rig/fsk_keyer.cpp` (custom serial baud rates and bit-bang timing) and `src/core/net/socket_compat.h` (the Linux-only `SOCK_CLOEXEC`/`SOCK_NONBLOCK` socket flags and `MSG_NOSIGNAL`). Keep new platform code in those files rather than spreading `#ifdef`s.

An AddressSanitizer build is a separate build directory, for example `cmake -S . -B build-asan -DCMAKE_CXX_FLAGS=-fsanitize=address`.

## Packaging

`.github/workflows/build.yml` builds and tests on Ubuntu 24.04 on every push, builds `.deb` packages for Debian 13 and Ubuntu 24.04, 25.04 and 25.10 on amd64 and arm64, builds x86_64 and aarch64 AppImages with Hamlib 4.6.2, and attaches everything to the GitHub release when a `v*` tag is pushed.

Locally: `cd build && cpack -G DEB` makes a `.deb`; `packaging/build-appimage.sh` makes an AppImage.

## Tuning the decoders on your own recordings

`tools/bench_rtty.py` decodes recordings with known content and reports the character error rate per engine.

1. **Record.** `gtty-cli record out.wav --in <device> --seconds 600` captures raw audio. WebSDR recordings (16-bit WAV) work directly. For SDR I/Q files add `--iq --center 1000` on the decode side.
2. **Find the tones.** Read mark and shift off the waterfall, or run `gtty-cli decode file.wav --mark 2125 --shift 170` and look at the reported AFC.
3. **Pick a reference.** A beacon or a station repeating a fixed line gives a character error rate; anything else gives framing-error counts only.
4. **Run all engines:**

   ```
   python3 tools/bench_rtty.py --file rec.wav --mark 2125 --shift 170 \
       --ref "THE QUICK BROWN FOX ... DE W6IWI" -- --engines all
   ```

5. **Vary one thing at a time.** Matched filters: `--mode normal|selective|flutter` or `--filter-bits F --window 0|1 --atc-tau S`. Correlator: `--corr-edge F --corr-accept F`. All engines: `--afc-gain G`, `--no-atc`, and `--squelch Q` to see what the GUI slider would discard. Runs are independent, so launch several in parallel and prefer the setting that helps the weak recording without hurting the strong one.
6. **Make it the default.** The presets live in `applyDecoderMode()` in `src/core/modem/decoder_bank.cpp`; correlator thresholds and other defaults in `DemodConfig` in `src/core/modem/rtty_demodulator.h`. Update the comments there, the tables in `docs/TODO.md` and `docs/ARCHITECTURE.md`, then run `ctest`.
7. **Beware of the scorer.** It keeps only lines that resemble the reference, strips a leading counter and counts substitutions, deletions and insertions over the reference length. Short recordings are dominated by the lock-up at the start; use at least a few minutes.

The scorer can also score another program's output: `tools/bench_rtty.py --score-file mmtty.txt --ref "<beacon text>"`.

## Architecture

The [architecture notes](ARCHITECTURE.md) describe the signal path, each demodulator, the voter, the skimmer and the measurements behind the defaults. The open items are in `docs/TODO.md` in the repository.

## This manual

The manual is Markdown under `docs/` and is published to the [GitHub wiki](https://github.com/gjyoung1974/GTTY/wiki), which is the page the Help item in the application opens. Each page has a front-matter block with its title and `nav_order`; the title becomes the wiki page name and `nav_order` sets its place in the sidebar. Links between pages use relative `.md` paths, so the pages also read correctly in the repository.

`tools/build_wiki.py` converts the pages (page links, images and the `{: .note }` style callouts) and the `wiki` workflow pushes the result to the wiki whenever `docs/` changes on `main`. Edit the files in `docs/`, not the wiki: pages edited on the wiki are overwritten by the next run. To preview the generated pages locally:

```
python3 tools/build_wiki.py /tmp/gtty-wiki
```

## License

gtty is free software under the GNU General Public License, version 3 or later.
