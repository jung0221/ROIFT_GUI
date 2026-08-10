## Keyboard shortcuts (slice navigation)
- W: axial + (next axial slice)
- S: axial -
- D: sagittal +
- A: sagittal -
- E: coronal +
- Q: coronal -
- [ and ]: decrement/increment all three slices together

## Mouse
- Left-click in a view to add seeds (when seed mode is draw)
- Hold left-drag to draw mask strokes when in mask mode
- Right-click to erase (or use mask dialog's erase mode)

## Locate a 3D surface point on the slices
- Shift+click the mask surface in the 3D panel: the axial, sagittal and coronal views
  all jump to the voxel under the cursor, and the status bar reports its `x/y/z`.
- A cyan `X` marks the point in-plane in all three views. Navigating any single
  plane (slider, W/S/A/D/Q/E, slice drag) clears the `X` from all three at once,
  since the point is only meaningful while every slice still cuts through it.
  `Esc` also dismisses it.
- Needs `Show 3D` enabled — the pick ray only tests the mask surface, not the seed glyphs.
- Shift is reserved for this gesture, so it never pans the 3D camera.

## Showing several masks at once
- Every row in `Masks` has an eye at its left edge. It starts closed: the mask is
  listed but not drawn.
- **Click the name** to load a mask for editing, as before. It is drawn straight away
  (the eye shows open but dimmed) and it is *transient* — clicking another row
  replaces it. Only this mask can be painted, thresholded, cleaned or saved.
- **Click the eye** to pin a mask. A pinned mask (accent-coloured eye) stays drawn no
  matter which mask you load next, so two or more masks can be compared in the slices
  and in the 3D view at once. Click the eye again to hide it and free its memory.
- Pinning the mask you are editing keeps it on screen when you move on to the next one.
- Colours are picked so masks stay apart: a mask with a single label gets one colour
  from a per-mask palette (shown as the swatch beside the eye), and a multi-label mask
  (ribs, TotalSegmentator) keeps the shared per-label palette. Right-click a row to force
  either rule or to set the mask's colour by hand.
- Drawn masks share the overlay opacity and the per-view `Show Mask` toggles. The
  `Mask Labels` filter applies to the mask being edited.
- Masks must sit on the same X/Y grid as the image; one that does not is refused with a
  message instead of being drawn misaligned. Switching image drops every pinned mask,
  since the grid changes.
- Each drawn mask is a full label volume in memory, so pinning several large masks is
  answered with a size warning before it happens.

## Mask I/O
- `Mask Options` dialog exposes load/save. When built with ITK the app saves masks as NIfTI using int16 as the pixel type.
- Segmentation outputs from `SegmentationRunner` are merged using ITK when available and then loaded into the GUI as the current mask.

## Opening images
- The sidebar panel is `Images` and the toolbar action is `Open` (Ctrl+O). Both take any
  supported volume, not just NIfTI: `.nii`, `.nii.gz`, DICOM (`.dcm`, `.dicom`, `.ima`) and
  NumPy (`.npz`, `.npy`). Masks accept everything except DICOM, which carries no labels.
- The same list drives `Open CSV`/`Add CSV`, so a CSV column may list `.npz` paths, and the
  mask/seed folder scan, so a `.npz` mask beside an image is picked up like a `.nii.gz` one.
- `Export CSV` writes the column header `image_path` (it used to be `nifti_path`). The
  importer accepts both, along with `path`, `file_path` and `filepath`.
- Saving is still NIfTI only — `Save` and `Save Mask` write `.nii`/`.nii.gz`.

## NumPy volumes (`.npz` / `.npy`)
- `Open` and `Open Mask` both accept `.npz` and `.npy`. Compressed and
  uncompressed archives, ZIP64, and `bool`/`int8..64`/`uint8..64`/`float16`/`float32`/`float64`
  arrays are all read; `float16` matters because that is what nnUNet writes for probabilities.
- **A numpy container stores samples and nothing else** — no spacing, no origin, no
  orientation, no axis convention. ROIFT_GUI recovers those instead of assuming them,
  because a wrong guess mirrors axes or reports millimetres that were never measured.
- Spacing/origin/orientation are taken from the first source that exists:
  1. spacing typed into the import dialog,
  2. a matching volume next to the array — `<name>.nii.gz`, `<name>.nii` or `<name>_0000.nii.gz`
     — used only when its dimensions match the array,
  3. a `<name>.json` sidecar with `"spacing": [x, y, z]` (millimetres, image order) and an
     optional `"axis_order": "zyx" | "xyz"`,
  4. otherwise 1 mm isotropic, reported in the dialog and on stderr as unverified.
- **Axis order.** The letters say what the array axes *are*, in order, so `ZYX` means the array
  is indexed `[z][y][x]`. All six permutations are offered, because producers disagree:
  `ZYX` = `SimpleITK.GetArrayFromImage` / nnUNet, `XYZ` = `nibabel.get_fdata`,
  `YXZ` = rows, columns, slices — the layout you get from stacking DICOM slices, which is
  common in public CT dumps. Getting it wrong transposes the volume: the axial panel shows a
  coronal slice, or the sagittal and coronal panels swap.
  `Automatic` picks the permutation matching the reference volume; with no reference it assumes
  the odd-length axis is the slice axis, which is a guess — check the preview.
- **Preview.** The dialog renders the middle axial slice under the current settings from a
  subsampled copy of the array, so changing the axis order re-renders instantly. Since a numpy
  file records no convention, looking at the anatomy is the only reliable confirmation: in a
  correct axial view the spine sits at the bottom and the body is wider left-right than
  front-back.
- **Mirror.** A numpy file records no handedness either. Tick `X`, `Y` or `Z` to reverse an
  axis if the preview is flipped — note a wrong `X` silently swaps the patient's left and right.
- 4D arrays are channelled volumes: `(C, Z, Y, X)` under `ZYX`, `(X, Y, Z, C)` under `XYZ`.
  Pick the channel in the dialog — e.g. one class of an nnUNet `probabilities` array. Only the
  selected channel is read when the layout allows it, so a multi-gigabyte softmax is not
  materialised in full.
- The dialog only appears when a real choice is open: several arrays, several channels, or no
  geometry found. An unambiguous single-array file with a matching `.nii.gz` opens directly.
  The choice is made once per file and reused whenever the entry is reselected.
- The array is auto-picked by name in this order: `probabilities`, `softmax`, `data`, `arr_0`,
  `image`, `volume`, `ct`, `seg`, `label`, then the first usable 3D array.

## NIfTI Auto-Detection Toggle
- In the `NIfTI Images` panel, use `Auto-detect masks/seeds` to enable or disable automatic scanning of the image folder for:
- mask files (`.nii`, `.nii.gz`) associated with the current image
- seed files (`.txt`)
- The `Refresh` buttons still trigger a manual rescan.

## Mask Heatmap
- In the `Mask` top tab, use `Advanced -> Heatmap`.
- When enabled, ROIFT_GUI aggregates all masks listed in `Masks` for the selected image and displays a combined RGB heatmap overlay.
- Heatmap intensity is normalized by the number of masks that were successfully loaded and matched to image dimensions.

## Right-Click Point Query
- Right-click on any pixel in axial/sagittal/coronal viewer to open a context menu.
- Select `Show masks lists on this point` to open a dialog listing all masks that contain that voxel.

## Vessel Graph (Morse centreline)
- Sidebar section `Vessel Graph`, or Ctrl+Shift+G.
- Always needs one seed placed on the structure — the last object seed is the root.
- `Domain = Current mask` graphs the mask already loaded or drawn; the mask label under the
  seed becomes the domain, so a multilabel mask graphs only the structure you clicked on.
- `Domain = Segment from CT` needs no mask: it generates 3D vessel seeds and runs `oiftrelax`
  first, then graphs the component the seed landed in. Expect **minutes** (~150 s for a
  512×512×173 volume), and it also writes `<image>_vessel_roift.nii.gz`, the segmentation
  before the component is picked.
- `Min branch` is a persistence threshold in millimetres: branches shorter than it are pruned.
  `Centring p` weights the geodesic toward the centre of the tube (0 = plain euclidean).
- Writes `<image>_vessel_graph.nii.gz` next to the image, adds it to the mask list and loads it
  as the overlay; voxels not connected to the root stay 0 and the count is logged.
- Runs `src/vessels/cli/vessel_graph.py`, so it needs the project Python (`ROIFT_PYTHON`).

## Example workflows
- Place seeds for two labels, open the Segmentation dialog, choose "Segment all", and select an output directory; the per-label outputs will be merged into a multilabel NIfTI and loaded automatically.
- Save seeds to a `.txt` file from the Segmentation dialog to reproduce or share seed sets.
