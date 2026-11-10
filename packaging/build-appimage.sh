#!/usr/bin/env bash
# Build an AppImage of gtty.
# Expects a configured and built tree in $BUILD_DIR (default: build) on a host with
# the runtime libraries installed. Uses linuxdeploy for shared-library collection and
# bundles the GTK4 resources (GSettings schemas, gdk-pixbuf loaders, Adwaita icons)
# itself, wired up by an AppRun hook, because linuxdeploy's GTK plugin targets GTK 3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
APPDIR="${APPDIR:-$ROOT/build/AppDir}"
ARCH="${ARCH:-$(uname -m)}"
TOOLS="$ROOT/packaging/tools"
mkdir -p "$TOOLS"

fetch() {  # url dest
  [ -x "$2" ] && return 0
  curl -fsSL -o "$2" "$1"
  chmod +x "$2"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" "$TOOLS/linuxdeploy"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

libdir() {  # first existing multiarch or plain lib dir containing $1
  for d in /usr/lib/"$(gcc -dumpmachine 2>/dev/null || echo "${ARCH}-linux-gnu")" /usr/lib64 /usr/lib /usr/local/lib; do
    [ -e "$d/$1" ] && { echo "$d/$1"; return 0; }
  done
  return 1
}

# --- GSettings schemas (GTK4 reads org.gtk.gtk4.Settings.*)
mkdir -p "$APPDIR/usr/share/glib-2.0/schemas"
cp /usr/share/glib-2.0/schemas/org.gtk.gtk4.Settings.*.xml "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
cp /usr/share/glib-2.0/schemas/org.gnome.desktop.interface.gschema.xml "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
cp /usr/share/glib-2.0/schemas/org.gnome.desktop.enums.xml "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "$APPDIR/usr/share/glib-2.0/schemas"

# --- gdk-pixbuf loaders (SVG icon rendering) + the query tool for a runtime cache
PIXBUF_MODDIR="$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0)"
mkdir -p "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp "$PIXBUF_MODDIR"/*.so "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/" 2>/dev/null || true
QUERY="$(pkg-config --variable=gdk_pixbuf_query_loaders gdk-pixbuf-2.0 2>/dev/null || true)"
[ -x "${QUERY:-/nonexistent}" ] || QUERY="$(command -v gdk-pixbuf-query-loaders || ls /usr/lib/*/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders 2>/dev/null | head -1)"
cp "$QUERY" "$APPDIR/usr/bin/gdk-pixbuf-query-loaders"
# Also bundle the librsvg GIO module location (empty dir keeps GIO from loading host modules).
mkdir -p "$APPDIR/usr/lib/gio/modules"

# --- Icon theme: libadwaita/GTK symbolic icons come from Adwaita.
mkdir -p "$APPDIR/usr/share/icons"
if [ -d /usr/share/icons/Adwaita ]; then
  cp -a /usr/share/icons/Adwaita "$APPDIR/usr/share/icons/"
  # Drop the large non-symbolic bitmap sets we never use.
  find "$APPDIR/usr/share/icons/Adwaita" -maxdepth 1 -type d -regex '.*/[0-9]+x[0-9]+.*' -exec rm -rf {} + 2>/dev/null || true
fi
[ -f /usr/share/icons/hicolor/index.theme ] && cp /usr/share/icons/hicolor/index.theme "$APPDIR/usr/share/icons/hicolor/"

# --- Hamlib rig backends are dlopen'ed plugins in some builds; copy them when present.
for d in /usr/lib/*/hamlib /usr/local/lib/hamlib; do
  [ -d "$d" ] && mkdir -p "$APPDIR/usr/lib/hamlib" && cp -a "$d"/. "$APPDIR/usr/lib/hamlib/"
done

# --- AppRun hook: point GLib/GTK at the bundled resources.
mkdir -p "$APPDIR/apprun-hooks"
cat > "$APPDIR/apprun-hooks/gtk4-env.sh" <<'HOOK'
# gtty AppImage: environment for bundled GTK4 resources
export XDG_DATA_DIRS="$APPDIR/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GSETTINGS_SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas"
export GIO_MODULE_DIR="$APPDIR/usr/lib/gio/modules"
export GDK_PIXBUF_MODULEDIR="$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
_cache_dir="${XDG_RUNTIME_DIR:-/tmp}/gtty-pixbuf-$(id -u)"
mkdir -p "$_cache_dir"
export GDK_PIXBUF_MODULE_FILE="$_cache_dir/loaders.cache"
"$APPDIR/usr/bin/gdk-pixbuf-query-loaders" > "$GDK_PIXBUF_MODULE_FILE" 2>/dev/null || true
unset _cache_dir
HOOK

export OUTPUT="gtty-${ARCH}.AppImage"
# APPIMAGE_EXTRACT_AND_RUN lets linuxdeploy run inside containers without FUSE.
APPIMAGE_EXTRACT_AND_RUN=1 "$TOOLS/linuxdeploy" --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/io.github.gtty.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/io.github.gtty.svg" \
  --executable "$APPDIR/usr/bin/gtty" --executable "$APPDIR/usr/bin/gtty-cli" \
  --executable "$APPDIR/usr/bin/gdk-pixbuf-query-loaders" \
  --deploy-deps-only "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders" \
  --output appimage
echo "built $OUTPUT"
