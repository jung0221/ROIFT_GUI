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
.github/workflows/      pr.yml (build, test, package), release.yml (tag, publish)
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
`linuxdeploy` with its Qt plugin. Four conditions must hold for it to work.

- **linuxdeploy must resolve the same libraries the binary linked against.** The
  script adds `$CONDA_PREFIX/lib` to `LD_LIBRARY_PATH` when the Qt the binary
  links resolves from that prefix. Testing the binary for unresolved
  dependencies does not suffice: conda's linker records the prefix in the
  binary's RUNPATH, so the binary always resolves, while the Qt plugins copied
  afterwards need `libGL` and `libEGL` from the prefix's `libglvnd`. Without it
  the Qt plugin aborts with `Could not find dependency: libEGL.so.1`, on any
  host lacking those libraries, which includes the CI runner. A system Qt on a
  developer machine with conda active is left alone.
- **It must select the matching qmake.** Several Qt installations usually
  coexist, so the script compares each candidate's `QT_INSTALL_LIBS` with the
  `libQt6Core.so.6` that `ldd` resolves for the installed binary, and falls back
  to the first candidate, with a warning, only if none matches.
  `QMAKE=/path/to/qmake6` overrides the search.
- **Every executable's dependencies must be bundled.** Each ROIFT CLI tool is
  passed with `--executable`, not only the GUI.
- **Headless use must work.** `EXTRA_PLATFORM_PLUGINS` adds the offscreen and
  minimal platform plugins. The Qt plugin otherwise bundles only the one the
  build machine used (xcb), and a headless invocation such as `--version` over
  SSH fails with *"Available platform plugins are: xcb"*.

`build-deb.sh` extracts that AppImage rather than re-running `cmake --install`,
because the AppImage is the only tree containing every runtime library. The
package is deliberately self-contained: the bundle goes under `/opt/roift_gui`,
with a short `/usr/bin/roift_gui` launcher, so it has no apt dependency on Qt6
or VTK. The launcher `exec`s the real binary rather than a copy, which keeps
`oiftrelax` next to `applicationDirPath()`.

### The C++ runtime

conda-forge builds Qt, VTK and ITK with a recent GCC (GCC 16, `GLIBCXX_3.4.36`,
at the time of writing), and linuxdeploy never bundles `libstdc++`. On a host
with an older runtime, Ubuntu 22.04 among them (`GLIBCXX_3.4.30`), neither
package would start: `version 'CXXABI_1.3.15' not found`.

Both packages therefore carry `libstdc++.so.6` and `libgcc_s.so.1` in
`lib/cxxrt/`, outside the RUNPATH, and select a runtime at launch.
`packaging/linux/cxxrt-select.sh` compares the highest `GLIBCXX` version of the
bundled and host runtimes and puts `lib/cxxrt` on `LD_LIBRARY_PATH` only when
the bundled one is newer. The newer runtime is chosen because `libstdc++` is
backward compatible only: a newer host may load a GPU driver into the process
that needs the host's own runtime. The AppImage's `AppRun` and the `.deb`'s
launcher both source the script.

Consequently the binaries under `bin/` are not intended to be run directly on an
older host: `/opt/roift_gui/bin/oiftrelax` fails there with the error above.
Started through the launcher, the GUI passes the selection on to the
`oiftrelax` processes it spawns.

### Host libraries and `Depends`

linuxdeploy declines to bundle a fixed list of libraries it expects every
desktop system to provide (libc, the GL dispatch libraries, X11, fontconfig,
freetype, harfbuzz and others), logging each as *"Skipping deployment of
blacklisted library"*. The `.deb`'s `Depends` names the packages providing
exactly those libraries. When the dependency set changes, re-derive it from that
log.

### Size and supported systems

ITK and VTK are both bundled (79 and 57 shared libraries), which puts the
AppImage at approximately 125 MB and the `.deb` at approximately 100 MB. Leaving
them out is not an option: no distribution ships an ITK the application could
rely on, and Debian's VTK is built against Qt5.

The glibc floor is set by the machine that builds the AppImage: CI builds on
`ubuntu-22.04` (glibc 2.35). Raising the runner image in the workflows raises
that floor.

Verified on clean `ubuntu:22.04` and `ubuntu:24.04` containers: the `.deb`
installs with `apt`, `roift_gui --version` and the AppImage start with
`LD_BIND_NOW=1`, and the bundled `oiftrelax` segments a `.nii.gz` phantom. On
22.04 the GUI was also started on a virtual display, loading a volume into all
three views with the bundled runtime and Mesa's software GL driver in the same
process. Other distributions are unverified.

### Regression test

`tests/oiftrelax_nifti_gz_test.py` (CTest `oiftrelax_nifti_gz`) segments a
synthetic sphere written as `.nii.gz` and requires a Dice coefficient of at
least 0.9. It uses the Python standard library only, so it is never skipped. It
guards the failure in which a standalone build compiled `gft` without zlib and
`oiftrelax` crashed on every gzipped volume; `CMakeLists.txt` now calls
`find_package(ZLIB)` before adding `roift`, whose own test for zlib runs before
its own `find_package`.

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
`vcpkg.json`. Pinning is what makes the binary cache useful: an unpinned vcpkg
moves under CI and invalidates it. Two further files shape what vcpkg builds:

| file | effect |
| --- | --- |
| `vcpkg.json` | requests qtbase's `windeployqt` feature, which is opt-in as of qtbase 6.11.1 |
| `vcpkg-configuration.json` | loads the overlay triplet in `triplets/`; vcpkg reads it automatically in manifest mode |
| `triplets/x64-windows.cmake` | builds every port **release-only**, and sets `CMAKE_SUPPRESS_REGENERATION=ON` |

Release-only halves the build and is what lets Qt6, VTK and ITK fit on a CI
runner's disk; a debug build of all three filled it. It also applies to local
builds, so a Debug configuration of the app finds no debug libraries. To develop
against Debug, comment out `VCPKG_BUILD_TYPE` in the triplet and reconfigure.
`CMAKE_SUPPRESS_REGENERATION` works around a CMake 4.x Ninja rule that loops
while building `liblzma` and `tiff`.

Without a toolchain file, `CMakeLists.txt` falls back to a `vcpkg_installed/`
tree at the repository root, the layout `scripts/restore_prebuilt.cmd` unpacks.
The prebuilt bundles attached to the `v1.0.0` and `v1.0.1` releases **cannot
build the current application**: the `v1.0.0` archive holds Qt6, ITK and zlib
but no VTK, and the `v1.0.1` archive holds runtime DLLs only. Configuring
against either stops at `find_package(VTK)`.

Linux dependencies come from **conda-forge** (`packaging/ci/env.yml`). ITK is
`libitk-devel`: the conda-forge package named `itk` is the Python binding and
carries neither headers nor `ITKConfig.cmake`. There is no second path: the old
`src/*download.cmake` modules that fetched Qt, ITK, curl and zlib directly were
never included by any `CMakeLists.txt` and have been removed rather than left to
diverge.

## CI

**`pr.yml`** runs on every pull request to `main`. It builds and tests on Linux
(conda-forge) and Windows (vcpkg), and produces the same packages a release
would, uploaded as run artifacts: `linux-packages` (AppImage and `.deb`) and
`windows-packages` (installer and ZIP). A pull request is therefore green only
if the release jobs would succeed on the same commit.

**`release.yml`** runs on a push to `main`:

1. `version` reads the version out of `CMakeLists.txt`. If no tag exists for it
   yet, that version ships as-is; otherwise the patch is bumped and the change
   is committed back with a `[release]` marker. Either way the tag is pushed.
   Editing `project(... VERSION 1.2.0)` in a pull request is therefore how a new
   minor line is opened.
2. `linux` and `windows` check out that tag, build, test and package.
3. `release` renames the four assets to the tag and publishes the GitHub Release.

Pushing a tag by hand is an equivalent entry point: `version` passes it through
and the same build-and-publish path runs.

**No personal access token is needed.** A tag pushed with `GITHUB_TOKEN` does
not start new workflow runs, so a separate tag-triggered workflow would never
fire. The build jobs therefore depend on `version` inside the same run. The
`[release]` marker on the bump commit prevents a loop should a token that does
retrigger workflows be substituted later.

### When a release run fails

The tag is pushed **before** anything is built, so a failed `linux` or `windows`
job leaves a tag with no Release attached. How to recover depends on the cause:

| cause | recovery |
| --- | --- |
| transient (network, runner) | `gh run rerun --failed <run-id>` rebuilds only the failed job, then publishes |
| a defect in the repository | fix it and merge; the next run tags the next patch and publishes that |

Re-running cannot recover from a defect, because the re-run checks out the tag,
and the tag still points at the broken commit. The orphaned tag is harmless and
can be deleted with `git push --delete origin <tag>`.

### Windows build time and caching

Building Qt6, VTK and ITK from source takes approximately three hours, against a
GitHub job limit of six. Three measures keep that affordable:

- **Binary cache.** vcpkg writes built packages to a directory that
  `actions/cache` carries between runs; a warm run restores all dependencies in
  under a minute. vcpkg's own `x-gha` backend was removed and silently caches
  nothing, which is why it is not used. The save key is unique per run and the
  restore key is prefix-matched. On `main` the save runs whatever the outcome,
  so a run that fails still banks whatever it built; elsewhere it runs only once
  the dependencies are fully installed (see the scoping rule below).
- **Step timeout.** The long step has `timeout-minutes: 300`. A step that times
  out still lets the cache save run; a job killed at the six-hour limit saves
  nothing.
- **Test timeout.** `ctest --timeout 300` bounds each test. A GUI test that
  aborts on Windows can wait on an error dialog indefinitely rather than fail.
- **Keeping the cache.** GitHub deletes a cache entry not restored for 7 days,
  after which the next Windows build is cold again. `cache-warm.yml` restores
  the newest entry every Monday so that a quiet period does not cost a
  three-hour rebuild. Scheduled workflows are disabled after 60 days without
  repository activity; re-enable it from the Actions tab if that happens.

Caches are scoped: a run can restore entries saved by its own branch or by
`main`, but not by another branch or pull request, and it searches its own
branch first. The cache that serves releases and new pull requests is therefore
the one saved by runs on `main`. It is also why a pull request never saves a
partial cache: an entry left by a cancelled run would be found before `main`'s
complete one, and every later run on that pull request would rebuild what it
lacks. A pull request whose cold build times out consequently banks nothing, a
case that arises only when `main` itself has no cache.
