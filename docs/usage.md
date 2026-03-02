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

## Example workflows
- Place seeds for two labels, open the Segmentation dialog, choose "Segment all", and select an output directory; the per-label outputs will be merged into a multilabel NIfTI and loaded automatically.
- Save seeds to a `.txt` file from the Segmentation dialog to reproduce or share seed sets.
