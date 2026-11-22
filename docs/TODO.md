# gtty: open items

Reviewed 2026-11-15 at `0d27bd1` (v0.3.2 plus the merges up to #36). The unit
tests pass (26 of 26; the six integration tests are opt-in and were skipped).

This file has no front matter on purpose: `tools/build_wiki.py` and
`docs/_config.yml` leave it out of the manual.

## 1. Open issues

- [ ] **#35 Shift/Tones menu.** Both parts are on `main`: the "Shift/Tones"
      label (#36) and `onShiftTonesChosen` closing the popover. Confirm the
      popover stays open when gtty sets the drop-downs itself (profile load,
      tones following the shift), then close the issue.
- [ ] **#31 `log.get_frequency`.** Changed in #34 to return the mark
      frequency in kHz. Tested against the TLF-style integration test and a
      dummy rig only. Waiting for zcsahok to confirm with TLF and a real radio.
- [ ] **#7 Copy from the receive panes.** Copy and Ctrl+C work. Select and
      middle-click (primary selection) fails only on df7cb's setup (Wayland,
      GTK 4.24). Waiting for the `GDK_DEBUG=clipboard` log. One lead to check
      without it: a selection that reaches the end of the text grows as new
      characters arrive.
- [ ] **#14 Space before end-of-transmission noise.** Implemented in #19,
      tested on simulated signals only. Needs on-air feedback about missing or
      extra spaces (a fade of two or more characters can add one).
- [x] **#32 Radio's built-in decoder on single-port Icoms.** The original
      IC-7300's *USB Serial Function* makes its one USB port either CI-V or RTTY
      decode; the IC-7300MKII has two ports like the IC-7610 (df7cb, from the
      MKII manual). Both are in `docs/decoders.md`. Still unverified from #15:
      line breaks in a QTC series on the IC-7610.
- [ ] **#6 Umbrella issue.** Every item has been split out. Close it once #7
      and #14 are settled. #12 and #13 (highlighting) are closed but were never
      confirmed on the air.

## 2. Documentation that no longer matches the code

- [ ] `docs/ARCHITECTURE.md`, MatchedFilterDemod section: says the default
      bank is "Fast (0.7 bit)" plus "Narrow (1.0 bit)". The bank is now Normal
      and Flutter presets, as the signal-path diagram and the Decoder modes
      section in the same file say. The README benchmark table uses the old
      Fast/Narrow columns too; rerun `tools/bench_rtty.py` and replace it.
- [ ] `docs/ARCHITECTURE.md`, Threads: says the XML-RPC accept loop serves
      "one connection at a time". `xmlrpc_server.cpp` serves many connections
      on one thread, and `test_cqrlog.py` depends on that.
- [ ] `docs/ARCHITECTURE.md` does not cover the newer parts: the radio decode
      port, trailing-noise separation, RX highlighting, line-break handling,
      the noise floor behind the waterfall, the spot server, the cluster
      client, the fllog client, sideband following and the separate PTT port.
- [ ] `docs/development.md` step 6 refers to "the tables in `docs/TODO.md`".
      There are none here; point it at `docs/ARCHITECTURE.md` only, or move
      the benchmark tables into this file.
- [ ] `README.md`: the title reads "for the Linux and macOS"; the Layout
      section describes `docs` as "architecture notes" and lists four test
      areas where there are 26 tests; "Gtty" and "gtty" are both used.
- [ ] `README.md` repeats large parts of the manual (FT-710 setup,
      GridTracker, FSK notes, macOS notes). Cut it down to an overview that
      links to the manual, so there is one copy to keep current.
- [ ] `data/io.github.gtty.metainfo.xml.in`: the description says "for the
      Linux desktop", there is no `<screenshots>` block and the release entry
      has no description.

## 3. Roadmap features

Carried over from the roadmap in `docs/ARCHITECTURE.md`; none of this is
started.

- [ ] **Comparative benchmark** against MMTTY and 2Tone on shared off-air
      recordings. `tools/bench_rtty.py --score-file` can already score another
      program's output; the recordings and the numbers are missing.
- [x] **HF channel simulator and benchmark** (`core/dsp/hf_channel`,
      `tests/bench_channel.cpp`, `gtty-cli channel`): error rate per engine on
      the ITU-R F.1487 channels.
- [x] **The best guess was no better than the Correlator alone** in contest
      traffic: it printed the noise between transmissions (a carrier coming up
      made A and B agree on a wrong character; a chance agreement of the two
      let held noise out) and left no space between them. A and B now count as
      one witness, a transmission ends after three characters without a sound
      decision, and a space follows it. `bench_channel --scenario contest`,
      2026-11-21, 2,200 characters per point: white noise at +6 dB 7.7 % to
      0.6 % (Correlator 5.8 %); mid-latitude moderate at +6 dB 11.1 % to
      4.3 % (Correlator 9.4 %). Ahead of the Correlator at 46 of 50 points;
      behind by 0.3 to 2.4 points at -6 dB on four of the slow-fading channels.
      Continuous text is unchanged. Needs on-air confirmation.
- [ ] **In continuous text below 0 dB the best guess is up to three points
      behind the Correlator** (white noise at -6 dB: 16.9 % against 13.6 %),
      as it was before. Adding up the engines' soft bits
      (`Voter::Combine::Soft`, `gtty-cli decode --vote soft`) does not change
      that; it is not the default.
- [ ] **Shift errors and stray spaces** are about a third of the best guess's
      errors on the fading channels at 0 dB (`bench_channel --detail`,
      mid-latitude moderate: 16.8 % in all, 14.0 % without shift errors,
      11.2 % without spaces as well). A lost FIGS or LTRS garbles a word; a
      doubtful character in a fade is taken for the end of a transmission and
      splits a callsign.
- [ ] **Maximum-likelihood character decoder** for deep fades, which the
      README names as the next decoder step.
- [ ] **Contest support**: contest definitions (exchange templates,
      multipliers, scoring), Enter-sends-message, automatic exchange grab,
      Cabrillo export, N1MM-style UDP broadcast.
- [ ] **Cloudlog / Wavelog upload** directly, not only through the WSJT-X UDP
      forwarding.
- [ ] **Flatpak** packaging and a Flathub submission (needs the metainfo
      items in section 2).

## 4. Logbook

The logbook can add, list recent QSOs, search by call, check dupes and export
ADIF. Missing:

- [ ] A logbook window: browse, search, edit and delete QSOs. Today a
      mistake in a logged QSO can only be fixed in the SQLite file.
- [ ] ADIF import, so an existing log gives worked-before and dupe status.
- [ ] Export of a selection (date range, contest) instead of the whole log.
- [ ] Retry for QSOs that could not be forwarded. If the logbook program is
      unreachable the QSO stays in gtty only and the operator has to export it
      by hand.

## 5. Code health

- [ ] **Split `src/gui/main_window.cpp`** (2,820 lines, the next largest
      source file is 514). Natural pieces: the XML-RPC method dispatch, the
      WSJT-X/GridTracker glue, the band map and cluster pane, the skimmer
      list, the macro editor, the log row.
- [ ] **Move the XML-RPC method dispatch into `src/core`** behind an
      interface. It then gets unit tests without a display, and `gtty-cli`
      could serve loggers headless.
- [ ] **Callbook password** is stored in plain text in
      `~/.config/gtty/profiles/<name>.conf` and is copied when a configuration
      is cloned. Move it to the system keyring (libsecret on Linux, Keychain
      on macOS), with the file as a fallback.
- [ ] **Unit tests are missing** for `log/adif`, `log/logbook`, `log/grid`,
      `transmitter`, `dsp/resampler`, `dsp/spectrum`, `audio/wav_file` and
      `rig/fsk_keyer` (UART pacing and bit-bang timing, testable on a pty).
- [ ] **Sanitizer builds** (ASan, UBSan, TSan) are described in
      `docs/development.md` but not run anywhere. The receiver, keyer, Hamlib
      poller and XML-RPC threads make a TSan run worthwhile.
- [ ] **Translations**: no gettext; every string in the interface is English
      in the source.

## 6. CI and packaging

- [ ] **Run the integration tests in CI.** All six are skipped unless
      `GTTY_INTEGRATION=1`. `cqrlog_integration`, `tlf_integration` and
      `log_forwarding` need only gtty, a display (xvfb) and `rigctld`, so they
      can run on every push. The Not1MM and spot-feed tests need a Not1MM
      checkout and PulseAudio; a nightly job is enough for those.
- [ ] **`test_text_append` in CI**: it needs a display and returns "skipped"
      without one, so check whether the Ubuntu job runs it; if not, run ctest
      under `xvfb-run`.
- [ ] **macOS Intel**: no `.dmg` is built (arm64 only), and the x86_64 build
      and test job runs only on a manual workflow run.
- [ ] **macOS signing and notarization**: the app is unsigned, so the first
      launch needs Control-click, Open, or `xattr`.
- [ ] **lintian** runs with `|| true`. Read the current output, fix what is
      real and let the rest fail the job.
- [ ] **AppImage Hamlib** is pinned to 4.6.2; check for a newer release when
      new rigs matter.

## 7. Needs on-air or hardware confirmation

- [ ] RX highlighting of CQ, 599, the exchange and run-together callsigns
      (#12, #13) in a contest.
- [ ] UART FSK at 45.45 baud on macOS with CP210x and CDC-ACM adapters; the
      README says some drivers refuse the rate, without a list.
- [ ] DTR/RTS bit-bang jitter on a loaded machine, measured rather than
      estimated, on Linux and macOS.
- [ ] CAT text keying on the rigs named in the README (K3, K4, KX3, TS-890).
- [ ] Rig filter width and IF shift on rigs other than the IC-7610 (#8, #33).
