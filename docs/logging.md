---
title: Logging
nav_order: 9
---

# Logging

gtty keeps an SQLite logbook, checks for dupes, exports ADIF, highlights known callsigns with Super Check Partial, looks callsigns up in a callbook and can write a daily session file of everything received and sent.

## The log row

The log row sits below the macro buttons: **Call**, **RST S**, **RST R**, **Exch rcvd**, **Name**, **Serial**, then the DUPE flag, an info label, the QSO count and the **Log QSO** and **Clear** buttons. See [The main window](main-window.md#log-row).

Fields are filled by:

- **clicking a word** in a receive pane (callsign, 5nn report, number or name),
- the **Skimmer** list, the **band map** and **GridTracker**, which put a callsign in the Call field,
- the **logbook** and the **callbook**, which fill Name and remember QTH and grid for the log entry,
- **loggers** driving the XML-RPC interface,
- typing. The Call field is forced to upper case.

## Logging a QSO

**Ctrl+L**, the **Log QSO** button or the `<LOG>` macro tag records the QSO with the current UTC time, the dial frequency and band, mode RTTY, both reports, the received exchange, the sent serial number as the sent exchange, the name and, when known from the logbook, the callbook or GridTracker, the QTH and grid. The serial number then advances, the fields clear, and a toast confirms the call. The QSO is also reported to GridTracker when that is enabled.

The logbook lives at `~/.local/share/gtty/logbook.sqlite` by default; the path is set under *Preferences → Station → Logbook database*. The count of QSOs appears next to the Log QSO button.

## Dupe check and worked-before

As soon as a callsign is in the Call field, **DUPE** appears in red if that call is already in the log on the current band in RTTY. The same check colors rows in the band map and the Skimmer list: dupes are dimmed, calls never worked are in the accent color.

Once a complete callsign has been typed (600 ms after the last keystroke), the info label shows *New* or how often and when you last worked the station, for example *Wkd 3×, last 2026-03-14 20m Bob*. With **Fill Name, QTH and grid from the lookup** on (*Preferences → Callbook*), the name from the last QSO is filled in and QTH and grid are remembered for the log entry.

## Super Check Partial

A Super Check Partial file (MASTER.SCP, available from [supercheckpartial.com](https://www.supercheckpartial.com/)) lists callsigns active in contests. Put the path under *Preferences → Station → Super Check Partial file*; the default is `~/.local/share/gtty/MASTER.SCP`. When loaded:

- callsigns in the file are underlined in green in the receive text, while other callsign-shaped words are blue;
- with three or more characters in the Call field, the status line shows up to twelve calls containing that fragment.

## Callbook lookup

*Preferences → Callbook* chooses **QRZ.com XML** (needs an XML subscription) or **HamQTH** (free account) and takes the username and password. When a complete callsign has been typed, gtty looks it up and shows name, QTH and grid in the info label after the worked-before text; with auto-fill on, the first name goes to the Name field and QTH and grid are remembered for the log entry. Values already filled from the logbook are kept.

{: .warning }
The password is stored in plain text in the configuration file under `~/.config/gtty/profiles/`.

Lookups need a build with libcurl; the Debian packages and the AppImage have it.

## ADIF export

**Export ADIF…** in the menu writes the whole logbook to a file (`gtty-log.adi` by default) for import into another logger or an online log. GridTracker can also forward each QSO as it is logged; see [Integrations](integrations.md).

## Timestamps and the session file

Under *Preferences → Station*:

- **Timestamps at the start of each received line** prefixes each line in the receive panes with the UTC time.
- **Save received and sent text to a daily session file** appends everything from the Best guess pane and everything you send to `gtty-<date>.txt` in the **Session file directory** (`~/.local/share/gtty/sessions` by default). Lines are stamped with the UTC time and marked RX or TX; the session start and end are recorded.

The session file is the raw material for tuning the decoders on your own recordings, together with `gtty-cli record`.
