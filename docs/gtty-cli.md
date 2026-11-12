---
title: gtty-cli
nav_order: 14
---

# gtty-cli

`gtty-cli` is a command-line tool built on the same modem as the GUI. It encodes text to a WAV file, decodes recordings and live audio, records audio, lists devices and Hamlib rigs, and reads a rig's frequency. It is the tool for testing a setup and for comparing decoder settings on a recording with a known text.

```
gtty-cli encode|decode|listen|record|devices|rigs|rig [options]
```

## Commands

| Command | What it does |
|---|---|
| `encode "TEXT" out.wav` | Write an AFSK RTTY WAV file of the text, with two LTRS before and one after, and 0.2 s of silence at each end |
| `decode in.wav` | Decode a recording with every enabled engine and print each engine's text and statistics, then the vote |
| `listen` | Decode live audio from an input device to standard output until Ctrl-C |
| `record out.wav` | Capture live audio to a WAV file for `--seconds` (10 by default) |
| `devices` | List PortAudio devices with their index, host API, channel counts and default sample rate |
| `rigs [filter]` | List Hamlib rig models, optionally those whose manufacturer or name contains the filter |
| `rig --model N --device DEV` | Open the rig, wait briefly and print frequency, mode, filter width and PTT state |

## Examples

```
gtty-cli encode "CQ CQ DE N0CALL" cq.wav        # make an AFSK RTTY WAV
gtty-cli decode cq.wav                          # decode it with every engine
gtty-cli devices                                # find the input device index
gtty-cli listen --in 7                          # decode live audio from device 7
gtty-cli record out.wav --in 7 --seconds 600    # record ten minutes of the band
gtty-cli rigs yaesu                             # Hamlib model numbers for Yaesu
gtty-cli rig --model 1049 --device /dev/ttyUSB0 --serial-baud 38400
gtty-cli decode --iq --center 1000 sdr.wav      # a stereo I/Q SDR recording
```

Decoding a recording prints one block per engine:

```
== A (chars 612, framing errors 3, AFC -4.2 Hz) ==
THE QUICK BROWN FOX ...
== B (chars 605, framing errors 9, AFC -4.1 Hz) ==
...
== Vote (chars 610, framing errors 0, AFC 0 Hz) ==
...
```

## Options

### Signal

| Option | Meaning | Default |
|---|---|---|
| `--baud B` | Baud rate | 45.45 |
| `--shift HZ` | Shift | 170 |
| `--mark HZ` | Mark tone | 2125 |
| `--reverse` | Space below mark | off |
| `--stop N` | Stop bits for encoding | 1.5 |
| `--rate HZ` | Sample rate for encoding, listening and recording | 48000 |
| `--ustty` | US-TTY character set instead of ITA2 | ITA2 |
| `--no-usos` | Do not unshift on space | unshift on |

### Decoder

| Option | Meaning |
|---|---|
| `--no-afc`, `--no-atc` | Switch AFC or ATC off |
| `--squelch Q` | Squelch threshold 0..1, as the GUI slider |
| `--mode normal|selective|flutter` | Preset for the matched-filter engines |
| `--filter-bits F` | Matched filter length in bit periods |
| `--window 0|1` | 0 boxcar, 1 Hann |
| `--atc-tau S` | ATC time constant in seconds |
| `--bit-window F` | Fraction of the bit period integrated around each bit center |
| `--afc-gain G` | Fraction of the measured error applied per character |
| `--corr-edge F`, `--corr-accept F` | Correlator start/stop cell threshold and acceptance score |
| `--engines all` | Run every engine, including the codec2 comparison decoder |
| `--start S`, `--limit S` | Decode only from `S` seconds in, for `S` seconds |
| `--quiet` | Print statistics only, not the text |

### Input and output

| Option | Meaning |
|---|---|
| `--in N`, `--out N` | PortAudio device index from `gtty-cli devices` |
| `--seconds N` | Recording length |
| `--iq` | Treat a stereo file as I/Q baseband (an SDR recording) |
| `--center HZ` | Audio frequency the I/Q center is shifted to (default a quarter of the sample rate) |

### Rig

| Option | Meaning |
|---|---|
| `--model N` | Hamlib model number |
| `--device DEV` | Serial port or `host:port` |
| `--serial-baud N` | Serial speed |
| `--debug` | Hamlib trace output |

{: .note }
Close the GUI before running `gtty-cli rig`: only one program may use the CAT port. Alternatively run `rigctld` and use model 2 in both.

## Benchmarking decoder settings

`tools/bench_rtty.py` in the source tree runs `gtty-cli decode` over recordings with known content and reports the character error rate per engine, so settings can be compared on real signals. See [Development](development.md#tuning-the-decoders-on-your-own-recordings).
