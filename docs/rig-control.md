---
title: Rig control
nav_order: 8
---

# Rig control

gtty controls the transceiver through [Hamlib](https://hamlib.github.io/), so any rig Hamlib supports can be used. Rig control is optional: without it, gtty still decodes and transmits, and a manual dial frequency stands in for the rig's.

## Connecting a rig

Under *Preferences → Rig*:

1. Tick **Enable rig control**.
2. Choose the **Rig model**. The list is searchable; the number in brackets is Hamlib's model number.
3. Enter the **Device**: the serial port, for example `/dev/ttyUSB0` on Linux or `/dev/cu.usbserial-XXXXXXXX` on macOS (`ls /dev/cu.*` lists them; use the `cu.` name, not `tty.`), or `host:port` for a NET rigctl connection (model 2, *Hamlib NET rigctl*).
4. Set the **Serial speed** to match the rig's CAT rate menu. 0 uses the backend's default.
5. Choose the **PTT method**: *CAT command*, *Serial DTR*, *Serial RTS* or *None (VOX)*. For DTR or RTS PTT, a different serial port can be named under **PTT serial device**; blank uses the rig device.

A PTT line on a port of its own is keyed by gtty directly rather than through Hamlib. It therefore works with any rig model, including NET rigctl (where `rigctld` would otherwise decide how PTT is keyed), and with **Enable rig control** switched off. If the port cannot be opened gtty says so when it starts, and again as *PTT failed* when you transmit. Opening a serial port raises DTR and RTS for a moment before gtty releases them, so the rig may key briefly when gtty starts or the settings change.
6. **Poll interval** sets how often frequency and mode are read (250 ms by default).

When the rig answers, the header bar shows its frequency, mode and filter width. When rig control is enabled but the rig does not answer the mode label reads *rig offline* and a toast reports the Hamlib error.

{: .warning }
Only one program may use the CAT port at a time. Close other rig control software, or point everything at a shared `rigctld` and select model 2 in gtty.

## What rig control does

- Shows frequency, mode and filter width in the header bar.
- Keys PTT and switches the rig to the **Rig mode on TX** set under *Preferences → Transmit*.
- Lets the band buttons, the band map, the Skimmer and the right-click on the waterfall QSY the rig.
- Makes the waterfall follow the rig's sideband.
- Fills the frequency and band in logged QSOs and in reports to GridTracker and the XML-RPC server.
- Drives the receiver filter and IF shift.

## Receiver filter and IF shift

The **Filter** dropdown under the tuning scope selects the receiver filter width from 250 Hz to 3000 Hz. *Rig* leaves whatever the rig has. The dropdown follows the width the rig reports, so a change made on the front panel is reflected. **IF shift** moves the passband in 50 Hz steps when the rig supports it; the row is hidden otherwise.

**Filter width to select on connect** under *Preferences → Rig* applies a width automatically whenever the rig connects, for example 500 Hz for RTTY. 0 leaves the rig alone.

## Band buttons

The band buttons QSY the rig to the frequency set for each band under *Preferences → Bands*. The defaults are the centers of the customary RTTY segments:

| Band | kHz | Band | kHz |
|---|---:|---|---:|
| 160 m | 1838 | 17 m | 18100 |
| 80 m | 3580 | 15 m | 21080 |
| 40 m | 7040 | 12 m | 24920 |
| 30 m | 10142 | 10 m | 28080 |
| 20 m | 14080 | 6 m | 50300 |

Each band remembers the frequency it was last on, and its button returns there; clicking the button of the current band goes to the preset. The current band is highlighted. The band of the dial frequency is also what the logbook's dupe check and GridTracker use.

## Without a rig

With rig control off, **Dial frequency without rig** under *Preferences → Rig* is the frequency gtty believes you are on. The band buttons set it, the header bar shows it, and the logbook, GridTracker and the XML-RPC server report it. Keep it in step with the rig's dial so logged QSOs land on the right band.

## Example: Yaesu FT-710 and FTDX10

The radio's USB port presents two serial ports (a CP2105). Interface 00 (`/dev/ttyUSB0`, "Enhanced COM") carries CAT; interface 01 (`/dev/ttyUSB1`, "Standard COM") provides the DTR and RTS lines the radio can use for FSK keying and PTT.

**Rig**: *Preferences → Rig*, model **Yaesu FT-710 (1049)** (or FTDX10), device `/dev/ttyUSB0`, serial speed matching the radio's CAT RATE menu (38400), PTT method **CAT command**. Set the radio's CAT RTS menu to OFF.

**True FSK**: *Preferences → Transmit*, method **FSK via DTR**, FSK serial device `/dev/ttyUSB1`, Rig mode on TX **RTTY** (RTTY-L). On the radio set RTTY SHIFT PORT = DTR, POLARITY TX = NOR, MARK FREQUENCY 2125, SHIFT 170. Flip gtty's **Invert FSK polarity** if the far end prints garbage. Receive in RTTY-L with gtty's **Rev** off, or in RTTY-U (Hamlib "RTTYR") with Rev on.

**AFSK instead**: the FT-710 ignores audio input in RTTY mode, so use Rig mode on TX **PKTUSB** (DATA-U) with Rev on, or **LSB** with Rev off, and feed the USB codec. Keep the ALC just barely moving.

To test the CAT connection without the GUI run `gtty-cli rig --model 1049 --device /dev/ttyUSB0 --serial-baud 38400` with gtty closed.

## Listing Hamlib models

```
gtty-cli rigs icom
```

prints every Hamlib model whose manufacturer or name contains the filter, with its model number and backend status.
