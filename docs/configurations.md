---
title: Configurations
nav_order: 11
---

# Configurations

A configuration is a complete, named set of settings, the way WSJT-X does it: rig, audio, tones, decoders, keying, macros, station details, integrations, everything in Preferences. Keep one for everyday operating, one for a contest, one for a second rig or a portable station, and switch between them from the **Configurations** submenu.

## The Configurations menu

The submenu lists the configurations with the active one marked, followed by operations on the active one:

| Item | What it does |
|---|---|
| *name* | Switch to that configuration. Any transmission is aborted, the settings are loaded and applied, and a toast confirms the switch |
| **Clone…** | Copy the active configuration under a new name and switch to it |
| **Rename…** | Rename the active configuration |
| **Reset to defaults…** | Return every setting of the active configuration to the built-in defaults, after confirmation |
| **Delete…** | Remove the active configuration and switch to Default, after confirmation |

**Default** always exists and cannot be renamed or deleted. The window title and the Preferences dialog title show the name of the active configuration when it is not Default.

Settings are saved to the active configuration when the Preferences dialog closes, when a macro is edited, when a configuration is switched and when gtty exits. Control-row changes (baud, shift, AFC, squelch and so on) are part of the configuration too.

## Files

Each configuration is a key-file at `~/.config/gtty/profiles/<name>.conf`, and `~/.config/gtty/gtty.conf` records which one is active. Names may not contain slashes or start with a dot. Copying a `.conf` file to another machine carries the whole setup, including the callbook password, which is stored in plain text.

gtty versions before configurations existed kept everything in `gtty.conf`; such a file is moved into the Default configuration on first start.

## Suggested use

- Clone **Default** to **Contest** and in the clone turn on the Skimmer, set Auto CQ to F5, load MASTER.SCP, shorten the Auto RX delay and set a 500 Hz filter width on connect.
- Keep a **Portable** configuration with the other rig's model and port, AFSK keying and the manual dial frequency you use.
- Reset a configuration rather than hunting for a stray setting when something behaves oddly; the logbook is not touched by a reset.
