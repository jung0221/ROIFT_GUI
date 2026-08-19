#!/usr/bin/env bash
# Build a self-contained AppImage from a roift_gui build tree.
#
# Usage:
#   packaging/linux/build-appimage.sh <build-dir> [output-dir]
#
# Requirements:
#   - the build tree was configured with the Qt6/VTK/ITK environment still
#     available (linuxdeploy resolves shared libs through it)
#   - qmake6/qmake reachable in PATH or via $QMAKE (conda: $CONDA_PREFIX/bin)
#   - network access on first run (downloads linuxdeploy into a cache dir)
set -euo pipefail

BUILD_DIR="$(readlink -f "${1:?usage: build-appimage.sh <build-dir> [output-dir]}")"
OUT_DIR="$(readlink -f "${2:-$PWD}")"
TOOLS_DIR="${LINUXDEPLOY_CACHE_DIR:-$HOME/.cache/roift-linuxdeploy}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Read the version from project() rather than the build tree's
# CMAKE_PROJECT_VERSION: when ROIFT_GUI is built as a subdirectory of a larger
# project, that cache entry holds the *parent* project's version (usually none).
VERSION="$(grep -oE 'project\(roift_gui VERSION [0-9]+\.[0-9]+\.[0-9]+' "$REPO_ROOT/CMakeLists.txt" \
           | grep -oE '[0-9]+\.[0-9]+\.[0-9]+$')"
VERSION="${VERSION:-0.0.0}"

# ── Stage the install tree into a fresh AppDir ──────────────────────────────
STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT
APPDIR="$STAGE_DIR/AppDir"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

# ── Fetch linuxdeploy + Qt plugin (cached across runs) ──────────────────────
mkdir -p "$TOOLS_DIR"
fetch() {
  local url="$1" dest="$2"
  if [ ! -x "$dest" ]; then
    echo "Downloading $(basename "$dest") ..."
    curl -fsSL "$url" -o "$dest" || wget -qO "$dest" "$url"
    chmod +x "$dest"
  fi
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
      "$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
      "$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

# ── Library search path for linuxdeploy ────────────────────────────────────
# linuxdeploy must resolve the same libs the binary was linked against. Only
# pull in the conda prefix when the staged binary has unresolved deps (CI:
# Qt/VTK/ITK live in the conda env, which is not on the loader path). Adding it
# unconditionally would shadow the system Qt for qmake/ldd on dev machines.
APP_BIN="$APPDIR/usr/bin/roift_gui"
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if ldd "$APP_BIN" | grep -q "not found" && [ -n "${CONDA_PREFIX:-}" ]; then
  export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$CONDA_PREFIX/lib"
  echo "Added $CONDA_PREFIX/lib to LD_LIBRARY_PATH (binary had unresolved deps)"
fi

# ── Locate qmake for the Qt deploy plugin ───────────────────────────────────
# Several Qt installs may coexist (system, conda base, conda env). Pick the
# qmake belonging to the Qt the binary actually links, by comparing its lib dir
# against the libQt6Core.so.6 that ldd resolves for the installed binary.
QT_CORE_DIR="$(ldd "$APP_BIN" | awk '/libQt6Core\.so/ {print $3}' | xargs -r dirname | xargs -r readlink -f || true)"

if [ -z "${QMAKE:-}" ]; then
  first_cand=""
  for cand in "$(command -v qmake6 || true)" \
              "$(command -v qmake || true)" \
              "${CONDA_PREFIX:-/nonexistent}/bin/qmake6" \
              "${CONDA_PREFIX:-/nonexistent}/bin/qmake" \
              "/usr/bin/qmake6" \
              "/usr/lib/qt6/bin/qmake6"; do
    [ -n "$cand" ] && [ -x "$cand" ] || continue
    [ -n "$first_cand" ] || first_cand="$cand"
    if [ -n "$QT_CORE_DIR" ]; then
      cand_libs="$(readlink -f "$("$cand" -query QT_INSTALL_LIBS)" 2>/dev/null || true)"
      if [ "$cand_libs" = "$QT_CORE_DIR" ]; then
        QMAKE="$cand"
        break
      fi
    fi
  done
  if [ -z "${QMAKE:-}" ] && [ -n "$first_cand" ]; then
    echo "warning: no qmake matches the linked Qt ($QT_CORE_DIR); using $first_cand" >&2
    QMAKE="$first_cand"
  fi
fi
[ -n "${QMAKE:-}" ] || { echo "error: qmake6/qmake not found; set QMAKE" >&2; exit 1; }
echo "Using QMAKE=$QMAKE (Qt libs: ${QT_CORE_DIR:-unknown})"
export QMAKE

# The ROIFT CLI tools ship in the same bin/ as the GUI (SegmentationRunner finds
# them next to the executable), so linuxdeploy has to bundle their libs too.
EXECUTABLE_ARGS=()
for cli in "$APPDIR"/usr/bin/oiftrelax* "$APPDIR"/usr/bin/exp_*; do
  [ -x "$cli" ] && EXECUTABLE_ARGS+=(--executable "$cli")
done

# linuxdeploy-plugin-qt bundles only the platform plugin the build machine used
# (xcb). Without these, anything headless — `--version` over SSH, a container —
# dies with "Available platform plugins are: xcb".
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqminimal.so"

# Run nested AppImage tools without FUSE (containers, CI).
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64

mkdir -p "$OUT_DIR"
cd "$OUT_DIR"
export OUTPUT="roift_gui-${VERSION}-x86_64.AppImage"

PATH="$TOOLS_DIR:$PATH" "$TOOLS_DIR/linuxdeploy-x86_64.AppImage" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/roift_gui.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/roift_gui.png" \
  "${EXECUTABLE_ARGS[@]}" \
  --plugin qt \
  --output appimage

echo "AppImage written to $OUT_DIR/$OUTPUT"
