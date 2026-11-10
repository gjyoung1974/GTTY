---
title: Architecture notes
nav_order: 18
---

# gtty architecture

## Why we wrote the modem ourselves

There is no reusable Linux library at the level of the Windows RTTY engines hams
consider modern (MMTTY, 2Tone, Gritty). fldigi's modem is good but is application
code (GPLv3, tied to fldigi's framework); minimodem is a program; libcodec2 and
liquid-dsp provide synchronous M-FSK demodulators without start/stop framing.
So gtty uses libraries for primitives (FFTW, libsamplerate, PortAudio, Hamlib,
libsndfile, SQLite) and owns the RTTY-specific DSP, which is what makes parallel
decoders, a future ML character decoder and skimmer-style decoding possible.

## Signal path

```
sound card ──PortAudio──▶ SPSC ring ──▶ Receiver thread
                                          │ libsamplerate → 12 kHz
                                          ├─▶ SpectrumAnalyzer (4096-pt, Blackman-Harris) ─▶ waterfall
                                          ├─▶ DecoderBank
                                          │     ├─ 0 MatchedFilterDemod "A" (Normal preset)
                                          │     ├─ 1 MatchedFilterDemod "B" (Flutter preset)
                                          │     ├─ 2 CorrelatorDemod (joint-timing character correlator)
                                          │     └─ 3 Codec2Demod (libcodec2 FSK; optional, off by default)
                                          │           │ chars per engine + CodeEvent{code, confidence, end sample}
                                          │           ▼
                                          │       Voter ──▶ "best guess" text slot (= bank.size())
                                          └─▶ Skimmer: MatchedFilterDemod every step across the passband
                                                      │ chars / scope / status / skimmer text
                                                      ▼
                                                  EventQueue ──▶ GTK main loop (30 Hz poll)
```

### MatchedFilterDemod (src/core/modem/matched_filter_demod.cpp)

1. Two complex mixers translate the mark and space tones to baseband.
2. Each baseband is filtered by a one-bit-long FIR (boxcar = true matched filter
   for rectangular FSK; Hann = better adjacent-signal rejection).
3. Envelopes are normalised by per-tone peak followers (ATC, ~250 ms). The
   decision variable is `d = m/mpk − s/spk` in [−1, 1].
4. Asynchronous framing: a mark→space transition opens a character; each bit is
   decided from the mean of `d` over the middle third of the bit period; the start
   bit is verified at its centre and the stop bit must be mark, else the character
   is dropped (framing error counter).
5. AFC: the instantaneous frequency of whichever channel carries the tone is
   averaged over strong bits and the LOs are moved by 35 % of the error per
   character, clamped to ±afcRange. Works for signals within ~±1 baud of the
   filters per step, iterating out to the range limit.
6. Quality = EMA of |d| at bit centres, where a clean tone reads about 1 with ATC
   on or off; the squelch slider (0..1) gates output below it. The correlator's
   quality is its correlation score on the same scale, so one slider fits all engines.
7. Scope points are the two filtered channels re-modulated to their tone
   frequencies (classic mark-X / space-Y crossed-ellipse display).

Measured in `tests/test_modem.cpp`: error-free at 10 dB SNR (3 kHz noise
bandwidth), a few errors at 3 dB, 40 Hz offset pulled in by AFC, clean at
48 k / 44.1 k / 8 k input rates, 75 baud / 850 Hz reverse.

Tuned on off-air recordings with `tools/bench_rtty.py` (2026-11-08): on a 40 m
beacon with deep QSB a 0.7-bit Hann filter with a 1 s ATC time constant gave
CER 9.5% against 11.2% for a 1.0-bit filter with 0.25 s, and the boxcar filter
was far worse (30%+), so the default bank is now "Fast (0.7 bit)" plus
"Narrow (1.0 bit)". Bit integration over the middle third of the bit was best.

### Transmit (src/core/transmitter.cpp)

Text → `BaudotEncoder` (LTRS/FIGS insertion, unshift-on-space mirrored from the
receiver) → one of:

- **AFSK**: `RttyModulator` generates phase-continuous FSK with fractional bit
  timing and 4 ms envelope ramps; runs inside the PortAudio output callback.
- **FSK UART**: `FskKeyer` sets the port to 45/50/75/100 baud 5N2 — termios2
  `BOTHER` on Linux, the `IOSSIOSPEED` ioctl on macOS — and writes the 5-bit
  codes. Pacing is by `TIOCOUTQ` where the driver reports a queue depth (Linux)
  and by `tcdrain()` where it does not (macOS), so echo and
  end-of-transmission track the real keying either way.
- **FSK DTR/RTS**: bit-banged against absolute `steady_clock` deadlines, so the
  sleeps cannot accumulate drift.
- **CAT text**: 24-character chunks through Hamlib `rig_send_morse` (`KY`).

PTT via Hamlib (`rig_set_ptt`, CAT or DTR/RTS line). Graceful stop drains the
queue, sends a trailing LTRS, drops PTT and restores the rig mode.

## GridTracker2 / WSJT-X UDP (src/core/net/wsjtx_udp.cpp)

Hand-rolled QDataStream encoder/decoder (big-endian, magic `0xadbccbda`,
schema 2). Outgoing: Heartbeat, Status, Decode, Clear, QSO Logged, Logged ADIF,
Close. Incoming: Reply, Halt TX, Free Text, Clear, Close. RTTY text is free
form, so the GUI shapes each heard callsign into a WSJT-X-looking message
(`CQ CALL GRID` or `MYCALL CALL GRID`) that GridTracker's parser understands;
grids are picked up from the same RTTY line when present. Non-blocking UDP
socket polled from the GTK tick; no extra thread.

## fldigi-compatible XML-RPC (src/core/net/xmlrpc_server.cpp)

A small HTTP/1.1 server with a hand-written XML-RPC parser and serialiser
(string, int, double, boolean, base64, array, struct, nil). The handler runs on
the server thread, so the GUI bridges each call onto the GTK main loop with
`g_idle_add` and waits on a future (3 s timeout). Received text is kept in a
growing string so `text.get_rx(start, len)` offsets stay stable within a session.

## Threads

- PortAudio input/output callbacks (real-time; lock-free rings only).
- Receiver DSP thread.
- Hamlib poller (mutex around every `rig_*` call).
- FSK keyer thread (UART pacing or bit-banging).
- GTK main thread polls `EventQueue` at 30 Hz and drives `Transmitter::tick()`.
- XML-RPC accept loop (one connection at a time, keep-alive honoured).

### Decoder modes (src/core/modem/decoder_bank.cpp)

`applyDecoderMode()` holds the presets for the matched-filter engines, measured
with tools/bench_rtty.py on the W6IWI off-air recordings (CER 40 m / 20 m):
Normal 0.6-bit Hann, 0.3 s ATC (9.0 % / 4.3 %); Selective 1.0-bit Hann, 0.25 s
ATC (12.5 % / 3.4 %, narrower, best on the stronger signal); Flutter 0.5-bit
Hann, 0.3 s ATC, 25 % decision cell (12.3 % / 5.6 %, for flutter paths). Custom
takes the three values from Preferences. Each engine's pane header has the
selector.

### CorrelatorDemod (src/core/modem/correlator_demod.cpp)

Same front end as the matched filter but with a half-bit filter, producing one
soft decision d(t) per sample into a cumulative-sum ring. For every candidate
start instant (every 1/8 bit) it integrates the central 60 % of each of the seven
bit cells, requires the start cell to read space and the stop cell mark, and
correlates the five data cells with all 32 templates (edge threshold 0.03 and acceptance
score 0.30 per bit on the ±1 soft-decision scale, both tunable through `DemodConfig`
and the CLI). The first
candidate above the acceptance score opens a one-bit refinement window; the strongest
correlation in it wins, and the search resumes seven bits later. AFC averages the
frequency discriminator over the accepted character. Reported end time =
start + 7 bits, so the voter can align it with the other engines.

### Voter (src/core/modem/voter.cpp)

Every engine emits `CodeEvent{code, valid, confidence, sample}` where `sample`
is the input-clock index at which the character ended. Codes within half a
character period of each other form a group (one vote per engine). A group is
decided once the slowest voting engine's latency plus 0.75 character has passed:
the code with the largest confidence sum wins; a lone opinion with confidence
below 0.2 (squelched) is dropped. The merged stream has its own Baudot shift
state and is the GUI's primary text (GridTracker, XML-RPC, session file).

### Codec2Demod (src/core/modem/codec2_demod.cpp)

libcodec2's FSK demodulator is synchronous, so it runs at twice the baud rate
(Rs = 2 × baud, Ts = 80 samples, 24 symbols per call): every RTTY element is a
whole number of half-bit symbols and codec2's timing tracker can lock. The
soft outputs (mark minus space magnitude per half-bit) go through a small
UART framer. codec2 estimates the tone frequencies itself, which doubles as
AFC. Its frame latency (~0.3 s) is why it does not vote by default.

### Skimmer (src/core/modem/skimmer.cpp)

A `MatchedFilterDemod` (Normal preset, AFC range half a step, squelch ≥ 0.5)
every `step` Hz across the waterfall range. A strong signal also decodes in the
neighbouring channels, so at collection time only the channel with the highest
absolute level within one shift reports (ties go to the lower channel). The GUI
merges text per channel, extracts callsigns, shows the RF frequency (dial ±
tone, by sideband) and tunes the main decoders to a clicked entry.

## Roadmap

1. **Comparative benchmark** against MMTTY and 2Tone on shared off-air
   recordings (tools/bench_rtty.py exists; the numbers do not yet).
2. **Contest integration**: contest definitions (exchange templates,
   multipliers, scoring), Enter-sends-message, automatic exchange grab, Cabrillo
   export, N1MM-style UDP broadcast, Cloudlog/Wavelog upload.
3. Flatpak packaging (deb and AppImage exist).
