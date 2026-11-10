---
title: Installation
nav_order: 2
---

# Installation

gtty runs on Linux with a GTK4 desktop and on macOS (Apple silicon and Intel). On Linux, packages are built for Debian 13 (trixie), Ubuntu 24.04 LTS, 25.04 and 25.10 on amd64 and arm64, and an AppImage covers other distributions. On macOS a `.dmg` is built for Apple silicon; on Intel Macs [build from source](#building-from-source-on-macos) with Homebrew.

## Debian and Ubuntu packages

1. Download the `.deb` for your release from the [releases page](https://github.com/gjyoung1974/GTTY/releases). Files are named `gtty_<version>_<distro>_<arch>.deb`.
2. Install it with apt so the dependencies are resolved:

   ```
   sudo apt install ./gtty_*.deb
   ```

3. Start **gtty** from the application menu or a terminal.

{: .note }
On a PipeWire desktop the `pipewire-alsa` package must be installed, otherwise the `default` and `pipewire` audio devices cannot be opened. apt installs it as a recommendation; `dpkg -i` does not. gtty shows a banner across the top of the window when it could not open its audio input.

## AppImage

1. Download `gtty-x86_64.AppImage` or `gtty-aarch64.AppImage` from the releases page.
2. Make it executable and run it:

   ```
   chmod +x gtty-*.AppImage
   ./gtty-*.AppImage
   ```

The AppImage bundles GTK4, libadwaita and Hamlib 4.6.2, so it works on distributions with older libraries and knows about recently added rigs.

## macOS disk image

1. Download `gtty-<version>-macos-arm64.dmg` from the releases page (Apple silicon only).
2. Open it and drag **gtty** to **Applications**.
3. The app is ad-hoc signed but not notarized, so macOS blocks the first launch. Control-click the app and choose **Open**, or allow it under System Settings → Privacy & Security → **Open Anyway**, or clear the quarantine flag:

   ```
   xattr -dr com.apple.quarantine /Applications/gtty.app
   ```

The app bundles GTK4, libadwaita, Hamlib and the other libraries, so Homebrew is not needed. It requires the macOS release it was built on (currently macOS 15) or later. `gtty-cli` is inside the bundle at `/Applications/gtty.app/Contents/MacOS/gtty-cli`.

## Serial port permissions

FSK keying and rig control use serial ports. On Linux your user must be in the `dialout` group:

```
sudo usermod -aG dialout $USER
```

Log out and back in for the change to take effect.

On macOS no group membership is needed; `/dev/cu.*` is accessible to everyone. See [macOS notes](#macos-notes) for how the ports are named.

## Building from source

### On Debian 13 or Ubuntu 24.04 and later

```
sudo apt install build-essential cmake ninja-build pkg-config \
     libgtk-4-dev libadwaita-1-dev libhamlib-dev portaudio19-dev \
     libfftw3-dev libsamplerate0-dev libsndfile1-dev libsqlite3-dev \
     libcurl4-openssl-dev libcodec2-dev
git clone https://github.com/gjyoung1974/GTTY.git gtty
cd gtty
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/src/gui/gtty
```

### Building from source on macOS

With [Homebrew](https://brew.sh):

```
brew install cmake ninja pkg-config gtk4 libadwaita hamlib portaudio \
     fftw libsamplerate libsndfile codec2
git clone https://github.com/gjyoung1974/GTTY.git gtty
cd gtty
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/src/gui/gtty
```

`sqlite3` and `libcurl` come from the macOS SDK, so they need no Homebrew package. Audio uses PortAudio's CoreAudio backend and the GUI uses GTK4's native macOS backend; X11 and XQuartz are not involved.

Run gtty from the build tree or from the prefix you installed it into. To build `gtty.app` and a `.dmg` from the build tree:

```
brew install dylibbundler librsvg adwaita-icon-theme
packaging/build-macos-app.sh
```

### Optional dependencies

Two dependencies are optional:

| Debian / Ubuntu | macOS | What it adds |
|---|---|---|
| `libcurl4-openssl-dev` | in the SDK, nothing to install | Callbook lookups (QRZ.com XML, HamQTH) |
| `libcodec2-dev` | `brew install codec2` | The libcodec2 FSK comparison decoder |

Without them gtty builds and runs; the matching features show a note in Preferences.

Always configure from the top-level directory. The `src/*` directories are not standalone CMake projects.

To install system-wide after building:

```
sudo cmake --install build
```

To build a `.deb` locally run `cpack -G DEB` inside the build directory (Linux only). To build an AppImage run `packaging/build-appimage.sh`.

## What gets installed

| Item | Path |
|---|---|
| GUI | `gtty` |
| Command-line tool | `gtty-cli` |
| Desktop entry and icon | `io.github.gtty.desktop`, `io.github.gtty.svg` (used by Linux desktops only) |
| Man pages | `gtty(1)`, `gtty-cli(1)` |

## macOS notes

- **Serial ports** are named `/dev/cu.usbserial-XXXXXXXX` (FTDI), `/dev/cu.SLAB_USBtoUART` (Silicon Labs) or `/dev/cu.usbmodemXXXX` (CDC), not `/dev/ttyUSB0`. Run `ls /dev/cu.*` to list them, and always use the `cu.` name rather than the `tty.` one, which blocks waiting for carrier detect. Wherever these pages say `/dev/ttyUSB0`, read your adapter's `cu.` name.
- **Microphone permission**: macOS gates audio input. The first time gtty opens the sound card, macOS asks for permission: for gtty itself when it is installed from the `.dmg`, or for the application that launched it (your terminal) when it is run from a build tree. If the prompt was dismissed, re-enable it under System Settings → Privacy & Security → Microphone. Without it the device opens but delivers silence and the waterfall stays empty.
- **True FSK** over a UART needs a driver that accepts the 45 baud RTTY rate. FTDI adapters do; some CP210x and CDC-ACM drivers do not, and gtty reports the error when the port is opened. FSK via DTR/RTS and AFSK work either way. See [Transmitting](transmitting.md).

The configuration and data paths below are the same as on Linux.

## Where gtty keeps its files

| Purpose | Default location |
|---|---|
| Active configuration name | `~/.config/gtty/gtty.conf` |
| Configurations | `~/.config/gtty/profiles/<name>.conf` |
| Logbook | `~/.local/share/gtty/logbook.sqlite` |
| Super Check Partial database | `~/.local/share/gtty/MASTER.SCP` |
| Daily session files | `~/.local/share/gtty/sessions/gtty-YYYY-MM-DD.txt` |

See [Configurations](configurations.md) and [Logging](logging.md) for details.
