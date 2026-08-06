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

## Mask I/O
- `Mask Options` dialog exposes load/save. When built with ITK the app saves masks as NIfTI using int16 as the pixel type.
- Segmentation outputs from `SegmentationRunner` are merged using ITK when available and then loaded into the GUI as the current mask.

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
