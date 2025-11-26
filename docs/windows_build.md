## System requirements

Required tools and libraries to build `ROIFT_GUI` on Windows:

- CMake (3.16+ recommended)
- A C++17-capable compiler (MSVC / Visual Studio)

## Build using the prebuilt vcpkg artifacts

This repository includes a prebuilt archive of the vcpkg artifacts (ITK, Qt6, curl, etc.) to simplify building on Windows. The restore script will unpack the prebuilt libraries and DLLs into the repository so you don't need a networked vcpkg installation.

1. Restore the prebuilt artifacts (run in `cmd.exe`, from the repository root):

```bat
.\scripts\restore_prebuilt.cmd
```

2. Configure and build (out-of-source, example for x64 Release):

```bat
:: Configure the build
cmake -S . -B build -A x64

:: Build the Release configuration
cmake --build build --config Release
```

Notes:

- The prebuilt artifacts include both library files and runtime DLLs. The project's CMake setup copies only the matching configuration's DLLs (Release vs Debug) next to the built executables to avoid mixing CRTs.

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
