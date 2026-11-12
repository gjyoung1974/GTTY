---
title: DX cluster band map
nav_order: 12
---

# DX cluster band map

gtty can connect to a DX cluster node and show the RTTY spots as a band map in the sidebar, colored from your logbook. Clicking a spot QSYs the rig and puts the callsign in the Call field.

## Connecting

Under *Preferences → Cluster*:

1. Tick **Connect to a DX cluster**.
2. Enter the **Host** (for example `dxc.nc7j.com`, `dxspider.co.uk` or `ve7cc.net`) and **Port** (DXSpider 7300, AR-Cluster 7373, CC Cluster 23).
3. **Login callsign** is sent at the login prompt; blank uses your station callsign, which must then be set under *Preferences → Station*.
4. **Commands after login**, separated by `;`, are sent once connected, for example `set/filter doc/pass rtty ; sh/dx 20` to ask the node to pass RTTY spots only and list the last twenty.

gtty reconnects with back-off when the connection drops. The status line at the top of the sidebar's Cluster page shows the connection state and the last line received from the node.

## The band map

Spots are listed by frequency with the callsign, the spot time, the comment and the spotter. Rows are colored from the logbook:

| Appearance | Meaning |
|---|---|
| Accent color | A callsign you have never worked |
| Normal | Worked before, but not on this band |
| Dimmed | A dupe: already in the log on this band in RTTY |

**Click a spot** to QSY the rig (or set the manual dial) and put the callsign in the Call field. This is refused while transmitting.

Options under *Preferences → Cluster → Band map* and in the sidebar:

- **RTTY spots only** (on by default): keep a spot if its comment mentions RTTY, or if it names no other mode and the frequency lies in a customary RTTY or digital segment. Spots mentioning FT8, PSK, CW, SSB and the like are dropped.
- **Current band only**, mirrored by the **This band** box in the sidebar header.
- **Drop spots older than** 30 minutes by default. The list holds at most 400 spots.

## Cluster commands

The entry at the bottom of the Cluster page sends a command to the node when you press Enter, for example `sh/dx 20`, `set/filter doc/pass rtty` or `sh/wwv`. The node's reply scrolls through the status line at the top of the page.

## Showing and hiding

The sidebar button in the header bar, **Band map** in the menu and the arrow button in the sidebar header show or hide the sidebar. Its state is kept in the configuration. Turning the Skimmer on opens the sidebar on the Skimmer page.
