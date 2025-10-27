# seed_gui_cpp — Overview and build documentation

This document explains how `seed_gui_cpp` (the manual seed editor / small GUI) is structured, how to build it, run it, and where to find the important code paths when developing or extending it.

## Goal
`seed_gui_cpp` is a small Qt-based application used to:
- Visualize 3D medical images (NIfTI), view axial/sagittal/coronal slices.
- Place manual seeds for segmentation and edit masks (draw/erase per-label).
- Launch an external segmentation tool (ROIFT / `oiftrelax`) for per-label segmentation and merge results.
- Save/load seeds and save/load masks.

## High level architecture
- `ManualSeedSelector` (src/ManualSeedSelector.*)
  - Main window and the primary UI. Holds image, seeds, mask buffer and three `OrthogonalView` widgets (axial/sagittal/coronal).
  - Handles user interactions: mouse clicks to add seeds, mask painting, keyboard navigation (W/A/S/D/Q/E), sliders, and the menu buttons.
  - Exposes a small public API used by helper modules:
    - `getSeeds()` — read-only access to current seeds
    - `getImagePath()` — path of loaded image
    - `applyMaskFromPath(path)` — convenience wrapper to load a mask and refresh views

- `SegmentationRunner` (src/SegmentationRunner.*)
  - Presents a small dialog to configure ROIFT parameters and runs per-label ROIFT.
  - Supports "segment all" mode that writes per-label seed files, runs ROIFT per-label (parallelized up to a configured cap), collects outputs, and merges them into a single multilabel NIfTI which is then loaded into the GUI as the mask.
  - Uses `QProcess` to spawn external `oiftrelax` processes and currently uses a simple scheduler. The concurrency cap is enforced in this module.

- `NiftiImage` (src/NiftiImage.*)
  - A small wrapper for reading NIfTI images (ITK-backed when available). Provides helper functions to get axial/sagittal/coronal slices as RGB buffers used by `OrthogonalView`.

- `OrthogonalView` (src/OrthogonalView.*)
  - A Qt widget that shows an image slice (QImage), handles panning/zoom and mouse events and supports an overlay callback used for drawing seeds and interactive overlays.

- Dialogs
  - `SeedOptionsDialog` and `MaskOptionsDialog` are small UI dialogs that control seed drawing mode, brush radius, mask save/load, and other options.

- Scripts (Python)
  - `scripts/overlay_nifti_labels.py`: overlay labels from a new multilabel NIfTI into an old one (new non-zero voxels replace old values).
  - `scripts/replace_nifti_labels.py`: label mapping utility (less used now, kept in repo).

## Build dependencies
Required tools/libraries to build `seed_gui_cpp`:
- CMake (3.10+ recommended)
- A C++17 capable compiler (g++ 9+ or clang)
- Qt 5 (QtWidgets, QtCore, QtGui) or compatible Qt 6 depending on your toolchain
- zlib (already used for minimal .nii.gz handling)

Optional (but recommended):
- ITK (to enable robust NIfTI I/O and merging) — if found by CMake it will be used. Without ITK some mask features (loading .nii.gz fallback) will still be available but some advanced features are disabled.

Platform packages (example for Debian/Ubuntu; package names may vary):

```bash
sudo apt update
sudo apt install build-essential cmake git libqt5widgets5 libqt5core5a libqt5gui5 qtbase5-dev qttools5-dev-tools libgl1-mesa-dev libx11-dev zlib1g-dev libhdf5-dev
```

Notes about conda/anaconda: if you build inside a conda environment you may get conflicting runtime libraries (OpenGL, zlib, etc.). If runtime loader issues appear, either build with the system toolchain or ensure LD_LIBRARY_PATH does not pick incompatible conda libs at runtime.

## Build steps
From the repository root:

```bash
# create a separate build dir for the GUI
mkdir -p build && cd build
cmake -DUSE_ITK=ON -S ..
cmake --build /home/jung/Documents/Doutorado/repos/ROIFT_GUI/build -j$(nproc)
```


If CMake discovers ITK (and you want ITK), it will enable extra capabilities (mask saving, merging multiple label outputs, faster I/O).

### Common CMake options
- To force ITK on/off: pass -DUSE_ITK=ON or -DUSE_ITK=OFF (project uses HAVE_ITK macro if ITK detected).
- For debug builds: -DCMAKE_BUILD_TYPE=Debug

## Running
After building, the binary is at `roift_gui/build/roift_gui`.

Simple GUI run (open without a provided file):

```bash
./roift_gui
```

Open directly with an input image (example used during testing):

```bash
./roift_gui --input ../data/pseudo_label/240723\ GRX_Left\ Foot/240723\ GRX_Left\ Foot.nii.gz
```

Keyboard shortcuts (slice navigation):
- W: axial + (next axial slice)
- S: axial -
- D: sagittal +
- A: sagittal -
- E: coronal +
- Q: coronal -
- [ and ]: decrement/increment all three slices together

Mouse:
- Left-click in a view to add seeds (when seed mode is draw)
- Hold left drag to draw mask strokes when in mask mode
- Right-click to erase (or use mask dialog's erase mode)

Mask I/O:
- `Mask Options` dialog exposes load/save. When built with ITK the app saves masks as NIfTI using int16 as the pixel type.
- Segmentation outputs from `SegmentationRunner` are also merged using ITK when available.

Segmentation (ROIFT / `oiftrelax`):
- The segmentation button launches a small dialog that can run a single label segmentation or batch-run per-label using seeds.
- `SegmentationRunner` looks for `oiftrelax` in PATH or common locations. Ensure the external executable is installed and runnable.
- The per-label outputs are merged into `segmentation_multilabel.nii.gz` and loaded automatically as the mask.

## File layout (key files)
- `seed_gui_cpp/src/ManualSeedSelector.*` — main UI and glue code.
- `seed_gui_cpp/src/SegmentationRunner.*` — ROI/segmentation dialog and runner logic.
- `seed_gui_cpp/src/NiftiImage.*` — thin wrapper for image I/O and slice helpers.
- `seed_gui_cpp/src/OrthogonalView.*` — custom Qt widget for orthogonal slices and overlays.
- `seed_gui_cpp/src/SeedOptionsDialog.*`, `MaskOptionsDialog.*` — supplemental dialogs.
- `scripts/` — helper Python scripts (overlaying labels, replacing labels, etc.).

## Developer notes
- Concurrency cap for parallel ROIFT processes is implemented in `SegmentationRunner.cpp`. Search for `QThread::idealThreadCount()` or the integer cap (5 or 10) in that file to adjust the runtime cap or expose it in the dialog.
- To add a new dialog or widget, add headers in `src/` and add the sources to the `CMakeLists.txt` under the roift_gui target.
- `ManualSeedSelector` exposes `applyMaskFromPath()` which is the easy path for loaders that want to programmatically set the mask and refresh views.
- For better async process handling, prefer connecting `QProcess::finished` signals to slots instead of busy-polling; current code uses a scheduler with explicit deletion logic — refactor carefully to avoid ownership double-free.

## Troubleshooting
- If save fails with an ITK error complaining about unknown IO object, make sure the output filename has a `.nii` or `.nii.gz` extension. The app will append `.nii.gz` if a suffix is missing.
- If the app crashes on loading a second image: ensure that `m_maskData` is cleared on load (code already does this in recent edits). If you see OOMs in heavy segmentation runs, reduce the segmentation parallel cap.
- If keyboard navigation doesn't affect slices, ensure focus/cursor is in a view (the code accepts focus or the mouse-over state for key handling).

## Testing and QA
- Quick manual tests:
  - Load a sample NIfTI, place seeds for two different labels, run "segment all" and verify `segmentation_multilabel.nii.gz` is produced and loaded.
  - Save a mask and verify with nibabel (`python -c "import nibabel as nib; print(nib.load('mask.nii.gz').get_fdata().dtype)"`) that dtype is `int16`.

## Scripts
- `scripts/overlay_nifti_labels.py` — overlay `new` labels into `old` image where `new != 0`. Usage:

```bash
python scripts/overlay_nifti_labels.py --old old.nii.gz --new new.nii.gz --out merged.nii.gz
```

- `scripts/replace_nifti_labels.py` — replace label mapping utility. Used for explicit remapping (kept for completeness but for the workflow above `overlay_nifti_labels.py` is usually sufficient).

## Next improvements (suggestions)
- Replace polling-based QProcess monitoring with fully asynchronous signal/slot handling (`QProcess::finished`) and use a job queue class.
- Expose concurrency cap as a field in the segmentation dialog.
- Add a small progress bar while running batch segmentations and an explicit Cancel button that cleanly kills children and cleans up temp files.
- Add unit/integration tests (seeding, mask merging) and a CI job that builds with and without ITK.

---

If you'd like, I can:
- Add this file to the repository (already created under `docs/seed_gui_cpp.md`).
- Create a brief `README.md` in `seed_gui_cpp/` that points to this doc.
- Add a quick checklist for debugging common runtime errors (OOM, missing `oiftrelax`, or ITK missing). Which of these should I add next?
