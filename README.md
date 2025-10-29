# ROIFT_GUI — Overview and build documentation

## Goal
`ROIFT_GUI` is a small Qt and ITK application used to:
- Visualize 3D medical images (NIfTI), view axial/sagittal/coronal slices.
- Place manual seeds for segmentation and edit masks (draw/erase per-label).
- Launch an external segmentation tool (ROIFT / `oiftrelax`) for per-label segmentation and merge results.
- Save/load seeds and save/load masks.

## Build
For platform-specific build instructions and troubleshooting, see the documentation in the docs/ directory:
- docs/linux_build.md — Linux (Debian/Ubuntu) build instructions and troubleshooting.
- docs/windows_build.md — Windows (Visual Studio / MSVC) build instructions and troubleshooting.

Each guide contains step‑by‑step commands and common fixes for that platform.

## Running
Simple GUI run (open without a provided file):

```bash
./roift_gui
```

Open directly with an input image (example used during testing):

```bash
./roift_gui --input example_image.nii.gz
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