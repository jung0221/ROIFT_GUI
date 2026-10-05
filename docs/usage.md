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
- **The eye decides what is drawn, and nothing else does.** Every row in `Masks` has one
  at its left edge, closed by default. Every mask with an open eye is drawn — one, two or
  a dozen, in the slices and in the 3D view alike.
- **Clicking a name only chooses which mask you edit.** That mask becomes the one painted,
  thresholded, cleaned or saved, and the row is selected, but it does not appear on screen
  until you open its eye. So you can edit one mask while looking at another.
- Selecting is instant: since nothing is drawn by it, the file is not read either. The
  voxels are fetched by the first thing that actually needs them — opening its eye,
  picking up the mask brush, saving, thresholding — with a busy cursor while that happens.
  Selecting a mask that is already on screen costs nothing at all: it takes the voxels
  from the layer instead of re-reading the file.
- Click an open eye to hide that mask again and free the memory it held.
- Some actions open an eye for you, because no eye was clicked and an empty viewer would
  misrepresent their result: a mask loaded by `Open Mask` or by the `--mask` argument; the
  result of a slice segmentation, of `Postprocess Mask` and of the vessel graph; the map a
  placed solver network draws; the first mask added while no image is open; and the mask you
  are editing when you pick up the mask brush or threshold it. A background volume run only
  adds its files to the mask list, with their eyes closed.
- A mask painted from scratch has no row and therefore no eye, so it is always drawn.
- Colours are picked so masks stay apart: a mask with a single label gets one colour
  from a per-mask palette (shown as the swatch beside the eye), and a multi-label mask
  (ribs, TotalSegmentator) keeps the shared per-label palette. Right-click a row to force
  either rule or to set the mask's colour by hand.
- Drawn masks share the overlay opacity and the per-view `Show Mask` toggles. The
  `Mask Labels` filter applies to the mask being edited.
- Masks must sit on the same X/Y grid as the image; one that does not is refused with a
  message instead of being drawn misaligned. Switching image closes every eye, since the
  grid changes under them.
- Each drawn mask is a full label volume in memory, so opening several large masks is
  answered with a size warning before it happens.

## Mask I/O
- The `Save` and `Load` buttons in the `File` group of the `Mask` section open `Save Mask`
  and `Open Mask`. NIfTI is the default format and is written with int16 samples; a label
  outside -32768 to 32767 is clamped to that range without a warning. A name that does not
  end in `.nii` or `.nii.gz` gets `.nii.gz` appended. The test is case-sensitive, so
  `mask.NII.GZ` is saved as `mask.NII.GZ.nii.gz`.
- **PNG label images.** While the image has one slice, `Save Mask` also offers
  `PNG label image (*.png)`. The file is 8-bit when every label is at most 255 and 16-bit
  when the largest label is at most 65535. A label above 65535, a negative label, or a mask
  with more than one slice is refused with a message, and nothing is written.
- **Which writer a name selects.** A name ending in `.png`, in any letter case, goes to the
  PNG writer whichever filter is chosen, so for a mask with more than one slice it is
  refused. With the PNG filter chosen, a name typed without a suffix gets `.png`, but a name
  with any other suffix is saved as NIfTI under the rule above.
- `Open Mask` reads PNG, BMP and TIFF label images as well as NIfTI and NumPy; see
  [Raster images](#raster-images-png-jpeg-bmp-tiff) for how their labels are read.
- A volume run started with `Run` works in the background. When it ends, the files it wrote
  (the single output, each polarity of a sweep, or each label of `Batch per label` together
  with the merged `segmentation_multilabel.nii.gz`) are added to the image's mask list. They
  are not loaded as the edited mask, and each is drawn only once its eye is opened. The
  outputs of a run on an exported image are listed among the unassigned masks instead; see
  [Raster images](#raster-images-png-jpeg-bmp-tiff).

## Opening images
- The sidebar panel is `Images` and the toolbar action is `Open` (Ctrl+O). Both take any
  supported image, not just NIfTI: `.nii`, `.nii.gz`, DICOM (`.dcm`, `.dicom`, `.ima`),
  NumPy (`.npz`, `.npy`) and the raster formats `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif` and
  `.tiff`, in any letter case. The mask dialogs offer NIfTI, NumPy, PNG, BMP and TIFF. DICOM
  carries no labels. JPEG is left out of the filter because lossy compression gives label
  edges values that no label has, but a one-channel JPEG chosen through `All files` still
  opens as a mask.
- The same list of image formats drives `Open CSV`/`Add CSV`, so a CSV column may list `.npz`
  or `.png` paths. The mask/seed folder scan uses a narrower list: a `.npz` mask beside an
  image is picked up like a `.nii.gz` one, but a raster file never is (see below).
- `Export CSV` writes the column header `image_path` (it used to be `nifti_path`). The
  importer accepts both, along with `path`, `file_path` and `filepath`.
- `Save` writes the image as NIfTI only. `Save Mask` writes NIfTI, or a PNG label image while
  the image has one slice; see [Mask I/O](#mask-io).

## Raster images (PNG, JPEG, BMP, TIFF)
- **One file, one slice.** A raster file opens through ITK's readers as a volume with one
  slice (Z = 1). A multi-page TIFF opens as a volume whose pages are its slices.
- **One panel.** An image with one slice, whatever its format, is shown in a single panel
  labelled `Image`. The sagittal, coronal and 3D panels and the slice slider row are hidden,
  since one slice has no other plane and no surface to contour. They return when a volume is
  selected.
- **Colour.** The loader reduces colour to luminance, 0.2125 R + 0.7154 G + 0.0721 B, and
  ignores alpha: an RGBA file reads as the luminance of its RGB, a grey + alpha file as its
  grey. A file with any other number of channels is refused.
- **Pixels, not millimetres.** A raster image has spacing 1 x 1 x 1 whatever its DPI tags
  say. DPI describes the printed size, not the imaged object, and the same picture would
  otherwise measure differently in each format. The ruler, which labels its lengths in
  millimetres, therefore reports pixels for a raster image.
- **Stored orientation.** The EXIF orientation tag of a JPEG is ignored, so a photograph
  appears as its pixels are stored, which may be rotated relative to a photo viewer.
- **Images classified as masks.** As with NIfTI, an image with integer samples whose values
  span at most 1.5 is classified as a mask and drawn as one. That covers a 0/1 image, but
  also `{254, 255}` and a uniform image, so a blank white PNG is shown as a mask. A colour
  raster is judged by the integer sample type of the file, not by its luminance values. The
  classification changes how the image is drawn and sets its window range to 0 to 1; the
  pixel values are unchanged.
- **Refusals.** A file that cannot be opened is refused, and the status bar gives the reason
  as `Could not read <file>: <reason>`. The reasons are integer TIFF samples of 32 bits or
  wider, which ITK cannot read (save the file with 8-bit, 16-bit or floating-point samples);
  more than 400 million samples (pixels times channels); a channel count outside one to four;
  and any error from ITK's reader, of which the first line is shown.
- **What the external tools receive.** `oiftrelax` in a volume run, LUNAS, the rib runner,
  super-resolution and the vessel graph read files, and none of them reads PNG or TIFF. The
  GUI therefore exports the image to a temporary NIfTI (`roift_src_<name>.nii.gz`;
  `roift_npz_<name>.nii.gz` for a NumPy image) and hands them that path. The exports live in a
  temporary directory that belongs to the window, with a fresh subdirectory for every export,
  so an export that a queued run is still reading is never overwritten. The directory is
  removed when the window closes. An export is reused while the same image stays loaded.
- **Where outputs go.** LUNAS writes into `<folder>/<case>/`, where `<folder>` is the folder
  of the original image, or its parent when that folder is named after the image, and
  `<case>` is named after the file LUNAS reads: `roift_src_<name>` (or `roift_npz_<name>`) for
  an exported image. The vessel graph writes beside the original image. Super-resolution and
  a volume run of `oiftrelax` only open their save dialogs in the folder of the original
  image. The rib runner works beside the export (see below).
- **Known limitations**, shared with NumPy images and older than raster support: the rib
  runner looks for the lung labelmap (`lungs_<case>.nii.gz` or `lung_<case>.nii.gz`) beside
  the file it is handed, which is the export, so it stops with `Lung labelmap not found`. The
  masks a volume run of `oiftrelax` produces from an exported image are listed among the
  unassigned masks rather than under the image, because they are registered against the
  export's path.
- **Folder scan.** Raster files are never picked up by the mask/seed folder scan. The scan
  lists every candidate beside the image, so a folder of photographs would list each one as a
  mask of the others. Open a raster mask explicitly with `Open Mask`, or with `Add` in
  `Masks`.
- **Raster masks.** PNG, BMP and TIFF label images open as masks. JPEG is not in the filter,
  but a one-channel JPEG chosen through `All files` opens as one, with whatever values its
  compression left at the label edges. The labels are the stored sample values, and for an indexed PNG they are the palette indices,
  not the colours those indices map to. A label image must have one channel: a colour or
  grey + alpha file is refused with a message giving its channel count. A TIFF with integer
  samples of 32 bits or wider is refused as well. A raster mask has spacing 1, as its image
  does, and must sit on the image's X/Y grid like any other mask.
- `--input` on the command line opens raster files as well.

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
- In the `Images` panel, use `Auto-detect masks/seeds` to enable or disable automatic scanning of the image folder for:
- mask files (`.nii`, `.nii.gz`, `.npz`, `.npy`; never raster files) associated with the current image
- seed files (`.txt`)
- The `Refresh` buttons still trigger a manual rescan.

## Segmenting one slice
- **Purpose.** A slice run segments the slice on screen instead of the whole volume. It is
  the only run a one-slice image has, and on a volume it changes one slice of the edited mask
  and leaves the others untouched.
- **Controls.** The `Parameters` group of the `Segmentation` section has `Scope` (`Volume`,
  `Current slice`), `Plane` (`Axial`, `Sagittal`, `Coronal`) and
  `Background on the plane border`, which is on by default. `Plane` and the border option are
  enabled only in `Current slice` scope. For a one-slice image the scope is `Current slice` and
  the plane `Axial`, both fixed; the choices made for volumes return when a volume is
  selected.
- **Which slice.** `Run` (Ctrl+Shift+S) segments the slice at the current position of the
  chosen plane's slider: z for axial, x for sagittal, y for coronal. The plane is oriented as
  its view draws it: axial `z = k` gives a plane of `(x, y)`, sagittal `x = k` a plane of
  `(y, z)`, and coronal `y = k` a plane of `(x, z)`.
- **Seeds.** Only the seeds on that slice take part, and the log reports how many were used
  and how many on other slices were ignored. Where several seeds share a pixel, the last one
  placed wins. At least one object seed (label > 0) must lie on the slice; label 0 seeds are
  background.
- **Parameters.** `Polarity`, `Relax`, `Pctile` and `Smoothing` apply as in a volume run. When
  the window has been moved off the full intensity range, the slice is clamped to the window
  first, as a volume run does, and the log says so.
- **Volume-only controls.** In slice scope `Batch per label`, `Polarity sweep`, `Use GPU` and
  `Method` are disabled. `Batch per label` and `Polarity sweep` write several output files
  chosen through dialogs, whereas a slice result goes into the edited mask. `Use GPU` and
  `Method` select binaries that cannot segment one slice: a slice run always uses the
  standard transform, so `Smoothing` stays visible and `Alpha` and `Sigma` are hidden. `Mode` is forced to `Multi-label`, and
  `Legacy binary` is unavailable. A box ticked before the switch keeps its tick but has no
  effect on a slice run. All of these return to their previous state when the scope goes
  back to `Volume`.
- **Background on the plane border.** When it is on, every pixel on the four edges of the
  plane that holds no user seed becomes a background seed. The binary's own face seeding is
  off in a slice run either way. Switch the option off when the structure touches the edge
  of the plane, which is common on coronal and sagittal CT slices and on cropped
  photographs, and place background seeds by hand instead. With the option off and no
  background seed at all, the whole plane is divided among the object labels.
- **The paste.** The result is written into the edited mask, on that slice only. A pixel takes
  the result where the result is positive. Elsewhere, a pixel holding a label seeded in this
  run is cleared and any other label is kept. Nothing outside the slice changes. If no mask
  is being edited, a blank one is created. The edited mask is shown after every successful
  paste. Nothing is written to disk until `Save Mask`. There is no undo: to revert, run again
  or repaint.
- **In the background.** The seeds, parameters and pixels are taken when `Run` is clicked.
  The run then happens in the background, queues behind other runs, and is cancelled when the
  window closes. If a different image, or a different mask for editing, is selected before
  the result arrives, the result is discarded. Painting the edited mask during the run does
  not discard it; the paste is then applied over the painted pixels of that slice.
- **Failure modes.** Each of the following leaves the mask unchanged and gives the reason in
  the status bar and the segmentation log:
  - no image is open;
  - the plane is narrower than 3 pixels in either direction, which the border seeds would
    cover entirely;
  - no object seed lies on the slice;
  - the slice holds a pixel that is not a finite number, such as a NaN in a floating-point
    NIfTI;
  - the edited mask is on a different grid from the image, which is checked when `Run` is
    clicked if the mask has been read, and otherwise when the result arrives;
  - no standard CPU `oiftrelax` is found. It is searched for in `ROIFT_EXECUTABLE`, then on
    `PATH`, then in the build folders around the application and the current directory; set
    `ROIFT_EXECUTABLE` to its full path;
  - `ROIFT_EXECUTABLE` names another binary. The GPU and experiment binaries cannot segment
    one slice, so point it at `oiftrelax` or unset it;
  - a different image, or a different mask for editing, was selected during the run, so the
    result is discarded;
  - the edited mask, selected but not yet read, could not be read when the result arrived;
  - the temporary directory for the run could not be created;
  - `oiftrelax` fails. The first line of the reason is shown in the status bar; the command
    line and the last lines of the binary's output go to the log.
- **Binary version.** Slice mode does not depend on how `oiftrelax` seeds the faces of a
  one-slice volume, a rule that recent builds changed: it passes a boundary stride of 0, which
  turns that seeding off, and writes the border seeds itself.

### Practical notes
Measured on 2026-10-05 by reproducing the slice runner's pipeline with the built
`oiftrelax`. Each figure comes from one image or one case and is not a target.
- **Speed and memory.** One slice run took about 0.05 s and 11 MB at 512 x 512, about 0.8 s
  and 103 MB at 2048 x 2048, and about 2.3 s and 290 MB for a 12-megapixel image
  (4000 x 3000): about 24 bytes per pixel for the larger images (the 512 x 512 run is about
  42).
- **Real data.** The scikit-image `coins` image reached a Dice coefficient of 0.977 against
  a reference made by an Otsu threshold (106.4) with holes filled, components of 100 pixels
  or fewer removed and components touching the edge dropped, which keeps 23 of the 24 coins.
  On the chest CT case `Stage1-0015ceb851d7251b8f399e39779d1e7d` (512 x 512 x 195), the axial
  slice z = 104, the coronal slice y = 228 and the sagittal slice x = 164 reached lung Dice
  0.989, 0.953 and 0.993 against the case's lung mask (labels 1 and 2); the coronal shortfall
  is the main bronchus joining the right lung.
- **The border at the plane edge.** With the lungs touching the top edge of a cropped coronal
  plane, Dice was 0.841 with `Background on the plane border` and 0.937 without it.
- **Seed placement.** A single object seed on a local extremum can remain trapped there when
  `Smoothing` is `None (sharp)`: one coin reached Dice 0.021 without smoothing and 0.988 with
  the seed moved by one pixel. `Light (1×)` was not measured. Place several seeds per object.
- **Polarity on photographs.** Colour becomes luminance, so a coloured object may be darker
  than its surroundings in grey: the orange suit in scikit-image's `astronaut` is mid-grey,
  between a bright backdrop and a black background. Choose the polarity from the grey
  picture, or use 0.
- **Scale.** A raster image is measured in pixels, so the smoothing acts over a fixed number
  of pixels, and an upscaled copy of the same image segments differently: the result on a
  512-pixel image and the result on a 2048-pixel copy of it agreed with Dice 0.877.

## Mask Heatmap
- In the `Mask` top tab, use `Advanced -> Heatmap`.
- When enabled, ROIFT_GUI aggregates all masks listed in `Masks` for the selected image and displays a combined RGB heatmap overlay.
- Heatmap intensity is normalized by the number of masks that were successfully loaded and matched to image dimensions.

## Right-Click Point Query
- Right-click any pixel in the axial/sagittal/coronal viewer to open a context menu on that voxel.
- `Copy coordinates and value` puts `X: 12, Y: 34, Z: 56, HU: -812` on the clipboard, so a voxel
  read off a slice can be pasted straight into a script or a note. The value is the image
  intensity at that voxel, not the mask label, whichever plane you clicked in.
- `Erase seeds near this point` appears only on the `Seeds` tab, and clears seeds within the
  seed brush radius.
- `Select network node <id>` and `Select network segment <name>` appear when a solver network is
  placed on the image and the voxel is near one; see below.

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

## Solver Network (1D haemodynamic YAML)
- `Tools > Solver Network...` in the top bar opens its window, which is separate rather than a
  sidebar section because it serves one workflow only. `Load Network (YAML)...` reads the
  openBF-style network that `vessels.cli.analyze_vessels --solver-yaml` writes
  (`<case>_artery_solver.yaml`), or any
  file of that layout: top-level scalars, one level of blocks, and a `network:` list of flat
  entries. An entry without `label`, `sn`, `tn`, `L` or `R0` refuses the whole file, by name.
- **Placing it on the image** takes the three files the export writes beside the YAML, all on
  the image's grid: `<stem>_geometry.json` (node positions, one ordered centreline per segment),
  `<stem>_segments.nii.gz` (every vessel voxel, labelled with its segment's position in the YAML;
  a branch the network cut carries the segment it hangs from) and `<stem>_lumen.nii.gz` (the
  modelled vessels only). Open the image first. The geometry is checked before anything is
  drawn: the YAML text must hash to its `yaml_sha256`, every segment must have the same label and
  nodes, every node a position, and the grid the image's size. **Any disagreement refuses the
  geometry and both maps**, and the summary line says why; the tree and details still work.
  An older export without the geometry is placed by its segment map alone, without nodes.
- **Colour** chooses which map is drawn: `Territory` (default; matches the artery file) or
  `Modelled lumen`. Both are added to the mask list.
- **The tree** is the topology: the trunk at the root, a segment's wider daughter on the same
  level after it, each narrower daughter nested under it, so a long chain does not indent off
  the panel. `L` and `Ø` in mm, `●` for an outlet. **Find** filters by name (`LB_0`, `RPA`);
  Enter on `n27` or `27` selects node 27.
- **Details** of the selected segment: size, E, its start and end nodes, its parent and
  daughters, its lumen and territory volumes, then every other key as written. Of a node: its
  kind (inlet, junction, outlet), voxel, and the segments into and out of it. Every node and
  segment named there is a link that selects it.
- **Selecting** a segment moves the three slices to half-way along its centreline and names it
  beside the marker; a node, to its position. A connector the export inserted has no lumen of
  its own, and says so.
- **Hover**, on a slice or on the 3D surface, names what is under the pointer: a node within
  2.5 mm (on the surface, within its vessel's radius), else the segment whose territory it is,
  with `fed, not modelled` when the voxel is outside that segment's lumen. The status bar says
  the same. Nothing is recomputed on hover: slices repaint their overlay; the 3D view re-renders
  once per change of the hovered item, at most every 60 ms, and never while the camera is
  dragged.
- **Graph on slices** draws every centreline within 3 mm of the slice, in its segment's colour,
  and the nodes there (green inlet, white junction, orange outlet); the selection bold in cyan,
  with its course off the slice dashed. **Graph in 3D** draws the same over the surface, in a
  layer the surface cannot hide. Only the selection and the hovered item are named in 3D.
- Right-click on a slice offers `Select network node` and `Select network segment` for what is
  under the cursor; Shift+click on the 3D surface selects it.
- **Unload** (the button beside `Load`, or `Tools > Unload Solver Network`) takes the network out
  of the viewer: both maps leave the mask list, and the tree, the details, the overlays and the
  3D graph are cleared. The files are not touched. Loading another network unloads the first.

## Example workflows
- Place seeds for two labels, tick `Batch per label` in the `Segmentation` section, click
  `Run`, and select an output directory. The per-label outputs are merged into
  `segmentation_multilabel.nii.gz` in that directory, which is added to the image's mask list.
- Save seeds to a `.txt` file with `Save` in the `File` group of the `Seeds` section to
  reproduce or share seed sets.
