#!/usr/bin/env bash
# Build gtty.app and a .dmg for macOS.
# Expects a configured and built tree in $BUILD_DIR (default: build) on a Mac with the
# Homebrew dependencies installed, plus: brew install dylibbundler librsvg adwaita-icon-theme
#
# Layout:
#   gtty.app/Contents/MacOS/gtty        launcher script (points GLib/GTK at the bundle)
#   gtty.app/Contents/MacOS/gtty-bin    the GUI
#   gtty.app/Contents/MacOS/gtty-cli    the command-line tool
#   gtty.app/Contents/Frameworks/       every non-system dylib, install names rewritten
#   gtty.app/Contents/Resources/share/  GSettings schemas, Adwaita icons, man pages
#   gtty.app/Contents/Resources/lib/    gdk-pixbuf loaders (SVG icons)
#
# The bundle is ad-hoc signed, not notarized: see docs/installation.md.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
ARCH="${ARCH:-$(uname -m)}"
BREW="$(brew --prefix)"
VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD_DIR/CMakeCache.txt")"
[ -n "$VERSION" ] || { echo "cannot read the version from $BUILD_DIR/CMakeCache.txt" >&2; exit 1; }

APP="$BUILD_DIR/gtty.app"
C="$APP/Contents"
STAGE="$BUILD_DIR/macos-stage"
OUTPUT="${OUTPUT:-$ROOT/gtty-${VERSION}-macos-${ARCH}.dmg}"

rm -rf "$APP" "$STAGE"
mkdir -p "$C/MacOS" "$C/Frameworks" "$C/Resources/share" "$C/Resources/lib"
cmake --install "$BUILD_DIR" --prefix "$STAGE/prefix"

cp "$STAGE/prefix/bin/gtty" "$C/MacOS/gtty-bin"
cp "$STAGE/prefix/bin/gtty-cli" "$C/MacOS/gtty-cli"
cp -R "$STAGE/prefix/share/." "$C/Resources/share/"
rm -rf "$C/Resources/share/applications" "$C/Resources/share/metainfo"   # Linux desktop files

# --- GSettings schemas (GTK4 reads org.gtk.gtk4.Settings.*)
SCHEMAS="$C/Resources/share/glib-2.0/schemas"
mkdir -p "$SCHEMAS"
cp -L "$BREW"/share/glib-2.0/schemas/org.gtk.gtk4.Settings.*.xml "$SCHEMAS/" 2>/dev/null || true
cp -L "$BREW"/share/glib-2.0/schemas/org.gnome.desktop.interface.gschema.xml "$SCHEMAS/" 2>/dev/null || true
cp -L "$BREW"/share/glib-2.0/schemas/org.gnome.desktop.enums.xml "$SCHEMAS/" 2>/dev/null || true
glib-compile-schemas "$SCHEMAS"

# --- Icon theme: libadwaita/GTK symbolic icons come from Adwaita.
mkdir -p "$C/Resources/share/icons/hicolor"
if [ -d "$BREW/share/icons/Adwaita" ]; then
  cp -RL "$BREW/share/icons/Adwaita" "$C/Resources/share/icons/"
  # Drop the large non-symbolic bitmap sets we never use.
  find "$C/Resources/share/icons/Adwaita" -maxdepth 1 -type d -name '[0-9]*x[0-9]*' -exec rm -rf {} + 2>/dev/null || true
else
  echo "warning: $BREW/share/icons/Adwaita not found; symbolic icons will be missing" >&2
fi
[ -f "$BREW/share/icons/hicolor/index.theme" ] && cp -L "$BREW/share/icons/hicolor/index.theme" "$C/Resources/share/icons/hicolor/"

# --- gdk-pixbuf loaders (SVG icon rendering) + the query tool for a runtime cache.
# Newer gdk-pixbuf builds may ship no loadable modules at all; then this is a no-op.
LOADERS="$C/Resources/lib/gdk-pixbuf-2.0/2.10.0/loaders"
mkdir -p "$LOADERS"
for d in "$BREW/lib/gdk-pixbuf-2.0/2.10.0/loaders" "$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0 2>/dev/null || true)"; do
  [ -d "$d" ] || continue
  for f in "$d"/*.so "$d"/*.dylib; do
    [ -e "$f" ] && cp -L "$f" "$LOADERS/"
  done
done
chmod -R u+w "$C"
QUERY="$(command -v gdk-pixbuf-query-loaders || true)"
[ -n "$QUERY" ] && cp -L "$QUERY" "$C/MacOS/gdk-pixbuf-query-loaders" && chmod u+w "$C/MacOS/gdk-pixbuf-query-loaders"

# --- Collect the dylibs and rewrite install names to @executable_path/../Frameworks.
FIX=(-x "$C/MacOS/gtty-bin" -x "$C/MacOS/gtty-cli")
[ -e "$C/MacOS/gdk-pixbuf-query-loaders" ] && FIX+=(-x "$C/MacOS/gdk-pixbuf-query-loaders")
for f in "$LOADERS"/*; do
  [ -e "$f" ] && FIX+=(-x "$f")
done
# stdin from /dev/null: dylibbundler prompts when it cannot find a library; fail instead.
dylibbundler -of -b "${FIX[@]}" -d "$C/Frameworks" -p @executable_path/../Frameworks/ \
  -s "$BREW/lib" </dev/null

# --- Launcher: point GLib/GTK at the bundled resources, then run the GUI.
cat > "$C/MacOS/gtty" <<'LAUNCHER'
#!/bin/sh
# gtty.app launcher: environment for the bundled GTK4 resources
here="$(cd "$(dirname "$0")" && pwd)"
res="$(cd "$here/../Resources" && pwd)"
export XDG_DATA_DIRS="$res/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GSETTINGS_SCHEMA_DIR="$res/share/glib-2.0/schemas"
export GDK_PIXBUF_MODULEDIR="$res/lib/gdk-pixbuf-2.0/2.10.0/loaders"
if [ -x "$here/gdk-pixbuf-query-loaders" ]; then
  cache_dir="$HOME/Library/Caches/io.github.gtty"
  mkdir -p "$cache_dir"
  export GDK_PIXBUF_MODULE_FILE="$cache_dir/loaders.cache"
  "$here/gdk-pixbuf-query-loaders" > "$GDK_PIXBUF_MODULE_FILE" 2>/dev/null || true
fi
exec "$here/gtty-bin" "$@"
LAUNCHER
chmod +x "$C/MacOS/gtty"

# --- Icon: io.github.gtty.svg -> gtty.icns
ICONSET="$STAGE/gtty.iconset"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  rsvg-convert -w "$s" -h "$s" "$ROOT/data/io.github.gtty.svg" -o "$ICONSET/icon_${s}x${s}.png"
  rsvg-convert -w $((s * 2)) -h $((s * 2)) "$ROOT/data/io.github.gtty.svg" -o "$ICONSET/icon_${s}x${s}@2x.png"
done
iconutil -c icns -o "$C/Resources/gtty.icns" "$ICONSET"

# --- Info.plist. The Homebrew libraries target the build host's macOS, so that is the minimum.
MIN_MACOS="$(sw_vers -productVersion | cut -d. -f1).0"
cat > "$C/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>gtty</string>
  <key>CFBundleDisplayName</key><string>gtty</string>
  <key>CFBundleIdentifier</key><string>io.github.gtty</string>
  <key>CFBundleExecutable</key><string>gtty</string>
  <key>CFBundleIconFile</key><string>gtty</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>${MIN_MACOS}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSMicrophoneUsageDescription</key><string>gtty decodes RTTY from the audio input connected to your radio.</string>
</dict>
</plist>
PLIST

# --- Nothing in the bundle may still point into Homebrew.
bad=0
while IFS= read -r f; do
  file -b "$f" | grep -q 'Mach-O' || continue
  if otool -L "$f" | tail -n +2 | grep -q "$BREW\|/usr/local/\|/opt/homebrew/"; then
    echo "error: $f still links outside the bundle:" >&2
    otool -L "$f" | tail -n +2 | grep "$BREW\|/usr/local/\|/opt/homebrew/" >&2
    bad=1
  fi
done < <(find "$APP" -type f)
[ "$bad" = 0 ] || exit 1

# --- Ad-hoc sign (install_name_tool invalidated the original signatures; arm64 refuses unsigned code).
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

# --- Disk image with an /Applications shortcut.
DMGROOT="$STAGE/dmg"
mkdir -p "$DMGROOT"
cp -R "$APP" "$DMGROOT/"
ln -s /Applications "$DMGROOT/Applications"
rm -f "$OUTPUT"
for attempt in 1 2 3; do   # hdiutil fails with "Resource busy" now and then on CI
  hdiutil create -volname "gtty $VERSION" -srcfolder "$DMGROOT" -fs HFS+ -format UDZO -ov "$OUTPUT" && break
  [ "$attempt" = 3 ] && exit 1
  sleep 5
done
echo "built $OUTPUT"
