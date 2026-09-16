## System requirements

Required tools and libraries to build `ROIFT_GUI` on Windows:

- CMake (3.16+ recommended)
- A C++17-capable compiler (MSVC / Visual Studio)

## Build using an external vcpkg installation

To install all dependencies, using vcpkg is recommended. This approach allows vcpkg to automatically download and install dependencies listed in `vcpkg.json`.

### Step 1: Clone vcpkg (if you don't have it already)

```bat
:: Choose a location for vcpkg, e.g., D:\vcpkg or C:\dev\vcpkg
cd D:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

:: Bootstrap vcpkg (run once to set it up)
.\bootstrap-vcpkg.bat
```

After bootstrapping, you'll have a working vcpkg installation at `<YOUR_VCPKG_PATH>` (e.g., `D:\vcpkg`).

### Step 2: Configure CMake with vcpkg toolchain

Set an environment variable or replace `<YOUR_VCPKG_PATH>` with your actual vcpkg root directory (e.g., `D:\vcpkg`):

```bat
:: From the ROIFT_GUI repository root
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=<YOUR_VCPKG_PATH>\scripts\buildsystems\vcpkg.cmake
```

Example with a specific path:

```bat
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

### Step 3: Build the project

```bat
cmake --build build --config Release
```

For a faster build, you can specify the number of parallel jobs:

```bat
cmake --build build --config Release --parallel 4
```

The dependency set is pinned by `builtin-baseline` in `vcpkg.json`. To build
against the same ports CI uses, check your vcpkg clone out at that commit:

```bat
git -C <YOUR_VCPKG_PATH> checkout <the builtin-baseline value>
<YOUR_VCPKG_PATH>\bootstrap-vcpkg.bat
```

### What vcpkg builds, and how long it takes

The first configure builds Qt6, VTK and ITK from source, which takes several
hours. Two repository files change what is built:

- `vcpkg-configuration.json` loads `triplets/x64-windows.cmake`, which builds
  every dependency **release-only**. A Debug configuration of the application
  therefore finds no debug libraries to link against. To build the application
  in Debug, comment out `VCPKG_BUILD_TYPE` in that triplet and reconfigure; the
  dependencies are then built twice and need correspondingly more time and disk.
- `vcpkg.json` requests qtbase's `windeployqt` feature. As of qtbase 6.11.1 the
  tool is opt-in; without it the build tree gets no Qt runtime beside the exe.

### The prebuilt bundles do not build this version

`scripts/restore_prebuilt.cmd` unpacks a `vcpkg_installed/` tree so that a
configure without `-DCMAKE_TOOLCHAIN_FILE` can skip vcpkg entirely. The archives
attached to the `v1.0.0` and `v1.0.1` releases predate VTK becoming a
dependency: `v1.0.0` holds Qt6, ITK and zlib, and `v1.0.1` holds runtime DLLs
only. Configuring against either fails at `find_package(VTK)`. Use the vcpkg
toolchain as described above.

## Produced binaries and locations

- GUI executable: `build\Release\roift_gui.exe`
- CLI segmentation tool (if built): `build\roift\oiftrelax.exe` or `build\roift\Release\oiftrelax.exe` depending on how CMake configured targets

## Quick runtime checks

- Run the GUI without an input file:

```bat
.\build\Release\roift_gui.exe
```

- Run the GUI and open an image directly:

```bat
.\build\Release\roift_gui.exe --input C:\path\to\image.nii.gz
```

The exe is built for the GUI subsystem, so no console window opens behind it.
Run it from a terminal to see `--version`/`--help` output and the diagnostics
the app writes to stderr; launched from Explorer, `--version` and `--help` fall
back to a message box.

## Installer and portable ZIP

```bat
cd build
cpack -C Release
```

produces `roift_gui-<version>-win64.exe` and `roift_gui-<version>-win64.zip`.
See [packaging.md](packaging.md) for what goes into them.
