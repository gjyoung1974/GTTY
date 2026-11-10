---
title: Troubleshooting
nav_order: 15
---

# Troubleshooting

## A banner says the audio input could not be opened

Nothing is decoded until the input works. The banner's **Preferences** button opens the Audio page.

- Choose a specific device rather than *(default)*. `gtty-cli devices` lists what PortAudio sees.
- On a PipeWire desktop install `pipewire-alsa`; without it the `default` and `pipewire` ALSA devices cannot be opened. The `.deb` recommends it, so `apt install ./gtty_*.deb` pulls it in and `dpkg -i` does not.
- On macOS, if the device opens but the waterfall stays blank and the level meter reads zero, gtty (or, when run from a build tree, the terminal running it) has not been granted microphone access. Allow it under System Settings → Privacy & Security → Microphone and restart gtty.
- Another program may hold the device exclusively. Close it or use a PipeWire or PulseAudio device instead of the raw hardware device.
- A device that was configured but is no longer present falls back to the system default with a toast.

## The waterfall shows the band but nothing decodes

- Click on a signal, between its two lines. The tone window must be on the signal, or within the AFC range of it.
- Check **Rev**. In RTTY-L or LSB leave it off with the default tones; in USB or an upper-sideband data mode turn it on.
- Check **Baud** and **Shift**. Amateur RTTY is 45.45 baud, 170 Hz.
- Lower the **Squelch**. At 0 everything prints.
- Look at the level bar under the scope. If it is pinned at the top, reduce the rig's audio output or the input gain; if it barely moves, increase them.

## Text decodes as garbage

- **Rev** is wrong. Toggle it.
- The signal is reversed at the sending end; toggle Rev for that station.
- The shift is wrong: a 450 or 850 Hz signal decoded at 170 Hz prints nothing useful.
- The station uses US-TTY figures; try the character set under *Preferences → RTTY*.
- The signal is off the tones further than the AFC range. Click on it or raise the AFC range.

## The other station prints garbage from my FSK

- Toggle **Invert FSK polarity** (DTR and RTS keying).
- Check the rig's FSK polarity menu and its mark and shift settings (2125 Hz, 170 Hz).
- Make sure the rig is in its FSK mode (RTTY), not a data or SSB mode, and that *Rig mode on TX* switches it there.
- With UART keying, make sure the keying interface is on the TXD line and that the port is not also used for CAT.

## PTT does not key

- *Preferences → Rig → PTT method* must match the wiring: CAT command, DTR, RTS, or None for VOX.
- For DTR or RTS PTT on a second port, set **PTT serial device**.
- Your user must be in the `dialout` group (log out and in after adding it).
- On Yaesu rigs, the CAT RTS menu must be OFF when gtty does not use RTS for PTT.
- **Key PTT through the rig** under *Preferences → Transmit* must be on.

## The rig does not connect

- Check the model number, the port and the serial speed; `gtty-cli rig --model N --device DEV --serial-baud B --debug` with the GUI closed shows Hamlib's trace.
- Only one program can open the CAT port. Close other rig software, or run `rigctld` and select model 2 (NET rigctl) in gtty.
- The AppImage ships Hamlib 4.6.2; a distribution's older Hamlib may not know a recent rig.

## Tooltips, the band map or the Skimmer list do not update

Settings are applied when the Preferences dialog closes, not while it is open. Close the dialog.

## GridTracker shows nothing

- Host and port must match GridTracker's WSJT-X receive port (UDP 2237 by default) and GridTracker must be set to accept the sender.
- GridTracker only plots stations it can place on a band: with no rig, set **Dial frequency without rig**.
- Your callsign and grid must be set under *Preferences → Station*.
- Decode reports are limited to one per callsign per minute; the station must appear in the Best guess pane as a callsign.

## A logger cannot connect over XML-RPC

- Enable the server under *Preferences → Integration* and make sure the port is free. If fldigi is running it holds 7362; give gtty another port and tell the logger.
- The bind address 127.0.0.1 accepts local connections only. Use 0.0.0.0 to accept connections from another machine.

## Callbook lookups do nothing

- Choose a service and enter the username and password under *Preferences → Callbook*. QRZ.com needs an XML subscription.
- The build must include libcurl; the Preferences page says so when it does not.
- Lookups run once a complete callsign has been typed, after 600 ms of no typing.

## The CW identifier does not go out with FSK keying

With FSK or CAT keying the ID is sent by keying PTT, which carries the mark tone, so **Key PTT through the rig** must be on. A toast says so.

## The decoders fall behind or the CPU is busy

- Turn off the **Skimmer** or raise its channel step.
- Disable engines you do not read in *Preferences → Decoders*.
- Narrow the waterfall range, which is also the Skimmer's search range.

## Getting help

Report problems at the [issue tracker](https://github.com/gjyoung1974/GTTY/issues). Include the gtty version from the About dialog, the distribution, the rig and keying method, and, for decoding problems, a recording made with `gtty-cli record`.
