
## High level architecture
 - `ManualSeedSelector` (src/ManualSeedSelector.*)
   - Main window and the primary UI. Holds the loaded `NiftiImage`, the editable mask buffer, the current seed list and three `OrthogonalView` widgets (axial/sagittal/coronal).
   - Responsibilities / public API:
     - getSeeds() — return current seeds (label, x,y,z)
     - getImagePath() — path of the loaded image
     - applyMaskFromPath(path) — load a mask and refresh views
   - Notes: this class orchestrates the UI, keeps an undo/backup of the image (calls `NiftiImage::deepCopy()`), and connects dialogs to actions.

 - `SegmentationRunner` (src/SegmentationRunner.*)
   - Presents a dialog to configure ROIFT parameters (polarity, niter, percentile) and runs external ROIFT (`oiftrelax`) per-label.
   - Supports a "segment all" batch mode: it writes per-label seed files, launches one ROIFT process per label (up to a concurrency cap), collects outputs, and merges them into a multilabel NIfTI (ITK-backed when available).
   - Uses `QProcess` for external processes and a simple scheduler to control concurrency. To change the parallel cap search for `QThread::idealThreadCount()` or the hard-coded cap in the file.

- `NiftiImage` (src/NiftiImage.*)
  - A small wrapper for reading NIfTI images (ITK-backed when available). Provides helper functions to get axial/sagittal/coronal slices as RGB buffers used by `OrthogonalView`.

 - `OrthogonalView` (src/OrthogonalView.*)
   - Custom Qt widget that renders a `QImage` slice, supports panning/zoom, mouse events, and accepts an overlay callback for drawing seeds, crosshairs, or mask previews.

- Dialogs
  - `SeedOptionsDialog` and `MaskOptionsDialog` are small UI dialogs that control seed drawing mode, brush radius, mask save/load, and other options.