
## High level architecture
 - `ManualSeedSelector` (src/ManualSeedSelector.*)
   - Main window and the primary UI. Holds the loaded `NiftiImage`, the editable mask buffer, the current seed list and three `OrthogonalView` widgets (axial/sagittal/coronal).
   - Responsibilities / public API:
     - getSeeds() — return current seeds (label, x,y,z)
     - getImagePath() — path of the loaded image
     - applyMaskFromPath(path) — load a mask and refresh views
   - Notes: this class orchestrates the UI, keeps an undo/backup of the image (calls `NiftiImage::deepCopy()`), and connects dialogs to actions.
   - Image loading: every entry point (a list row, `addImagesFromPaths()`, the constructor's path) goes through `selectImageRow()`, which reads the file with the static `readImageData()` and hands the result to `applyLoadedImage()`, the one place that swaps `m_image`, clears the masks and seeds of the previous image, ranges the sliders with their signals held, sets the window controls and composes the views once. With `setBackgroundImageLoading(true)`, which `main()` sets after the command-line images are in, `readImageData()` runs on a worker thread (`beginBackgroundImageLoad()`); the result is posted back to the GUI thread (`finishBackgroundImageLoad()`), which drops it when the entry was removed meanwhile and otherwise applies it, then loads whatever row the list points at if the user moved on. The previous image stays on screen, and usable, until then; a read cannot be interrupted, so the destructor joins the worker. Off, the default, a selection is read inline and has loaded on return, which the tests and the `--mask`/`--seeds` arguments rely on; `waitForImageLoad()` spins the event loop until a background read is done.
   - Mask layers: which mask is *edited* (`m_maskData`, chosen by a row click) and which masks are *drawn* (`MaskLayer::visible`, set only by the eye) are independent. Selection is lazy — `selectActiveMask()` takes the voxels from a layer that already has them and otherwise records the path in `m_pendingActiveMaskPath`, and `ensureActiveMaskLoaded()` does the read at the first operation that needs voxels (show, paint, save, threshold, vessel graph). Anything new that touches `m_maskData` has to call it first, or it will act on a blank buffer. `m_maskLayers` holds one entry per drawn mask plus one for the edited mask whether or not it is drawn, since that entry carries its colour rule; the edited mask's entry holds no voxels of its own, so nothing is stored twice. `visibleMaskRenderItems()` resolves the layers into what the 2D blend and the 3D merge walk, with the edited mask last so it is on top.
   - Exported images: the native binaries and the Python tools read files and none of them reads NumPy or raster images, so `nativeImagePath()` exports such an image to a NIfTI in a `QTemporaryDir` owned by the window, in a fresh numbered subdirectory per export (a queued run may still be reading an earlier one), and reuses it until the next load. The tools place their outputs, or open their save dialogs, beside `getImagePath()` rather than the export; LUNAS, however, names its case after the exported file, and the rib runner still names its outputs after the export and looks for the lung labelmap beside it (see `docs/usage.md`).
   - One-slice images and slice runs: `isPlanarImage()` (Z = 1) drives `applySliceLayout()`, which shows the axial panel alone, and `updateSegmentationScopeControls()`, which fixes the scope to the current axial slice. `segmentCurrentSlice()` builds a `planar::RunRequest` on the GUI thread (seeds on the slice, pixels with the window applied, the CPU `oiftrelax`) and queues it on the segmentation worker; `applyPlaneSegmentationResult()` and `pastePlaneResult()` paste the labels into `m_maskData` only when the image and the path of the edited mask are still those the run started on (`PlaneRunPins`), so painting the same mask during a run does not discard the result. See `docs/usage.md`.

 - `MaskLayers` (src/MaskLayers.*)
   - The mask volume model, free of the window: `MaskVolume` (label buffer + grid), `readMaskVolume()` (one reader for ITK formats and NumPy), and `MaskLayer` — a drawn mask plus the rule (`MaskColorMode`) that turns its labels into colours.

 - `MaskListDelegate` (src/MaskListDelegate.*)
   - Paints the mask list row: eye, colour swatch, name. The eye's hit target (`eyeRect()`) is shared with the viewport event filter in `ManualSeedSelector::eventFilter`, which turns a click there into a visibility toggle instead of a selection.

 - `SegmentationRunner` (src/SegmentationRunner.*)
   - Presents a dialog to configure ROIFT parameters (polarity, niter, percentile) and runs external ROIFT (`oiftrelax`) per-label.
   - Supports a "segment all" batch mode: it writes per-label seed files, launches one ROIFT process per label (up to a concurrency cap), collects outputs, and merges them into a multilabel NIfTI (ITK-backed when available).
   - Uses `QProcess` for external processes and a simple scheduler to control concurrency. To change the parallel cap search for `QThread::idealThreadCount()` or the hard-coded cap in the file.
   - `resolveCpuRoiftExecutable()` finds the standard CPU `oiftrelax` by name for slice runs, and refuses a `ROIFT_EXECUTABLE` that names another binary.

 - `SeedFiles` (src/SeedFiles.*)
   - The seed-file writers shared by volume and slice runs: `writeMultilabelSeedFile()`, `writeLegacySeedFile()`, and `dedupeSeedsKeepingLatest()`, which keeps the last seed placed on a voxel.

 - `PlanarSlice` (src/PlanarSlice.*)
   - Plane geometry free of widgets, oriented as the slice views draw it (axial `z = k` is `(x, y)`, sagittal `x = k` is `(y, z)`, coronal `y = k` is `(x, z)`): `makeGeometry()`, `toPlane()` for a voxel, `extractPlane()` for the pixels, `borderPixels()` for the four edges, and `pastePlaneLabels()`, the paste policy (a positive result wins; elsewhere only the labels seeded in the run are cleared). Pinned by the `planar_slice` test.

 - `PlaneSegmentation` (src/PlaneSegmentation.*)
   - `segmentPlane()` writes the plane as a (width, height, 1) int32 NIfTI and the seeds beside it, adding the border seeds when asked, runs `oiftrelax` with boundary stride 0, and reads the label plane back. A plane whose values are not integers, or span more than 65535, is mapped onto 0 to 10000 first, because `gft` rescales float input from 0 and allocates a bucket per intensity level. It polls a cancel callback while the binary runs and kills the process on request. Pinned by the `plane_segmentation` test.
   - The GUI cuts the plane itself rather than passing a plane option to `oiftrelax`: it already holds DICOM, NumPy and raster images in memory, which the binary cannot read, and a one-slice file is cheap to write.
   - It writes its own border seeds rather than using the binary's face seeding: every pixel of all four edges is seeded (the binary's stride lattice starts at 0 and reaches the far edges only when the size fits the stride), the result does not depend on the binary's version, and the user can switch the border off.

- `ExternalProcessRunner` (src/ExternalProcessRunner.cpp)
  - `ManualSeedSelector` members that shell out to project Python scripts: LUNAS and rib seed
    generation, super-resolution, mask post-processing, and `runVesselGraph()` (Morse
    centreline of the current mask, rooted at the last seed — see `docs/usage.md`).

- `SolverNetwork` (src/SolverNetwork.*) and `SolverNetworkUi` (src/SolverNetworkUi.cpp)
  - `SolverNetwork` is free of widgets: `parseSolverNetwork()` reads the YAML subset the solver
    files use; `parseSolverGeometry()` the geometry JSON; `solverGeometryProblems()` lists every
    way a geometry fails to describe a YAML on an image (hash, labels, nodes, grid), and a
    non-empty list means nothing is placed. `SegmentVoxelIndex` maps a voxel of a segment map to
    its segment, storing only the vessel's voxels.
  - `SolverNetworkUi.cpp` holds the `ManualSeedSelector` members of the `Tools > Solver Network...`
    window. The window is built once, hidden, when the main window is, so the network's state
    outlives closing it; `unloadSolverNetwork()` removes its maps through `forgetMaskPath()`, the
    same path the mask list's remove button takes. Both maps are
    ordinary mask layers, so drawing and colours need nothing new. Selection and hover never
    re-contour the surface: the graph is two actors built once per load
    (`Mask3DView::setNetworkGraph()`), the selection and hover swap small highlight actors
    (`setNetworkHighlight()`), and names are at most two text actors (`setAnnotations()`). All
    of these live in `Mask3DView`'s overlay renderer (layer 1, same camera), which draws after
    the translucent surface. 3D hover is a ray cast against a `vtkStaticCellLocator` built once
    per surface, throttled to one every 40 ms and skipped while a button is down.
  - `Mask3DView::setMaskData()` contours one surface per label while a mask has up to 32
    labels, so touching labels keep their shared wall. Past that it contours the union once
    and colours each vertex by the label of the voxel under it (`paintSurfaceLabels()`):
    discrete flying edges passes over the whole volume once per label, and a 1153-segment map
    took 25 s against 0.35 s now. `colorForLabel()` cycles its 252 distinct colours past label
    255 instead of clamping, which had painted every higher label one colour.

- `NiftiImage` (src/NiftiImage.*)
  - A small wrapper for reading NIfTI images (ITK-backed when available). Provides helper functions to get axial/sagittal/coronal slices as RGB buffers used by `OrthogonalView`.
  - `load()` reads a `.nii.gz` in place: ITK's NIfTI IO inflates it itself, so there is no decompressed copy in the temp directory (which cost the volume's size again, in memory where `/tmp` is a tmpfs, and stayed behind after a failed read). Only a successful read replaces the loaded image, so a failed open leaves the previous image and its spacing intact, and `lastError()` carries the reason for NIfTI files too. `finalizeLoad()` takes the range over the raw buffer, split across up to eight threads for large volumes, and leaves NaN and infinities out of it. The slice getters walk the buffer directly (X fastest, one strided run per row) rather than calling `GetPixel` per voxel, and an index past the volume gives a black slice instead of a read past the buffer. Pinned by the `nifti_volume` test.
  - `loadRaster()` reads PNG, JPEG, BMP and TIFF through ITK's factory reader as one-slice volumes (a multi-page TIFF as a volume), reduces colour to luminance with alpha ignored, sets the spacing to 1, and refuses what ITK would read wrongly or what is too large (integer TIFF samples of 32 bits or wider, more than 400 million samples, more than four channels). `lastError()` carries the reason to the status bar, since a Windows GUI build has no stderr. `readMaskVolume()` in `MaskLayers` reads raster masks by palette index and accepts one channel only.

 - `OrthogonalView` (src/OrthogonalView.*)
   - Custom Qt widget that renders a `QImage` slice, supports panning/zoom, mouse events, and accepts an overlay callback for drawing seeds, crosshairs, or mask previews.
   - Slice updates: `ManualSeedSelector::updateViews(planes)` recomposes only the views named in its `PlaneMask`, and a slice slider asks for its own plane alone (the other two only when a 3D locate marker has to be dropped). The sliders and the brush go through `requestViewUpdate()`, which draws at once when the views have been still for one 16 ms interval and otherwise coalesces into one draw when the interval ends; so a single step is never delayed, and a pointer moving at 1 kHz cannot queue more recompositions than frames. Direct `updateViews()` calls bypass the hold. Before this a drag on a 704 x 704 x 640 volume recomposed all three views (3 ms) per pointer event and reached 20 repaints per second.

- Dialogs
  - `SeedOptionsDialog` and `MaskOptionsDialog` are small UI dialogs that control seed drawing mode, brush radius, mask save/load, and other options.