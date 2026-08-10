#pragma once

/**
 * MaskLayers.h — the mask volumes the viewer draws.
 *
 * The window *edits* exactly one mask (the buffer in ManualSeedSelector), but
 * it may *draw* several at once, and the two are independent: a MaskLayer is a
 * label volume plus the rule that turns its labels into colours, and the eye in
 * the mask list is the only thing that decides whether it is drawn.
 */

#include <QColor>
#include <QString>

#include <cstddef>
#include <string>
#include <vector>

#include "NiftiImage.h" // NpzImportOptions

/// A label volume on its own grid: C-order, X fastest, 0 = background.
struct MaskVolume
{
    std::vector<int> data;
    unsigned int dimX = 0;
    unsigned int dimY = 0;
    unsigned int dimZ = 0;
    double spacingX = 1.0;
    double spacingY = 1.0;
    double spacingZ = 1.0;

    std::size_t voxelCount() const;
    /// True when the dimensions are non-zero and the buffer matches them.
    bool isValid() const;
    /// Distinct non-zero labels, ascending.
    std::vector<int> distinctLabels() const;
};

/// Distinct non-zero labels in a label buffer, ascending.
std::vector<int> distinctMaskLabels(const std::vector<int> &data);

/// Read a mask file into @p out: NIfTI and the other ITK formats through ITK,
/// .npy/.npz through the numpy importer — which has no header to read the
/// layout from, so it takes the current image's convention in @p numpyOptions.
/// Returns false and fills @p error on failure.
bool readMaskVolume(const std::string &path,
                    const NpzImportOptions &numpyOptions,
                    MaskVolume &out,
                    QString *error = nullptr);

/// How one mask turns its label values into colours while others are on screen.
enum class MaskColorMode
{
    Auto,     ///< the mask's own colour when binary, the label palette when not
    PerMask,  ///< every label of this mask in the mask's own colour
    PerLabel, ///< the shared label palette (colorForLabel), whatever the mask
};

/// Whether a mask is drawn. The eye in the mask list is the only thing that
/// sets it: which mask is *edited* is a separate matter, marked by the row
/// selection, and does not put anything on screen.
enum class MaskVisibility
{
    Hidden = 0,
    Visible = 1,
};

/// A mask the viewer draws. `volume` is empty for the mask being edited: its
/// voxels live in the editable buffer, and only the style fields apply here.
struct MaskLayer
{
    QString path; ///< cleaned absolute path — the layer's identity everywhere
    MaskVolume volume;
    std::vector<int> labels; ///< distinct non-zero labels, ascending
    MaskColorMode colorMode = MaskColorMode::Auto;
    QColor color;         ///< the mask's own colour: palette slot, or user override
    int colorSlot = 0;    ///< slot the colour came from, released when the layer goes
    bool visible = false; ///< the eye is open: draw it, whatever is being edited

    /// Resolves Auto: a mask with more than one label reads better in the
    /// shared label palette than flattened into a single colour.
    bool usesLabelPalette() const;
    QColor colorForLabelValue(int label) const;
    /// Up to @p maxColors representative colours, for a list swatch.
    std::vector<QColor> swatchColors(int maxColors = 3) const;
};

/// Distinct, CT-readable colour for a per-mask palette slot; wraps around.
QColor maskSlotColor(int slot);
/// Number of slots before maskSlotColor() repeats itself.
int maskSlotCount();
