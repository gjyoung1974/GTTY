---
title: Tuning
nav_order: 5
---

# Tuning

RTTY is sent as two audio tones, mark and space, 170 Hz apart in the usual amateur convention. gtty's decoders listen at a nominal pair of tones, 2125 Hz mark and 2295 Hz space by default, and AFC lets them follow a signal that is a little off. Tuning is a matter of putting the signal in the tone window, either by moving the window on the waterfall or by retuning the rig.

## Tones, shift and polarity

The tone pair is set by the **Tones** preset in the control row, or directly under *Preferences → RTTY* with the **Mark tone** and **Space tone** fields. Shift and polarity follow from the two tones: with space above mark the signal is "normal"; with space below mark it is "reversed", shown by the **Rev** box in the control row.

Presets:

| Preset | Mark / space |
|---|---|
| 2125 / 2295 (standard) | the default, 170 Hz shift |
| 2125 / 1955 (reversed) | the same tones with space below mark |
| 1275 / 1445 (low, standard) | low tones |
| 1275 / 1105 (low, reversed) | low tones, reversed |
| 2125 / 2975 (850 Hz shift) | wide shift, used by some utility stations |

The **Shift** dropdown offers 170, 200, 425, 450 and 850 Hz and keeps the mark tone. **Baud** offers 45.45 (amateur standard), 50, 75 and 100.

Whether you need **Rev** depends on the rig's sideband. In RTTY-L or LSB with the default tones leave it unticked; in USB or an upper-sideband data mode tick it. If a strong, well tuned signal prints garbage, Rev is the first thing to try.

## The waterfall

Mouse actions on the waterfall:

| Action | Effect |
|---|---|
| **Left click** | Move the tone window so its center is at the clicked frequency |
| **Drag inside the tone window** | Slide the window along the band |
| **Drag elsewhere** | Drag the window center with the pointer |
| **Scroll wheel** | Nudge the tones by 10 Hz |
| **Right click** | Retune the rig so the clicked signal lands on the nominal tones (needs a connected rig) |
| **Middle click** | Place a notch at that frequency, or remove the notch already there |

Right-click tuning is the contester's way: the signal lands on the nominal tones, so the rig's narrow filter and the decoders agree, and **Net** has nothing to correct.

The direction of the waterfall follows the rig's sideband by default, so in RTTY-L the display reads like the rig's own scope with higher audio frequencies on the left. *Preferences → Station → Waterfall direction* can fix it to low-on-the-left or low-on-the-right. The displayed range (300–3000 Hz by default) is set on the same page; the Skimmer searches the same range.

## The XY scope

The scope plots the filtered mark channel against the filtered space channel. A tuned signal draws two ellipses at right angles: one lights while mark is sent, the other while space is sent. When the pattern is tilted or one ellipse is missing, the signal is off the tones or the shift is wrong. The mark and space levels also set the brightness of the two ellipses.

## AFC

**AFC** (automatic frequency control) measures the frequency of the tones actually received and moves the decoder's local oscillators toward them, a fraction of the error per character. The readout under the scope shows the current offset. The maximum excursion is the **AFC range** under *Preferences → RTTY* (±60 Hz by default).

**Reset AFC** in the menu returns the decoders to the nominal tones. The AFC also resets whenever the tones are changed, a band button is pressed or a rig retune is made from the waterfall.

With **Net** ticked, the transmitter uses the AFC-tracked tones rather than the nominal ones, so you answer a station exactly on its frequency even if it is a little off yours. Untick Net when you run a frequency and want to stay put.

## ATC

**ATC** (automatic threshold control) normalizes the mark and space envelopes by their recent peaks before the decision between them is made. It is what keeps the decoder working through selective fading, where one tone drops while the other holds. Leave it on; the time constant can be tuned per engine in *Preferences → Decoders* (Custom mode).

## Squelch

The **Squelch** slider mutes decoder output while the signal quality is below the threshold. Quality is on one scale for every engine: a clean signal reads about 1, noise reads near 0. A setting around 0.3–0.5 stops the panes filling with garbage between signals; 0 prints everything. **SQL** appears next to the AFC readout while the squelch is closed.

## Notches

Up to four notch filters can be placed on the waterfall with a middle click; they are drawn as marks on the display and removed with another middle click or with **Clear notches** in the menu. Use them on a carrier or a neighboring signal that sits inside the receiver passband. Placing a fifth notch drops the oldest.

## Rig receiver filter

With a rig connected the **Filter** dropdown under the scope selects the receiver filter width (250 Hz to 3000 Hz) through Hamlib, and **IF shift** moves the passband when the rig supports it. A width can also be applied automatically when the rig connects. See [Rig control](rig-control.md).
