---
title: Transmitting
nav_order: 7
---

# Transmitting

gtty can key a transmitter four ways. All of them are set up under *Preferences → Transmit*.

## Keying methods

| Method | How it works | Needs |
|---|---|---|
| **AFSK (audio)** | The two tones are generated on the sound card and the rig is used in a data or SSB mode | Audio to the rig, PTT through the rig (CAT, DTR or RTS) or VOX |
| **FSK via serial UART TXD** | The serial port is set to the RTTY baud rate with 5 data bits and 2 stop bits, and the characters are sent as serial data. TXD idles at mark | A serial port whose TXD line reaches the rig's FSK input, usually through a transistor or opto-coupler |
| **FSK via DTR** / **FSK via RTS** | The handshake line is switched between mark and space by a timed thread | A serial port whose DTR or RTS line reaches the rig's FSK input |
| **CAT text** | The text is handed to the rig's own keyer through Hamlib (`rig_send_morse`, the `KY` command). In RTTY mode those rigs key FSK from it | Elecraft K3, K4, KX3 or Kenwood TS-890 |

True FSK gives the cleanest signal because the rig makes the shift itself and audio levels cannot overdrive anything. The UART method has the lowest timing jitter and works with FTDI, CP210x, PL2303 and native serial ports; the interface's transistor normally inverts the signal, which is what rigs expect. For DTR or RTS keying, tick **Invert FSK polarity** if the far end prints garbage.

Enter the port under **FSK serial device** (for example `/dev/ttyUSB1` on Linux, or `/dev/cu.usbserial-XXXXXXXX` on macOS). On Linux your user must be in the `dialout` group; on macOS no group membership is needed.

{: .note }
On macOS the *FSK via UART* method needs a serial driver that accepts the 45 baud RTTY rate. FTDI adapters do; some CP210x and CDC-ACM drivers refuse it, and gtty reports the error when it opens the port. *FSK via DTR* / *RTS* and AFSK work with any adapter.

{: .note }
Many rigs with a built-in USB sound card present two serial ports. One carries CAT; the other provides DTR and RTS that the rig can route to its FSK and PTT inputs. The [FT-710 example](rig-control.md#example-yaesu-ft-710-and-ftdx10) shows a complete setup.

## AFSK levels

**AFSK level** sets the tone amplitude as a percentage of full scale. Start low and raise it until the rig's ALC just begins to move. An overdriven RTTY signal is wide and hard to copy.

## Sequencing

- **Key PTT through the rig**: use Hamlib PTT (the method chosen under *Preferences → Rig*). Untick it to rely on VOX or an external keying interface.
- **Rig mode on TX**: the mode to select when PTT is keyed. Leave it empty to leave the rig alone, or choose RTTY, RTTYR, LSB, USB, PKTLSB or PKTUSB. Use RTTY for FSK, a data mode or SSB for AFSK.
- **LTRS preamble characters**: letters-shift characters sent before the text to let the far end's decoder lock. Two by default.
- **PTT lead time**: the delay in milliseconds between keying PTT and the first character, so the rig's relays have settled. 100 ms by default.
- **Diddle**: send LTRS while the transmitter is keyed and nothing is queued, so the signal keeps its bit clock and the far end keeps its lock. On by default.

## Typing and sending

Press **Ctrl+T** or the **TX** button to key the transmitter; text typed in the transmit pane then goes out as it is typed. Sent text turns gray. A macro with a `<TX>` tag does the same.

- **Word mode type-ahead** sends only after each space or Enter, so a typo can be fixed before the word is sent. With Auto RX on, a dangling partial word is sent once the Auto RX delay has passed.
- **Ctrl+R** or the **TX** button again finishes sending what is queued and then drops PTT.
- **Esc** or **Abort** drops PTT immediately and clears the transmit pane.
- **Clear text** (**Ctrl+K**) clears the receive panes and the transmit pane. While transmitting only the unsent text is dropped, so the record of what went out stays visible.

Your transmitted text is echoed in red into the Best guess pane and written to the session file.

## After transmitting

- **Auto RX**: return to receive once the transmit buffer has been empty for the **Auto RX delay** (2 s by default). With Auto RX off the transmitter stays keyed, sending diddle, until you press RX, which is what you want when typing a long over by hand. The box in the control row mirrors this setting.
- **Clear the TX pane on return to receive**: on by default.

## Auto CQ

Tick **Auto CQ** in the control row to repeat a macro, F1 by default, with a pause between calls (5 s by default). Both are set under *Preferences → Transmit → Auto CQ*. Auto CQ stops as soon as a callsign is entered in the Call field, whether typed, clicked in the receive text or taken from the Skimmer, the band map or GridTracker, and when a transmission is aborted.

## Tune

The **Tune** button keys the transmitter with a steady mark tone and no text, for setting drive level, ALC and an antenna tuner. It switches off by itself after the **Tune time-out** (10 s by default; 0 keeps it on until you press the button again). With **Net** on, Tune uses the AFC-tracked mark frequency.

## CW identifier

gtty can send your callsign in Morse. **Send CW ID** in the menu sends it at once; **Send CW ID at the end of every transmission** appends it automatically; the `<CWID>` macro tag sends it from a macro. The text (your callsign unless **ID text** is set), speed and, for AFSK, the tone are set under *Preferences → Transmit → CW identifier*. With FSK or CAT keying the ID is sent by keying PTT, which carries the mark tone, so **Key PTT through the rig** must be on. The header bar shows **ID** while it is sent.

## TX monitor

With FSK or CAT keying the sound card output is free, so gtty can play an AFSK copy of the keyed text on it. Set **Monitor level** above zero to hear what you send.

## Net

With **Net** ticked, the transmitter uses the AFC-tracked receive tones, so you reply on the other station's exact frequency. See [Tuning](tuning.md#afc).
