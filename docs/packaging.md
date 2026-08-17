# Packaging and releases

Four assets ship with every release: a Windows NSIS installer and a portable
ZIP, a Linux AppImage and a `.deb`. All four are self-contained — Qt6, VTK, ITK
and the ROIFT command-line tools are inside — so a user needs neither conda nor
vcpkg nor a system Qt.

```
CMakeLists.txt          project(roift_gui VERSION x.y.z) — the only version
cmake/Packaging.cmake   install() rules + CPack (NSIS, ZIP)
packaging/make_icons.py generates SVG + PNG + ICO from one design
packaging/linux/        AppImage and .deb builders, .desktop, AppStream metainfo
packaging/windows/      .ico and the exe version resource template
packaging/ci/env.yml    the conda-forge dependency set CI builds against
resources/              roift_gui.qrc — the window/taskbar icon
.github/workflows/      pr.yml (build + test), release.yml (tag + publish)
```

## Version

`project(roift_gui VERSION ...)` in `CMakeLists.txt` is the only place a version
is written. Everything derives from it:

- `ROIFT_GUI_VERSION`, a directory-scope compile definition read by
  `src/Version.h` and nowhere else in `src/`
- `roift_gui --version` and the About box
- the Windows exe's `VERSIONINFO` resource (`packaging/windows/roift_gui.rc.in`)
- every package file name, via `CPACK_PACKAGE_VERSION`

`vcpkg.json` deliberately carries no `version-string`: it exists to acquire
dependencies, not to be published as a port, and a second version field would
only drift.

The release line is **v1.1.x**. `v1.0.0` and `v1.0.1` are older releases that
shipped prebuilt vcpkg dependency bundles rather than the app, so the app's
versions start above them.

## Install tree

`cmake --install` produces the tree both packagers consume.

```
bin/roift_gui                  RPATH $ORIGIN/../lib
bin/oiftrelax, bin/exp_*       CLI tools, found via applicationDirPath()
share/applications/            roift_gui.desktop
share/icons/hicolor/…/apps/    roift_gui.png, roift_gui.svg
share/metainfo/                roift_gui.metainfo.xml
share/doc/roift_gui/LICENSE
```

The ROIFT tools sit in the same `bin/` as the GUI on purpose:
`SegmentationRunner::resolveRoiftExecutable` searches
`QCoreApplication::applicationDirPath()` first, so a packaged app finds its own
segmentation binaries without any path configuration.

On Windows the same install adds every runtime DLL and Qt plugin subtree that
`windeployqt` and vcpkg's app-local deployment already placed beside the built
exe. Configuring with `-DROIFT_COPY_RUNTIME=OFF` skips that deployment and CMake
warns that the resulting packages will be incomplete.

## Icons

`packaging/make_icons.py` draws one design — an axial CT slice with one lung
segmented, in the `src/Theme.h` palette — and emits all three formats from it,
so SVG, PNG and ICO cannot drift apart. Re-run it after any edit:

```bash
python3 packaging/make_icons.py     # needs Pillow and numpy
```

| Output | Used by |
| --- | --- |
| `resources/icons/roift_gui-256.png` | `resources/roift_gui.qrc` → window and taskbar icon; hicolor 256×256; the AppImage's icon |
| `resources/icons/roift_gui.svg` | hicolor `scalable/apps`, for menus that prefer vectors |
| `packaging/windows/roift_gui.ico` | the exe's icon resource and the NSIS installer |

## Linux packages

```bash
packaging/linux/build-appimage.sh <build-dir> [output-dir]
packaging/linux/build-deb.sh <appimage-file> [output-dir]
```

`build-appimage.sh` stages `cmake --install` into an AppDir and runs
`linuxdeploy` with its Qt plugin. Two things matter for it to work:

- **linuxdeploy must see the same libraries the binary linked against.** The
  script adds `$CONDA_PREFIX/lib` to `LD_LIBRARY_PATH` only when the staged
  binary actually has unresolved dependencies, so on a developer machine it does
  not shadow the system Qt.
- **It must pick the right qmake.** Several Qt installs usually coexist, so the
  script compares each candidate's `QT_INSTALL_LIBS` against the `libQt6Core.so.6`
  that `ldd` resolves for the installed binary, and only falls back to the first
  candidate (with a warning) if none matches. `QMAKE=/path/to/qmake6` overrides.

Each ROIFT CLI tool is passed to linuxdeploy with `--executable` so its
dependencies get bundled too, not just the GUI's.

`build-deb.sh` extracts that AppImage rather than re-running `cmake --install`,
because the AppImage is the only tree that contains *every* runtime library. The
package is deliberately fat: the bundle goes under `/opt/roift_gui`, with a
three-line `/usr/bin/roift_gui` launcher, so it has no apt dependency on Qt6 or
VTK. The launcher `exec`s the real binary instead of copying it — that is what
keeps `oiftrelax` next to `applicationDirPath()`.

**Size.** ITK and VTK are large and both are bundled, so expect a few hundred MB
uncompressed. Leaving them out is not an option: no distro ships an ITK the app
could rely on.

**glibc floor.** An AppImage inherits the glibc of the machine that built it.
CI builds on `ubuntu-22.04` (glibc 2.35), which sets the oldest distro the
AppImage runs on. Raising the runner image in `.github/workflows/release.yml`
raises that floor.

## Windows packages

CPack builds both from the install tree:

```powershell
cmake --build build --config Release
cd build
cpack -C Release
```

producing `roift_gui-<version>-win64.exe` (NSIS: Start-menu and desktop
shortcuts, uninstaller) and `roift_gui-<version>-win64.zip` (unzip and run
`bin\roift_gui.exe`). Neither is code-signed, so SmartScreen warns about an
unrecognized publisher.

The exe is a GUI-subsystem binary (`WIN32_EXECUTABLE`), so no console window
opens behind it. `--help` and `--version` still print to stderr when launched
from a console, and fall back to a message box when there is none.

## Dependencies: one path per platform

Windows dependencies come from **vcpkg**, pinned by the `builtin-baseline` in
`vcpkg.json`. Pinning is what makes the binary cache useful — an unpinned vcpkg
moves under CI and invalidates it.

Linux dependencies come from **conda-forge** (`packaging/ci/env.yml`). There is
no second path: the old `src/*download.cmake` modules that fetched Qt, ITK, curl
and zlib directly were never included by any `CMakeLists.txt` and have been
removed rather than left to diverge.

## CI

**`pr.yml`** — on every pull request to `main`, builds and tests on Linux
(conda-forge) and Windows (vcpkg), and checks that the Linux install tree has
the files the packagers expect. It packages nothing.

**`release.yml`** — on a push to `main`:

1. `version` reads the version out of `CMakeLists.txt`. If no tag exists for it
   yet, that version ships as-is; otherwise the patch is bumped and the change
   is committed back with a `[release]` marker. Either way the tag is pushed.
   Editing `project(... VERSION 1.2.0)` in a PR is therefore how you open a new
   minor line.
2. `linux` and `windows` check out that tag, build, test and package.
3. `release` renames the four assets to the tag and publishes the GitHub Release.

Pushing a tag by hand is an equivalent entry point: `version` passes it through
and the same build-and-publish path runs.

**No PAT is needed.** A tag pushed with `GITHUB_TOKEN` does not start new
workflow runs, so a separate tag-triggered workflow would never fire. The build
jobs therefore depend on `version` inside the same run. The `[release]` marker
on the bump commit is belt-and-braces for anyone who later swaps in a token that
*does* retrigger.

The first Windows run builds Qt, VTK and ITK from source and takes hours —
GitHub's job limit is 6 h. `VCPKG_BINARY_SOURCES: 'clear;x-gha,readwrite'`
caches that build in the Actions cache, so later runs are minutes. If the cold
build times out, re-run the job: the cache keeps whatever finished.
