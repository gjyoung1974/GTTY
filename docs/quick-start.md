---
title: Quick start
nav_order: 3
---

# Quick start

This page takes a fresh installation to a first decoded signal and a first logged QSO. Everything here is covered in more depth in the later chapters.

## 1. Station details

Open Preferences with the gear button at the left of the header bar or **Ctrl+,** and go to **Station**. Enter your callsign, name, QTH and grid locator. The callsign is used by the macros, the CW identifier, the DX cluster login and GridTracker, so set it first.

Choose the appearance (follow system, light or dark) on the same page if you like.

## 2. Audio

On **Preferences → Audio** pick the input device that carries receive audio from the rig and the output device that feeds its transmit audio. The list shows every PortAudio device with its host API in brackets. Leave both on *(default)* to use the system's default devices.

If the receive audio only arrives on one channel of a stereo input, set **Input channel** to Left or Right; *Mix* averages both.

Close the dialog. Settings are applied when the dialog closes. The level bar under the tuning scope should move with the rig's audio and the waterfall should show the band. If a banner appears across the top of the window, gtty could not open the audio device; see [Troubleshooting](troubleshooting.md).

## 3. Rig (optional but recommended)

On **Preferences → Rig** tick **Enable rig control**, choose the rig model, enter the serial device and the serial speed, and pick the PTT method. The frequency and mode then show in the header bar, the band buttons QSY the rig, and the waterfall follows the rig's sideband.

Without a rig, set **Dial frequency without rig** to the frequency you are listening on so the band, the logbook and GridTracker know where you are.

See [Rig control](rig-control.md) for the details and an FT-710 example.

## 4. Tune a signal

Tune the rig to the RTTY segment of a band (the **Band** buttons do this; 20 m is 14080 kHz by default). RTTY signals appear on the waterfall as two parallel lines 170 Hz apart.

Click on a signal, between its two lines. The shaded tone window moves there, AFC pulls the decoder onto the tones, the XY scope shows the two crossed ellipses, and text appears in the **Best guess** pane and in the engine panes beneath it.

Right-click a signal instead to retune the rig so the signal lands exactly on the nominal tones (2125 / 2295 Hz by default).

The *Tuning* chapter explains the waterfall, the scope and the AFC, ATC, Rev and Net controls.

## 5. Transmit

On **Preferences → Transmit** choose the keying method.

- **AFSK (audio)** needs only the sound card. Set the rig to a data mode and keep the ALC just barely moving.
- **FSK via serial UART TXD**, **FSK via DTR** or **FSK via RTS** key the rig's FSK input through a serial port. Enter the port under **FSK serial device**.
- **CAT text** uses the rig's own text keyer (Elecraft K3/K4/KX3, Kenwood TS-890).

Set **Rig mode on TX** to the mode the rig should switch to when PTT is keyed (RTTY for FSK, PKTUSB or LSB for AFSK on most rigs).

Press **Tune** in the control row to key a steady mark tone and set the drive level. It switches off after the time-out set in Preferences (10 s by default).

To send text, type into the **Transmit** pane and press **Ctrl+T** (or the **TX** button). Text goes out as you type. **Ctrl+R** returns to receive once everything typed has been sent; **Esc** aborts immediately. The F1–F12 buttons send the macros; the defaults cover a complete QSO.

## 6. A first QSO

1. Press **F1** (CQ). The macro keys the transmitter, sends `CQ CQ CQ DE <your call> ... PSE K` and returns to receive.
2. When a station answers, click their callsign in the receive text. It lands in the **Call** field. Clicking a `599`-style report fills **RST R**; clicking a name fills **Name**.
3. Press **F3** (RPT) to send the report, your name and QTH.
4. When they are done, press **F4** (73). This macro also logs the QSO (the `<LOG>` tag) and the serial number advances.

**Ctrl+L** logs the QSO at any time; the **Log QSO** button does the same. A red **DUPE** next to the serial number means the call is already in the log on this band.

## 7. Next steps

- Load a Super Check Partial file (MASTER.SCP) under **Preferences → Station** to have known contest callsigns underlined in the receive text.
- Enable **GridTracker** or the **XML-RPC** server under **Preferences → Integration**.
- Connect to a **DX cluster** under **Preferences → Cluster** to get a band map of RTTY spots.
- Turn on the **Skimmer** from the menu to see every station in the passband.
- Clone the configuration from the **Configurations** menu before setting up a contest layout, so your everyday settings stay intact.
