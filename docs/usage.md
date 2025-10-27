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

## Example workflows
- Place seeds for two labels, open the Segmentation dialog, choose "Segment all", and select an output directory; the per-label outputs will be merged into a multilabel NIfTI and loaded automatically.
- Save seeds to a `.txt` file from the Segmentation dialog to reproduce or share seed sets.
