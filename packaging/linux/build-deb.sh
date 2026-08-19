#!/usr/bin/env bash
# Build a self-contained .deb from an already-built roift_gui AppImage.
#
# Usage:
#   packaging/linux/build-deb.sh <appimage-file> [output-dir]
#
# Why from the AppImage and not `cmake --install`: the install tree carries only
# the binaries and desktop files — Qt6, VTK and ITK come from the conda env and
# are pulled in by linuxdeploy while building the AppImage. The extracted
# AppImage is therefore the only tree that contains *every* runtime lib, so we
# repackage that.
#
# The package is deliberately "fat": it installs the whole bundle under
# /opt/roift_gui (no apt Depends on Qt6/VTK — Debian's VTK is Qt5-only), plus a
# /usr/bin launcher, .desktop entry and icons for menu integration.
set -euo pipefail

APPIMAGE="$(readlink -f "${1:?usage: build-deb.sh <appimage-file> [output-dir]}")"
OUT_DIR="$(readlink -f "${2:-$PWD}")"

# Version: env override, else parse "roift_gui-<ver>-x86_64.AppImage".
VERSION="${VERSION:-$(basename "$APPIMAGE" | sed -n 's/^roift_gui-\(.*\)-x86_64\.AppImage$/\1/p')}"
VERSION="${VERSION#v}"                 # Debian version must start with a digit
VERSION="${VERSION:-0.0.0}"

MAINTAINER="${DEB_MAINTAINER:-Jungeui Choi <jungchoi0221@gmail.com>}"
HOMEPAGE="${DEB_HOMEPAGE:-https://github.com/jung0221/ROIFT_GUI}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── Extract the AppImage into an AppDir (squashfs-root) ─────────────────────
# --appimage-extract needs no FUSE, so this works in containers/CI.
chmod +x "$APPIMAGE"
( cd "$WORK" && APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-extract >/dev/null )
APPDIR="$WORK/squashfs-root"
[ -x "$APPDIR/usr/bin/roift_gui" ] || { echo "error: extracted tree has no usr/bin/roift_gui" >&2; exit 1; }

# ── Stage the package root ──────────────────────────────────────────────────
PKG="$WORK/pkgroot"
INSTALL_ROOT="/opt/roift_gui"

# Bundle: the AppImage's usr/ tree relocated under /opt. qt.conf (Prefix=../)
# and the binary's RPATH ($ORIGIN/../lib) are relative, so it runs from /opt.
install -d "$PKG$INSTALL_ROOT"
cp -a "$APPDIR/usr/." "$PKG$INSTALL_ROOT/"

# Launcher on PATH: exec the real binary. The bundle is self-resolving — the
# binary's RUNPATH is $ORIGIN/../lib, every bundled lib's is $ORIGIN, and Qt
# plugins resolve via the adjacent qt.conf. SegmentationRunner finds oiftrelax
# next to the real binary, so exec'ing it (not a copy) is what makes that work.
#
# Do not export LD_LIBRARY_PATH here. Child processes inherit it, so the
# `xdg-open` that revealPathInFileManager spawns would load our bundled glib
# against the host's helpers.
install -d "$PKG/usr/bin"
cat > "$PKG/usr/bin/roift_gui" <<EOF
#!/bin/sh
exec "$INSTALL_ROOT/bin/roift_gui" "\$@"
EOF
chmod 0755 "$PKG/usr/bin/roift_gui"

# Desktop integration. Rewrite Exec to the absolute launcher path: a bare
# "roift_gui" resolves via PATH, so a user's ~/.local/bin/roift_gui (e.g. a
# dev-build symlink) would shadow it and the menu icon would open the wrong app.
install -d "$PKG/usr/share/applications"
sed 's|^Exec=roift_gui|Exec=/usr/bin/roift_gui|' \
  "$APPDIR/usr/share/applications/roift_gui.desktop" \
  > "$PKG/usr/share/applications/roift_gui.desktop"
chmod 0644 "$PKG/usr/share/applications/roift_gui.desktop"

for asset in \
  "share/icons/hicolor/256x256/apps/roift_gui.png" \
  "share/icons/hicolor/scalable/apps/roift_gui.svg" \
  "share/metainfo/roift_gui.metainfo.xml"; do
  if [ -f "$APPDIR/usr/$asset" ]; then
    install -D -m0644 "$APPDIR/usr/$asset" "$PKG/usr/$asset"
  fi
done

# ── Control metadata ────────────────────────────────────────────────────────
INSTALLED_KB="$(du -sk "$PKG$INSTALL_ROOT" "$PKG/usr" | awk '{s+=$1} END {print s}')"
install -d "$PKG/DEBIAN"
cat > "$PKG/DEBIAN/control" <<EOF
Package: roift-gui
Version: $VERSION
Architecture: amd64
Maintainer: $MAINTAINER
Homepage: $HOMEPAGE
Section: science
Priority: optional
Installed-Size: $INSTALLED_KB
Depends: libc6, libgl1, libglib2.0-0, libfontconfig1
Description: Seed-based ROIFT segmentation for 3D medical images
 ROIFT GUI opens NIfTI, DICOM series and NumPy volumes, shows axial, sagittal
 and coronal slices beside a 3D mask render, and turns hand-placed object and
 background seeds into per-label segmentations using the bundled ROIFT tools.
 .
 Self-contained: Qt6, VTK and ITK are bundled under /opt/roift_gui, so it
 installs and runs without conda or a system Qt6/VTK.
EOF

# ── Build the package (root:root ownership without needing root) ────────────
mkdir -p "$OUT_DIR"
DEB="$OUT_DIR/roift_gui-${VERSION}-amd64.deb"
dpkg-deb --build --root-owner-group "$PKG" "$DEB"
echo "deb written to $DEB"
