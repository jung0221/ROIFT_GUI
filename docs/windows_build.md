## System dependencies
Required tools/libraries to build `ROIFT_GUI`:
- CMake (3.16+ recommended)
- A C++17 capable compiler (g++ 9+ or clang)
- zlib (used for minimal .nii.gz handling)
- ITK (to enable robust NIfTI I/O and merging) — if found by CMake it will be used. Without ITK some mask features (loading .nii.gz fallback) will still be available but some advanced features are disabled.
- Observation: buiding this program will install a local QT6

## Configure and build
```bash
# clone and prepare vcpkg
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat

# in windows powershell, install itk, zlib, and curl to x64
.\vcpkg.exe install itk:x64-windows zlib:x64-windows curl:x64-windows

# build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ` -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ` -DVCPKG_TARGET_TRIPLET=x64-windows ` -DUSE_ITK=ON
cmake --build build --config Release
```
``` 

## Where the binaries are
- GUI: `build/Release/roift_gui` (or the build directory you used)
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