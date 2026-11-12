---
title: The main window
nav_order: 4
---

# The main window

From top to bottom the window holds the header bar, the waterfall and tuning scope, the receive panes, the transmit pane, the band buttons, the macro buttons, the log row, the control row and a status line. A sidebar on the right holds the DX cluster band map and the Skimmer list.

The dividers between the waterfall, the receive panes and the transmit pane can be dragged.

## Header bar

| Element | Meaning |
|---|---|
| Gear button | Opens Preferences (**Ctrl+,**) |
| Frequency | The rig's dial frequency, or the manual dial frequency when no rig is connected, in kHz with two decimals |
| Mode | The rig's mode and filter width, *manual* when rig control is off, *rig offline* when it is on but the rig did not answer |
| RX / TX / TUNE / ID | What the transmitter is doing. Green while receiving, red while keyed |
| Sidebar button | Shows or hides the band map and Skimmer sidebar |
| ☰ menu | Preferences, Configurations, Band map, Skimmer, Modem-only view, Send CW ID, Reset AFC, Clear RX, Clear all text, Clear notches, Export ADIF, Help, About |

The window title shows the name of the active configuration when it is not *Default*.

## Waterfall and spectrum

The left part shows a spectrum of the receive audio above a scrolling waterfall. The shaded band is the tone window between the mark and space cursors, which follow the AFC. Notches are drawn as vertical marks. The frequency under the pointer is shown in the corner.

By default the direction follows the rig's sideband, so in RTTY-L or LSB higher audio frequencies are on the left, the same way as the rig's own scope. Change this under *Preferences → Station → Waterfall direction*. The displayed range is 300–3000 Hz by default and is set on the same page.

Mouse actions are described under [Tuning](tuning.md).

## Scope column

To the right of the waterfall:

- **XY scope**: the classic crossed-ellipse tuning display. Mark drives one axis, space the other. A well tuned signal shows two ellipses at right angles.
- **AFC readout**: how far the AFC has pulled the decoder from the nominal tones, with **SQL** appended while the squelch is closed.
- **Quality bar**: the primary decoder's signal quality, 0 to 1.
- **Level bar**: audio input level. Keep it out of the top band.
- **Filter** and **IF shift**: the rig's receiver filter width and passband shift, enabled while a rig is connected. See [Rig control](rig-control.md).

## Receive panes

The **Best guess** pane spans the full width. Each character in it is decided by a confidence-weighted vote of the decoder engines. The engine panes sit side by side beneath it, one per enabled engine with its pane switched on in *Preferences → Decoders*. The two matched-filter engines have a mode dropdown (Normal, Selective, Flutter, Custom) in their pane header.

Colors in the text:

| Color | Meaning |
|---|---|
| Blue, bold | A word that looks like a callsign |
| Green, bold, underlined | A callsign found in the Super Check Partial file |
| Red | Your own transmitted text, echoed into the Best guess pane |
| Gray, small | Timestamps, when enabled |

**Click a word** in any receive pane to use it:

- a callsign goes to the **Call** field,
- a `5nn` report goes to **RST R**,
- a number goes to **Exch rcvd**,
- a word of letters goes to **Name**.

Received text is kept to about 200 000 characters per pane; older text is dropped.

## Transmit pane

Type here. While transmitting, text is sent as you type and already-sent text turns gray. With *Word mode type-ahead* on, text is sent only after each space or Enter so a typo can still be fixed. The pane header reminds you of the keys: **Ctrl+T** transmit, **Ctrl+R** receive, **Esc** abort.

The pane is cleared when the transmitter returns to receive unless *Clear the TX pane on return to receive* is switched off in *Preferences → Transmit*.

## Band buttons

One button per band from 160 m to 6 m, each tuned to the customary RTTY segment. The current band is highlighted. With a rig connected the button QSYs the rig; without one it sets the manual dial frequency. Edit the frequencies under *Preferences → Bands*. Hovering a button shows its frequency.

Each band remembers where the dial last was, including across restarts, and its button returns you there. Click the button of the band you are already on to go to the preset frequency instead.

## Modem-only view

When a contest logger such as TLF drives gtty over XML-RPC, the logger has its own band switching, macros and log entry. **☰ → Modem-only view** (**Ctrl+M**) hides the band buttons, the macro buttons and the log row, leaving the waterfall, the receive panes, the transmit pane and the control row. The sidebar keeps its own toggle, so the Skimmer list can stay open. The F-key macros still work while hidden. The choice is saved with the configuration.

## Macro buttons

Twelve buttons in two rows, tied to **F1–F12**. Click or press the key to run a macro; right-click a button to edit it. See [Macros](macros.md).

## Log row

| Field | Purpose |
|---|---|
| **Call** | The station you are working. Typing a complete callsign shows worked-before information and triggers a callbook lookup |
| **RST S**, **RST R** | Reports sent and received, 599 by default |
| **Exch rcvd** | The received contest exchange |
| **Name** | The operator's name, filled from the logbook or callbook when known |
| **Serial** | Your next serial number; advances after each logged QSO |
| **DUPE** | Appears in red when the call is already in the log on this band in RTTY |
| Info label | *New*, or *Wkd 3×, last 2026-03-14 20m Bob*, then the callbook summary |
| QSO count | Total QSOs in the logbook |
| **Log QSO** | Logs the QSO (**Ctrl+L**) |
| **Clear** | Clears the fields |

## Control row

| Control | Meaning |
|---|---|
| **TX** | Toggle the transmitter (**Ctrl+T** / **Ctrl+R**) |
| **Tune** | Key a steady mark tone to set the drive level; switches off after the configured time-out |
| **Abort** | Drop PTT immediately and discard unsent text (**Esc**) |
| **Clear text** | Clear the receive and transmit panes (**Ctrl+K**) |
| **Baud** | 45.45, 50, 75 or 100 baud |
| **Shift** | 170, 200, 425, 450 or 850 Hz |
| **Tones** | Mark/space presets; *Custom* when the tones were set freely in Preferences |
| **AFC** | Automatic frequency control follows the signal within the AFC range |
| **ATC** | Automatic threshold control normalizes the mark and space levels |
| **Rev** | Reverse: space tone below mark |
| **Net** | Transmit on the AFC-tracked receive tones rather than the nominal ones |
| **Auto RX** | Return to receive automatically once the transmit buffer has been empty for the configured delay |
| **Auto CQ** | Repeat the CQ macro with a pause between calls until a callsign is entered |
| **Squelch** | Mutes decoder output below this quality; 0 is off |

## Status line

Shows rig errors, the Super Check Partial matches for the partial call in the Call field, and similar short messages. Longer messages appear as toasts at the bottom of the window.

## Sidebar

The sidebar has two pages, **Cluster** and **Skimmer**, switched at its top. The **This band** box limits the cluster band map to the current band. The arrow button collapses the sidebar; the sidebar button in the header bar reopens it. See [DX cluster band map](cluster.md) and [Decoders](decoders.md#skimmer).
