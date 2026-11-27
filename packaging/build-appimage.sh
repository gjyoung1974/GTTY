#!/usr/bin/env bash
# Build an AppImage of gtty.
# Expects a configured and built tree in $BUILD_DIR (default: build) on a host with
# the runtime libraries installed. Uses linuxdeploy for shared-library collection and
# bundles the GTK4 resources (GSettings schemas, gdk-pixbuf loaders, Adwaita icons)
# itself, wired up by an AppRun hook, because linuxdeploy's GTK plugin targets GTK 3.
#
# linuxdeploy and the AppImage runtime are not released for every architecture (riscv64
# has neither). There the script collects the libraries itself and puts the AppDir behind
# the uruntime static runtime; this needs patchelf and mksquashfs (squashfs-tools).
# BUNDLER=builtin forces that path on any architecture.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
APPDIR="${APPDIR:-$ROOT/build/AppDir}"
ARCH="${ARCH:-$(uname -m)}"
TOOLS="$ROOT/packaging/tools"
mkdir -p "$TOOLS"

if [ -z "${BUNDLER:-}" ]; then
  case "$ARCH" in
    x86_64|aarch64|armhf|i386) BUNDLER=linuxdeploy ;;
    *) BUNDLER=builtin ;;
  esac
fi

fetch() {  # url dest
  [ -x "$2" ] && return 0
  curl -fsSL -o "$2" "$1"
  chmod +x "$2"
}
if [ "$BUNDLER" = linuxdeploy ]; then
  fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" "$TOOLS/linuxdeploy"
fi

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

# .DirIcon is the icon file managers and the AppImage catalog show: it must be a PNG
# (an SVG is rejected). Make it a real file at the AppDir root, not a symlink to the SVG.
PNG_ICON="usr/share/icons/hicolor/256x256/apps/io.github.gtty.png"
set_diricon() {
  rm -f "$APPDIR/.DirIcon" "$APPDIR/io.github.gtty.svg"
  cp "$APPDIR/$PNG_ICON" "$APPDIR/io.github.gtty.png"
  cp "$APPDIR/$PNG_ICON" "$APPDIR/.DirIcon"
}
check_diricon() {  # the image must carry a PNG .DirIcon
  local dir; dir="$(mktemp -d)"
  (cd "$dir" && APPIMAGE_EXTRACT_AND_RUN=1 "$OLDPWD/$OUTPUT" --appimage-extract .DirIcon >/dev/null)
  if ! file -L "$dir/squashfs-root/.DirIcon" | grep -q "PNG image"; then
    echo "$OUTPUT has no PNG .DirIcon" >&2; rm -rf "$dir"; exit 1
  fi
  rm -rf "$dir"
}

if [ "$BUNDLER" = linuxdeploy ]; then
  # APPIMAGE_EXTRACT_AND_RUN lets linuxdeploy run inside containers without FUSE.
  APPIMAGE_EXTRACT_AND_RUN=1 "$TOOLS/linuxdeploy" --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/io.github.gtty.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/io.github.gtty.png" \
    --executable "$APPDIR/usr/bin/gtty" --executable "$APPDIR/usr/bin/gtty-cli" \
    --executable "$APPDIR/usr/bin/gdk-pixbuf-query-loaders" \
    --deploy-deps-only "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
  set_diricon
  APPIMAGE_EXTRACT_AND_RUN=1 "$TOOLS/linuxdeploy" --appdir "$APPDIR" --output appimage
  check_diricon
  echo "built $OUTPUT"
  exit 0
fi

# ------------------------------------------------------------------ builtin bundler
# What linuxdeploy does for the other architectures, done here: copy the shared libraries
# the binaries need into usr/lib, point their run paths at it, add AppRun, and append the
# AppDir as a SquashFS image to a static runtime.
URUNTIME_VERSION="v0.8.5"
case "$ARCH" in   # sha256 of uruntime-appimage-squashfs-lite-$ARCH in that release
  riscv64) URUNTIME_SHA256="b00242eb7bc4d25ed602baefa87594507c92e5e7f5d9d3897d8985fa0a09a5da" ;;
  aarch64) URUNTIME_SHA256="48dbb5d7ff4bdfdbfbfaf675e6ab21fa6646df693915c066a7443576967045e1" ;;
  x86_64)  URUNTIME_SHA256="95fcae18298788ee08e8fc29c4e33ac85dcd2b01fb631dda33d60b8ab8e5b4a5" ;;
  *) echo "no AppImage runtime known for $ARCH" >&2; exit 1 ;;
esac
for tool in patchelf mksquashfs ldd sha256sum; do
  command -v "$tool" >/dev/null || { echo "$tool is needed to build the AppImage on $ARCH" >&2; exit 1; }
done

# Libraries every desktop system provides and that must match the host (C library, GL
# and display drivers, sound servers, fonts): the AppImage excludelist linuxdeploy uses,
# https://github.com/AppImageCommunity/pkg2appimage/blob/master/excludelist
EXCLUDED=" libanl.so.1 libBrokenLocale.so.1 libcidn.so.1 libc.so.6 libdl.so.2 libm.so.6
  libmvec.so.1 libnss_compat.so.2 libnss_dns.so.2 libnss_files.so.2 libnss_hesiod.so.2
  libnss_nisplus.so.2 libnss_nis.so.2 libpthread.so.0 libresolv.so.2 librt.so.1
  libthread_db.so.1 libutil.so.1 libstdc++.so.6 libgcc_s.so.1 libGL.so.1 libEGL.so.1
  libGLdispatch.so.0 libGLX.so.0 libOpenGL.so.0 libdrm.so.2 libglapi.so.0 libgbm.so.1
  libxcb.so.1 libxcb-dri2.so.0 libxcb-dri3.so.0 libX11.so.6 libX11-xcb.so.1
  libwayland-client.so.0 libasound.so.2 libjack.so.0 libpipewire-0.3.so.0
  libfontconfig.so.1 libfreetype.so.6 libharfbuzz.so.0 libfribidi.so.0 libcom_err.so.2
  libexpat.so.1 libgpg-error.so.0 libICE.so.6 libSM.so.6 libusb-1.0.so.0 libuuid.so.1
  libz.so.1 libgmp.so.10 "

is_elf() { [ -f "$1" ] && [ ! -L "$1" ] && [ "$(head -c 4 "$1" | tr -d '\0')" = $'\177ELF' ]; }

deploy_deps() {  # copy the shared libraries the ELF file $1 needs into usr/lib
  local listing name arrow path _rest
  listing="$(ldd "$1")" || { echo "ldd failed on $1" >&2; exit 1; }
  if grep -q "not found" <<<"$listing"; then
    echo "unresolved libraries in $1:" >&2; grep "not found" <<<"$listing" >&2; exit 1
  fi
  while read -r name arrow path _rest; do
    [ "$arrow" = "=>" ] || continue
    case "$path" in /*) ;; *) continue ;; esac
    case "$path" in "$APPDIR"/*) continue ;; esac
    case "$name" in ld-linux*) continue ;; esac
    case "$EXCLUDED" in *" $name "*|*" $name"$'\n'*) continue ;; esac
    [ -e "$APPDIR/usr/lib/$name" ] || cp -L "$path" "$APPDIR/usr/lib/$name"
  done <<<"$listing"
}

LOADERS="$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
for f in "$APPDIR"/usr/bin/* "$LOADERS"/*.so "$APPDIR"/usr/lib/hamlib/*.so*; do
  is_elf "$f" && deploy_deps "$f"
done
ls "$APPDIR"/usr/lib/*.so* >/dev/null   # nothing collected means ldd gave no usable output

set_rpath() {  # rpath file...
  local rpath="$1" f; shift
  for f in "$@"; do
    is_elf "$f" || continue
    chmod u+w "$f"
    strip --strip-unneeded "$f" 2>/dev/null || true
    patchelf --set-rpath "$rpath" "$f"
  done
}
set_rpath '$ORIGIN/../lib' "$APPDIR"/usr/bin/*
set_rpath '$ORIGIN' "$APPDIR"/usr/lib/*.so*
set_rpath '$ORIGIN/../../..' "$LOADERS"/*.so
set_rpath '$ORIGIN/..' "$APPDIR"/usr/lib/hamlib/*.so*

# --- AppDir entry points: AppRun, and the desktop file and icon at the top level.
ln -sf usr/share/applications/io.github.gtty.desktop "$APPDIR/io.github.gtty.desktop"
set_diricon
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
# gtty AppImage entry point: set up the bundled GTK4 resources, then start gtty.
APPDIR="${APPDIR:-$(dirname "$(readlink -f "$0")")}"
export APPDIR
for hook in "$APPDIR"/apprun-hooks/*.sh; do
  . "$hook"
done
exec "$APPDIR/usr/bin/gtty" "$@"
APPRUN
chmod 755 "$APPDIR/AppRun"

# --- Runtime + SquashFS image = AppImage.
RUNTIME="$TOOLS/uruntime-${URUNTIME_VERSION}-${ARCH}"
fetch "https://github.com/VHSgunzo/uruntime/releases/download/${URUNTIME_VERSION}/uruntime-appimage-squashfs-lite-${ARCH}" "$RUNTIME"
echo "$URUNTIME_SHA256  $RUNTIME" | sha256sum -c - || { rm -f "$RUNTIME"; exit 1; }
IMAGE="$BUILD_DIR/gtty-${ARCH}.squashfs"
mksquashfs "$APPDIR" "$IMAGE" -noappend -all-root -no-progress -comp zstd -Xcompression-level 19 -b 1M
cat "$RUNTIME" "$IMAGE" > "$OUTPUT"
chmod 755 "$OUTPUT"
rm -f "$IMAGE"
echo "built $OUTPUT"
