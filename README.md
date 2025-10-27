# ROIFT_GUI — Overview and build documentation

## Goal
`ROIFT_GUI` is a small Qt-based application used to:
- Visualize 3D medical images (NIfTI), view axial/sagittal/coronal slices.
- Place manual seeds for segmentation and edit masks (draw/erase per-label).
- Launch an external segmentation tool (ROIFT / `oiftrelax`) for per-label segmentation and merge results.
- Save/load seeds and save/load masks.

## Build dependencies
Required tools/libraries to build `ROIFT_GUI`:
- CMake
- A C++17 capable compiler (g++ 9+ or clang)
- Qt 5 (QtWidgets, QtCore, QtGui) or compatible Qt 6 depending on your toolchain
- zlib (already used for minimal .nii.gz handling)
- ITK (to enable robust NIfTI I/O and merging) — if found by CMake it will be used. Without ITK some mask features (loading .nii.gz fallback) will still be available but some advanced features are disabled.

Platform packages (example for Debian/Ubuntu; package names may vary):

```bash
sudo apt install build-essential cmake git libqt5widgets5 libqt5core5a libqt5gui5 qtbase5-dev qttools5-dev-tools libgl1-mesa-dev libx11-dev zlib1g-dev libhdf5-dev
```

Notes about conda/anaconda: if you build inside a conda environment you may get conflicting runtime libraries (OpenGL, zlib, etc.). If runtime loader issues appear, either build with the system toolchain or ensure LD_LIBRARY_PATH does not pick incompatible conda libs at runtime.

## Build steps
From the repository root:

```bash
mkdir -p build && cd build
cmake -DUSE_ITK=ON -S ..
cmake --build . -j$(nproc)
```

## Running
After building, the binary is at `build/roift_gui`.

Simple GUI run (open without a provided file):

```bash
./build/roift_gui
```

Open directly with an input image (example used during testing):

```bash
./build/roift_gui --input example_image.nii.gz
```


Segmentation (ROIFT / `oiftrelax`):
- The segmentation button launches a small dialog that can run a single label segmentation or batch-run per-label using seeds.
- `SegmentationRunner` looks for `oiftrelax` in PATH or common locations. Ensure the external executable is installed and runnable.
- The per-label outputs are merged into `segmentation_multilabel.nii.gz` and loaded automatically as the mask.

Documentation
-------------
Additional project documentation is available in the `docs/` directory:

- `docs/usage.md` — keyboard/mouse shortcuts and quick usage notes for the GUI.
- `docs/architecture.md` — high-level architecture and descriptions of the main modules (`ManualSeedSelector`, `SegmentationRunner`, `NiftiImage`, etc.).