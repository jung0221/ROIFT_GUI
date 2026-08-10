
## High level architecture
 - `ManualSeedSelector` (src/ManualSeedSelector.*)
   - Main window and the primary UI. Holds the loaded `NiftiImage`, the editable mask buffer, the current seed list and three `OrthogonalView` widgets (axial/sagittal/coronal).
   - Responsibilities / public API:
     - getSeeds() — return current seeds (label, x,y,z)
     - getImagePath() — path of the loaded image
     - applyMaskFromPath(path) — load a mask and refresh views
   - Notes: this class orchestrates the UI, keeps an undo/backup of the image (calls `NiftiImage::deepCopy()`), and connects dialogs to actions.
   - Mask layers: which mask is *edited* (`m_maskData`, chosen by a row click) and which masks are *drawn* (`MaskLayer::visible`, set only by the eye) are independent. Selection is lazy — `selectActiveMask()` takes the voxels from a layer that already has them and otherwise records the path in `m_pendingActiveMaskPath`, and `ensureActiveMaskLoaded()` does the read at the first operation that needs voxels (show, paint, save, threshold, vessel graph). Anything new that touches `m_maskData` has to call it first, or it will act on a blank buffer. `m_maskLayers` holds one entry per drawn mask plus one for the edited mask whether or not it is drawn, since that entry carries its colour rule; the edited mask's entry holds no voxels of its own, so nothing is stored twice. `visibleMaskRenderItems()` resolves the layers into what the 2D blend and the 3D merge walk, with the edited mask last so it is on top.

 - `MaskLayers` (src/MaskLayers.*)
   - The mask volume model, free of the window: `MaskVolume` (label buffer + grid), `readMaskVolume()` (one reader for ITK formats and NumPy), and `MaskLayer` — a drawn mask plus the rule (`MaskColorMode`) that turns its labels into colours.

 - `MaskListDelegate` (src/MaskListDelegate.*)
   - Paints the mask list row: eye, colour swatch, name. The eye's hit target (`eyeRect()`) is shared with the viewport event filter in `ManualSeedSelector::eventFilter`, which turns a click there into a visibility toggle instead of a selection.

 - `SegmentationRunner` (src/SegmentationRunner.*)
   - Presents a dialog to configure ROIFT parameters (polarity, niter, percentile) and runs external ROIFT (`oiftrelax`) per-label.
   - Supports a "segment all" batch mode: it writes per-label seed files, launches one ROIFT process per label (up to a concurrency cap), collects outputs, and merges them into a multilabel NIfTI (ITK-backed when available).
   - Uses `QProcess` for external processes and a simple scheduler to control concurrency. To change the parallel cap search for `QThread::idealThreadCount()` or the hard-coded cap in the file.

- `ExternalProcessRunner` (src/ExternalProcessRunner.cpp)
  - `ManualSeedSelector` members that shell out to project Python scripts: LUNAS and rib seed
    generation, super-resolution, mask post-processing, and `runVesselGraph()` (Morse
    centreline of the current mask, rooted at the last seed — see `docs/usage.md`).

- `NiftiImage` (src/NiftiImage.*)
  - A small wrapper for reading NIfTI images (ITK-backed when available). Provides helper functions to get axial/sagittal/coronal slices as RGB buffers used by `OrthogonalView`.

 - `OrthogonalView` (src/OrthogonalView.*)
   - Custom Qt widget that renders a `QImage` slice, supports panning/zoom, mouse events, and accepts an overlay callback for drawing seeds, crosshairs, or mask previews.

- Dialogs
  - `SeedOptionsDialog` and `MaskOptionsDialog` are small UI dialogs that control seed drawing mode, brush radius, mask save/load, and other options.