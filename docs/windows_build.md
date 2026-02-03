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
