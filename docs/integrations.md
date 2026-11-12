---
title: Integrations
nav_order: 13
---

# Integrations

gtty talks to other station software two ways: the WSJT-X UDP protocol that GridTracker2 and similar programs listen to, and an fldigi-compatible XML-RPC server that loggers such as CQRLOG and TLF drive. Both are set up under *Preferences → Integration*.

## GridTracker2 and the WSJT-X UDP protocol

gtty behaves like a WSJT-X instance named `gtty`. Everything it hears is plotted on the GridTracker map, QSOs you log are forwarded so GridTracker can push them to your online logs, and clicking a station in GridTracker fills gtty's Call field.

### Setup

1. In GridTracker2, *Settings → General*: note the WSJT-X/JTDX receive port (UDP 2237 on 127.0.0.1 by default).
2. In gtty, *Preferences → Integration → WSJT-X UDP protocol*: tick **Enable** and set the same **Host** and **UDP port**.
3. Set your callsign and grid under *Preferences → Station*, so GridTracker knows who is reporting.
4. Without a connected rig, set *Preferences → Rig → Dial frequency without rig* to the frequency you are listening on. GridTracker only plots stations it can place on a band.

If WSJT-X is also running, GridTracker accepts several senders on the same port. A multicast address such as `224.0.0.1` works as well.

### What is exchanged

| gtty → GridTracker | GridTracker → gtty |
|---|---|
| Heartbeat every 15 s | **Reply** (a station clicked on the map) → the callsign goes to the Call field and its grid is remembered for the log |
| **Status**: dial frequency, mode RTTY, the DX call, the report, TX state, the tone frequency | **Halt TX** → the transmission is aborted |
| **Decode**: one `CQ <CALL> [GRID]` message per callsign heard in the Best guess pane, at most once a minute per call, with a grid when one appears on the same line | **Free text** → inserted into the transmit pane, and sent if asked |
| **QSO Logged** and **Logged ADIF** when you log a QSO | |

The three switches under **What to exchange** turn off the decode reports, the logged-QSO reports or the acceptance of replies.

## fldigi-compatible XML-RPC

Loggers that drive fldigi can drive gtty. Tick **Enable XML-RPC server** and set the **Bind address** (127.0.0.1 by default) and **Port** (7362, fldigi's default). If fldigi itself is running, give gtty a different port.

Then point the logger's fldigi settings at `127.0.0.1:7362`:

- **CQRLOG**: *Preferences → fldigi/wsjtx interface*, fldigi port 7362. CQRLOG reads decoded text, fills its log fields from gtty's log row and can send text.
- **TLF**: set `FLDIGI=127.0.0.1:7362` in `logcfg.dat` and use RTTY mode. TLF reads the receive text, keys TX and RX, sets the frequency and drives the log fields.

### Methods

With a logger in charge, [Modem-only view](main-window.md#modem-only-view) (**Ctrl+M**) hides gtty's own band buttons, macros and log row.

gtty answers the fldigi methods loggers use:

| Group | Methods |
|---|---|
| Identity | `fldigi.name` (returns `gtty`), `fldigi.version`, `fldigi.version_struct`, `fldigi.name_version`, `fldigi.list`, `system.listMethods` |
| Modem | `modem.get_name` (RTTY), `modem.get_names`, `modem.set_by_name`, `modem.get_carrier`, `modem.set_carrier`, `modem.inc_carrier`, `modem.get_bandwidth` (the shift), `modem.get_afc_search_range` |
| Frequency and TX | `main.get_frequency`, `main.set_frequency`, `main.inc_frequency`, `main.tx`, `main.rx`, `main.abort`, `main.get_trx_status`, `main.get_trx_state`, `main.get_sideband` |
| Decoder | `main.get_afc`, `main.set_afc`, `main.toggle_afc`, `main.get_squelch`, `main.set_squelch`, `main.toggle_squelch`, `main.get_squelch_level`, `main.set_squelch_level`, `main.get_reverse`, `main.set_reverse`, `main.toggle_reverse` |
| Text | `text.get_rx_length`, `text.get_rx`, `text.clear_rx`, `text.add_tx`, `text.add_tx_bytes`, `text.clear_tx` |
| Macros | `main.run_macro` (0-based), `main.get_max_macro_id` |
| Log fields | `log.get_call`, `log.set_call`, `log.get_name`, `log.set_name`, `log.get_rst_in`, `log.set_rst_in`, `log.get_rst_out`, `log.set_rst_out`, `log.get_exchange`, `log.set_exchange`, `log.get_serial_number`, `log.set_serial_number`, `log.get_serial_number_sent`, `log.get_frequency`, `log.get_time_on`, `log.get_time_off`, `log.get_band`, `log.get_locator`, `log.set_locator`, `log.get_qth`, `log.set_qth`, `log.clear` |
| Rig | `rig.get_name`, `rig.set_name`, `rig.get_frequency`, `rig.set_frequency`, `rig.get_mode`, `rig.set_mode`, `rig.get_modes`, `rig.get_notch`, `rig.set_notch` |

The receive text served to loggers is the Best guess stream. `main.set_frequency` QSYs the rig, or sets the manual dial when no rig is connected. Methods fldigi has but gtty has no use for (RSID, lock, spotting, notes) are accepted and answer with neutral values so loggers do not fail.
