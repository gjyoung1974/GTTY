---
title: Decoders
nav_order: 6
---

# Decoders

gtty runs several demodulators on the same audio and lets them vote. Each engine has its own receive pane; the **Best guess** pane shows the merged result and is what GridTracker, the XML-RPC server and the session file see.

## The engines

| Engine | What it is | Default |
|---|---|---|
| **A: matched filter** | An MMTTY-style decoder: complex mixers at mark and space, matched filters, per-tone ATC, soft start and stop bit decisions, AFC | Normal mode, pane shown, votes |
| **B: matched filter** | A second instance of the same decoder with different settings, so two views of a fading signal are available at once | Flutter mode, pane shown, votes |
| **Correlator** | A joint-timing character decoder in the Gritty style. It correlates whole 7-bit characters against all 32 Baudot templates and picks the best start instant, so it has no separate bit clock to lose in a fade | Enabled, pane hidden, votes |
| **codec2 FSK** | The FSK demodulator from libcodec2, as a comparison engine. Only present when gtty was built with libcodec2 | Disabled |

On the W6IWI off-air beacon recordings used for tuning, the engines measure as follows (character error rate, lower is better):

| Engine | 40 m, weak with deep fading | 20 m, fading |
|---|---:|---:|
| A, Normal | 9.0 % | 4.3 % |
| B, Flutter | 12.3 % | 5.6 % |
| Correlator | 6.4 % | 4.3 % |
| codec2 | 17.3 % | 4.9 % |
| Best guess (vote of A, B, Correlator) | 7.8 % | 4.1 % |

## Matched-filter modes

Engines A and B have a mode dropdown in their pane header and under *Preferences → Decoders*:

| Mode | Settings | When to use it |
|---|---|---|
| **Normal** | 0.6-bit Hann filter, 0.3 s ATC | Everyday use |
| **Selective** | 1.0-bit Hann filter, 0.25 s ATC | Narrower: a crowded band, a strong neighbor, a strong signal |
| **Flutter** | 0.5-bit Hann filter, 0.3 s ATC, 25 % decision cell | Wider and faster: polar flutter, rapid fading |
| **Custom** | The three values set in Preferences | Experiments |

The three custom values are:

- **Matched filter length** in bit periods (0.3 to 2.0). Shorter is wider in bandwidth and follows flutter; longer is narrower and rejects neighbors.
- **Filter window**: boxcar is the true matched filter for rectangular FSK; Hann rejects adjacent signals better.
- **ATC time constant** in seconds (0.02 to 3.0).

Running A in Normal and B in Selective or Flutter is the usual arrangement: when one pane prints garbage in a fade the other often still reads.

## Per-engine switches

Each engine has three switches in *Preferences → Decoders*:

- **Enabled**: the engine runs. Disabling an engine saves CPU.
- **Show its RX pane**: the engine's text is displayed. The Correlator's pane is hidden by default; its opinion still counts in the vote.
- **Takes part in the best-guess vote**.

codec2 decides about half a second later than the others, so letting it vote delays the merged pane. It stays non-voting by default.

## Best guess

The engines' characters are aligned in time and each position is decided by a confidence-weighted vote. The merged stream appears in the **Best guess** pane and feeds GridTracker, the XML-RPC server, the session file and callsign reporting. The pane can be hidden under *Preferences → Decoders* if you prefer to read the engines directly.

During the first second of a new signal only one engine may have an opinion, so the merged pane can miss the first characters while the engine panes show them.

## Skimmer

The Skimmer runs a decoder every 50 Hz (configurable from 20 to 200 Hz) across the whole waterfall range, so every RTTY signal in the receiver passband is decoded at once. Turn it on with **Skimmer** in the menu or under *Preferences → Decoders*. It uses CPU in proportion to the passband width and the channel step.

Stations appear in the **Skimmer** page of the sidebar, sorted by frequency, with the RF frequency computed from the dial and the sideband, the callsign found in the text (or **?** until one has been seen), the age of the last decode and the last few characters received. Rows are colored from the logbook: a new callsign in the accent color, a dupe on this band dimmed.

**Click a row** to move the decoders to that signal and put the callsign in the Call field. Entries disappear 90 seconds after the station was last heard.

{: .tip }
With a wide receiver filter (2.4 kHz or more) and the Skimmer on, you can see the whole RTTY segment at once and work stations by clicking them, which is how a contest run on search-and-pounce goes fastest.

## Other decoder settings

*Preferences → RTTY* also holds:

- **Stop bits (TX)**: 1.5 or 2. Receive tolerates either.
- **Character set**: ITA2 (international) or US-TTY.
- **Unshift on space**: return to letters after a space, as most amateur stations expect.
- **AFC range** in Hz.
- **Net**: transmit on received tones.

The command-line tool exposes the same engine parameters for decoding recordings, so settings can be compared on a file with a known text. See [gtty-cli](gtty-cli.md) and the [Development](development.md) page.
