# Linux build

ROIFT_GUI is a Qt **6** application. That single fact decides how you get the
dependencies: it needs VTK built against Qt6, and Debian/Ubuntu ship VTK with
Qt5 bindings only. `apt` cannot satisfy this build. Use conda-forge, which is
also what CI does — see `packaging/ci/env.yml`, the single source of truth for
the dependency set.

## Dependencies

| Need | Package |
| --- | --- |
| Qt 6 (Widgets, optionally Svg) | `qt6-main` |
| VTK 9 with Qt6 support | `vtk` |
| ITK (NIfTI/DICOM I/O) | `libitk-devel` — conda-forge's `itk` is the *Python* package |
| zlib (`.nii.gz` in the ROIFT tools) | `zlib` |
| Toolchain | `cmake`, `ninja`, `cxx-compiler`, `binutils` |
| OpenGL headers | `libgl-devel` |

`micromamba`, `mamba` and `conda` all work; substitute whichever you have.

```bash
micromamba create -y -n roift -f packaging/ci/env.yml
micromamba activate roift
```

## Configure and build

```bash
cmake -B build -S . -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
  -DBUILD_ROIFT_TESTS=ON \
  -DBUILD_GPU_OIFT=OFF
cmake --build build -j"$(nproc)"
```

`-DCMAKE_PREFIX_PATH="$CONDA_PREFIX"` is what points CMake at the environment's
Qt6/VTK/ITK instead of a system copy. Drop `-DBUILD_GPU_OIFT=OFF` if you have
CUDA and want `oiftrelax_gpu`; without a CUDA compiler the GPU target is skipped
anyway.

## Where the binaries are

- GUI: `build/roift_gui`
- ROIFT CLI tools: `build/roift/oiftrelax`, `build/roift/experiments/exp_*`
  (and `build/roift/gpu/oiftrelax_gpu` with CUDA)

`SegmentationRunner` looks for these next to the GUI executable, then in PATH,
then in the usual build subdirectories, so a plain build tree works with no
extra setup. `ROIFT_EXECUTABLE` overrides the search with an explicit path.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

The four tests are `ui_paths`, `wheel_guard`, `mask_overlay` and `npz_import`.
No display is needed: the two that build widgets set `QT_QPA_PLATFORM=offscreen`
themselves, so there is no `xvfb-run` in the loop. `npz_import` reports as
skipped unless numpy and SimpleITK are importable.

## Run

```bash
./build/roift_gui                                    # empty window
./build/roift_gui --input path/to/image.nii.gz       # open a volume
./build/roift_gui --version
```

## Mixing conda and system libraries

Building inside a conda environment and then running against system OpenGL (or
the other way round) is the usual source of loader errors — messages about
`libGL`, `libstdc++`, `libcurl` or `libpng` being missing or the wrong version.
Keep the environment you built in active when you run the build-tree binary. If
you want something that runs anywhere, build a package instead — the AppImage
and `.deb` bundle every non-system library. See [packaging.md](packaging.md).

## Packaging

```bash
packaging/linux/build-appimage.sh build dist
packaging/linux/build-deb.sh dist/*.AppImage dist
```

Details, including what ends up inside each package, are in
[packaging.md](packaging.md).
