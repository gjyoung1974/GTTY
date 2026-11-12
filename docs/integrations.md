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
- **TLF**: add `FLDIGI` and `RTTYMODE` to `logcfg.dat`. TLF reads the receive text, sends its messages through gtty and keys TX and RX. See [Using gtty with TLF](#using-gtty-with-tlf) below.

With a logger in charge, [Modem-only view](main-window.md#modem-only-view) (**Ctrl+M**) hides gtty's own band buttons, macros and log row.

## Using gtty with TLF

[TLF](https://github.com/Tlf/tlf) is a console contest logger. It has no RTTY modem of its own and normally drives fldigi; gtty takes fldigi's place. TLF is the logger and keeps the keyboard; gtty is the modem and shows the waterfall.

### 1. Set up gtty

1. *Preferences → Integration*: tick **Enable XML-RPC server**. Leave the address at 127.0.0.1 and the port at 7362.
2. Set up audio, keying and your tones as for normal operating, and check that gtty decodes and transmits by itself first.
3. Press **Ctrl+M** for the [modem-only view](main-window.md#modem-only-view). TLF has its own band switching, messages and log entry, so gtty's are not needed.

A separate [configuration](configurations.md) for contesting with TLF keeps these settings apart from everyday operating.

### 2. Set up TLF

Make a directory for the contest and put a `logcfg.dat` in it. The lines that matter for gtty:

```
CALL=N0CALL
RULES=cqww
LOGFILE=cqww-rtty.log

RTTYMODE
FLDIGI
DIGI_RIG_MODE=LSB
```

| Line | Purpose |
|---|---|
| `RTTYMODE` | Starts TLF in RTTY mode rather than CW |
| `FLDIGI` | Connects to `http://localhost:7362/RPC2`, where gtty listens. For another port write `FLDIGI=http://localhost:7400/RPC2` |
| `DIGI_RIG_MODE` | The mode TLF puts the rig in for RTTY: `LSB`, `USB`, `RTTY` or `RTTYR`. With `FLDIGI` set TLF defaults to USB, so set this to the mode you use with gtty: usually `LSB` for AFSK and `RTTY` for FSK |

`DIGI_RIG_MODE` only has an effect when TLF controls the rig; see [Sharing the rig](#3-sharing-the-rig).

The F-key messages for RTTY are the `DKF1` to `DKF12` lines. In them `%` is your call, `@` the other station's call, `#` the serial number, `[` the report and `|` a new line:

```
DKF1=|CQ TEST % % CQ |
DKF2=|@ DE % |
DKF3=|@ 599 # # |
```

A digital message that is not defined takes the text of the CW message for the same key. `man tlf` lists every keyword; `/usr/share/tlf/logcfg.dat` is a commented template and `/usr/share/tlf/rules` holds the contest rule files.

### 3. Sharing the rig

Only one program can hold the CAT port. Pick one:

- **gtty owns the rig.** Leave `RADIO_CONTROL` out of `logcfg.dat`. Simplest, but TLF then does not know the frequency, so you set the band in TLF by hand.
- **Both through rigctld.** Start `rigctld` for your rig, select Hamlib model 2 (NET rigctl) in gtty under *Preferences → Rig* with `localhost:4532` as the device, and in `logcfg.dat` set:

  ```
  RADIO_CONTROL
  RIGMODEL=2
  RIGPORT=localhost
  ```

  TLF then follows the rig's frequency and band, and both programs can QSY it. See [Rig control](rig-control.md).

### 4. Operate

Start gtty first, then run `tlf` in the contest directory. Keep the keyboard focus in TLF and use gtty for tuning: click a signal on the waterfall or a line in the Skimmer list.

| Key in TLF | Action |
|---|---|
| Type a call, then **Space** | Moves to the exchange field |
| **Enter** | Sends the next message for the state of the QSO and logs it when the exchange is complete |
| **+** | Switches between CQ (running) and S&P |
| **F1–F12** | Send the `DKF` messages through gtty |
| **Escape** | Stops transmitting and clears the entry |
| `:DIG`, `:CW`, `:SSB` | Change mode |
| `:FLDIGI` | Switches the connection to gtty off and on |
| `:WRI` | Writes the Cabrillo file |
| **Alt+H** | TLF's key help |
| **Alt+X** or `:EXIT` | Quit |

Received text in TLF is gtty's Best guess stream. Text TLF sends appears in gtty's transmit pane as it goes out.

### If it does not work

- **No text in TLF, or F-keys do nothing**: check that the XML-RPC server is enabled in gtty and the port matches the `FLDIGI` line, then type `:FLDIGI` twice in TLF to reconnect. gtty must be running before TLF connects.
- **fldigi is also running**: it already holds port 7362. Close it or give gtty another port.
- **The rig changes sideband when TLF starts**: set `DIGI_RIG_MODE` as above.
- **Rig control fails in one program**: both are opening the CAT port; use rigctld.

### Methods

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
