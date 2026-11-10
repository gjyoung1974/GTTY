---
title: Macros
nav_order: 10
---

# Macros

The twelve macro buttons, tied to **F1–F12**, send canned messages. A macro is text with tags in angle brackets that are replaced or acted on when the macro runs. The tags are the ones fldigi uses, so fldigi macros can be pasted in.

**Right-click a button** to edit its label and text. Changes are saved to the active configuration at once.

## Tags

| Tag | Effect |
|---|---|
| `<TX>` | Key the transmitter and start sending what follows |
| `<RX>` | Return to receive after everything queued has been sent |
| `<TX/RX>` | Both, in that order |
| `<MYCALL>` `<MYNAME>` `<MYQTH>` `<MYLOC>` | Your callsign, name, QTH and grid from *Preferences → Station* |
| `<CALL>` `<NAME>` `<QTH>` `<LOC>` | The other station's callsign and name from the log row, and their QTH and grid when known from the logbook or callbook |
| `<RST>` | The report you send (RST S) |
| `<MYRST>` | The report you received (RST R) |
| `<CNTR>` | Your serial number, three digits with leading zeros |
| `<INCR>` `<DECR>` | Step the serial number up or down |
| `<XCHG>` | The sent exchange (empty unless set by a logger over XML-RPC) |
| `<LOG>` | Log the QSO, as Ctrl+L does |
| `<CLRRX>` | Clear the receive panes |
| `<CLRTX>` | Abort and clear the transmit pane |
| `<TIME>` | UTC time as HHMM |
| `<ZDT>` | UTC date and time as YYYY-MM-DD HH:MMZ |
| `<CR>` | A new line |
| `<CWID>` | Send the CW identifier |

Tag names are not case sensitive. An unknown tag is sent literally. A new line in the macro text is sent as a carriage return and line feed.

## Default macros

| Key | Label | Text |
|---|---|---|
| F1 | CQ | `<TX>CQ CQ CQ DE <MYCALL> <MYCALL> <MYCALL> PSE K` ⏎ `<RX>` |
| F2 | ANS | `<TX><CALL> <CALL> DE <MYCALL> <MYCALL> K` ⏎ `<RX>` |
| F3 | RPT | `<TX><CALL> DE <MYCALL> UR RST <RST> <RST> NAME <MYNAME> QTH <MYQTH> <MYQTH> BTU <CALL> DE <MYCALL> K` ⏎ `<RX>` |
| F4 | 73 | `<TX><CALL> DE <MYCALL> TNX FER QSO 73 SK` ⏎ `<RX><LOG>` |
| F5 | CQ TEST | `<TX>CQ TEST <MYCALL> <MYCALL> CQ` ⏎ `<RX>` |
| F6 | EXCH | `<TX><CALL> <RST> <CNTR> <CNTR> <CALL>` ⏎ `<RX>` |
| F7 | TU QRZ | `<TX>TU <MYCALL> CQ` ⏎ `<RX><LOG>` |
| F8 | AGN | `<TX>AGN AGN <CALL> <CALL>` ⏎ `<RX>` |
| F9 | MYCALL | `<MYCALL> <MYCALL>` |
| F10 | CALL | `<CALL> ` |
| F11 | TX | `<TX>` |
| F12 | RX | `<RX>` |

F1 to F4 cover a casual QSO: call CQ, answer, send the report, sign off and log. F5 to F7 are the contest run: CQ TEST, send the exchange, log and move on. F9 to F12 insert text or key the transmitter without sending anything else, for use while typing by hand.

## Tips

- A macro that ends in `<RX>` waits until the text has been sent, then drops PTT, so you can press the next F-key while it is still sending.
- Put `<LOG>` after `<RX>` so the QSO is logged as the transmission ends and the serial advances before the next one.
- **Auto CQ** repeats the macro chosen under *Preferences → Transmit* (F1 by default) at the set interval; pick F5 for a contest.
- Macros belong to the configuration, so a contest configuration can have its own set. See [Configurations](configurations.md).
- Loggers connected over XML-RPC can run a macro by number (`main.run_macro`, 0-based).
