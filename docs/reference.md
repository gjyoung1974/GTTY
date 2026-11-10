---
title: Reference
nav_order: 16
---

# Reference

## Keyboard shortcuts

| Key | Action |
|---|---|
| **F1** to **F12** | Run the macro on that button |
| **Ctrl+T** | Transmit |
| **Ctrl+R** | Return to receive after the queued text has been sent |
| **Esc** | Abort: drop PTT at once and clear the transmit pane |
| **Ctrl+L** | Log the QSO |
| **Ctrl+K** | Clear the receive and transmit text |
| **Ctrl+,** | Preferences |
| **Ctrl+F1** | Open this manual in a browser |

## Mouse

| Where | Action | Effect |
|---|---|---|
| Waterfall | Left click | Move the tone window to the pointer |
| Waterfall | Drag inside the tone window | Slide the window |
| Waterfall | Drag elsewhere | Drag the window centre with the pointer |
| Waterfall | Scroll wheel | Nudge the tones by 10 Hz |
| Waterfall | Right click | Retune the rig so the signal lands on the nominal tones |
| Waterfall | Middle click | Place or remove a notch |
| Receive pane | Click a word | Fill Call, RST R, Exch rcvd or Name |
| Macro button | Right click | Edit the macro |
| Band map, Skimmer list | Click a row | QSY or retune, and take the callsign |
| Band button | Hover | Show the frequency |

## Menu

| Item | Action |
|---|---|
| Preferences | Open the Preferences dialog |
| Configurations | Switch, clone, rename, reset or delete configurations |
| Band map | Show or hide the sidebar |
| Skimmer | Decode the whole passband |
| Send CW ID | Send the CW identifier now |
| Reset AFC | Return the decoders to the nominal tones |
| Clear RX | Clear the receive panes |
| Clear all text | Clear the receive and transmit panes |
| Clear notches | Remove every notch |
| Export ADIF… | Write the logbook to an ADIF file |
| Help | Open this manual in a browser (**Ctrl+F1**) |
| About gtty | Version and licence |

## Preferences pages

| Page | Contents |
|---|---|
| **Audio** | Input and output device, input channel |
| **Rig** | Hamlib model, device, speed; PTT method and device, poll interval; filter width on connect; dial frequency without rig |
| **RTTY** | Baud, tone preset, mark and space tones, stop bits; character set, unshift on space; AFC range, Net |
| **Decoders** | Per engine: enabled, pane, vote, mode and custom values; best-guess pane; Skimmer and channel step |
| **Transmit** | Keying method, FSK device and polarity, diddle, word mode, AFSK level; PTT through rig, rig mode on TX, LTRS preamble, PTT lead; Auto RX and delay, clear TX pane; Auto CQ interval and macro; TX monitor level; CW ID; Tune time-out |
| **Station** | Callsign, name, QTH, grid; logbook and SCP paths; waterfall range and direction; font size; timestamps; session file; appearance |
| **Cluster** | Host, port, login, commands; RTTY only, this band, maximum age |
| **Callbook** | Service, username, password, auto-fill |
| **Bands** | Dial frequency per band button |
| **Integration** | WSJT-X UDP (GridTracker) host, port and what to exchange; XML-RPC bind address and port |

## Defaults

| Setting | Default |
|---|---|
| Baud | 45.45 |
| Mark / space | 2125 / 2295 Hz (170 Hz shift, not reversed) |
| Stop bits (TX) | 1.5 |
| Character set | ITA2, unshift on space |
| AFC, ATC | On, range ±60 Hz |
| Net | On |
| Squelch | 0 |
| Engine A | Normal; B: Flutter; Correlator: on, pane hidden; codec2: off |
| Skimmer | Off, 50 Hz step |
| Keying | AFSK, 50 % level, diddle on, 2 LTRS preamble, 100 ms PTT lead |
| PTT | Through the rig, CAT command |
| Auto RX | Off, 2 s delay; TX pane cleared on RX |
| Auto CQ | F1 every 5 s |
| Tune time-out | 10 s |
| CW ID | Off, 20 WPM, mark tone |
| Waterfall | 300–3000 Hz, follows the rig's sideband |
| Font size | 12 px |
| Manual dial | 14080 kHz |
| Rig poll | 250 ms |
| Cluster | Port 7300, RTTY only, 30 min |
| GridTracker | 127.0.0.1:2237, off |
| XML-RPC | 127.0.0.1:7362, off |

## Files

| File | Purpose |
|---|---|
| `~/.config/gtty/gtty.conf` | Which configuration is active |
| `~/.config/gtty/profiles/<name>.conf` | One file per configuration, GLib key-file format |
| `~/.local/share/gtty/logbook.sqlite` | The logbook (SQLite) |
| `~/.local/share/gtty/MASTER.SCP` | Super Check Partial database (download separately) |
| `~/.local/share/gtty/sessions/gtty-YYYY-MM-DD.txt` | Daily session files |

## Band table

Band buttons and their default frequencies are listed under [Rig control](rig-control.md#band-buttons). The band of a frequency, used by the dupe check and the integrations, follows the usual amateur allocations from 160 m to 70 cm.

## Signal processing summary

The receive audio is resampled to 12 kHz. A 4096-point spectrum feeds the waterfall. The matched-filter engines mix the mark and space tones to baseband, filter each with a one-bit-long matched filter, normalise the envelopes with ATC, decide each bit from the middle of the bit period, verify the start and stop bits and drop characters that fail framing. AFC averages the instantaneous frequency over strong bits. The correlator matches whole characters against the 32 Baudot templates at every candidate start instant. A voter aligns the engines' characters in time and decides each one by confidence. Details are in the [architecture notes](ARCHITECTURE.md).
