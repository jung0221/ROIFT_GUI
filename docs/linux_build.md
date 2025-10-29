# Linux build (Debian/Ubuntu example)

This file contains the system-level build dependencies and step-by-step instructions for building ROIFT_GUI on a Linux system (Debian/Ubuntu-style commands). Adjust package names for other distributions.

## System dependencies
Required tools/libraries to build `ROIFT_GUI`:
- CMake (3.16+ recommended)
- A C++17 capable compiler (g++ 9+ or clang)
- Qt 5 (QtWidgets, QtCore, QtGui) or compatible Qt 6 depending on your toolchain
- zlib (used for minimal .nii.gz handling)
- ITK (to enable robust NIfTI I/O and merging) — if found by CMake it will be used. Without ITK some mask features (loading .nii.gz fallback) will still be available but some advanced features are disabled.

Example packages for Debian/Ubuntu (may vary by release):

```bash
sudo apt update
sudo apt install build-essential cmake git libqt5widgets5 libqt5core5a libqt5gui5 qtbase5-dev qttools5-dev-tools libgl1-mesa-dev libx11-dev zlib1g-dev libhdf5-dev libinsighttoolkit5-dev
```

Notes about conda/anaconda: if you build inside a conda environment you may get conflicting runtime libraries (OpenGL, zlib, libcurl, etc.). If you see runtime loader issues, either build and run with the system toolchain (deactivate conda) or ensure LD_LIBRARY_PATH does not pick incompatible conda libs at runtime.

## Configure and build
From the repository root:

```bash
cmake -S . -B build
# build
cmake --build build -j$(nproc)
```

## Where the binaries are
- GUI: `build/roift_gui` (or the build directory you used)
- If the repository contains the ROIFT implementation and you build it, the `oiftrelax` binary will be in the build directory as well (check `build/roift` or `build/roift/roift` depending on the layout).

## Quick runtime check
After build, run the GUI:

```bash
./build/roift_gui
```

To run the GUI opening an image directly:

```bash
./build/roift_gui --input path/to/image.nii.gz
```

If you encounter shared-library conflicts at runtime (errors about missing or incompatible libX11, libcurl, libpng, etc.), try deactivating conda or running the binary with a clean environment:

```bash
# temporarily run with a clean environment (keeps PATH)
env -i PATH="$PATH" ./build/roift_gui
```