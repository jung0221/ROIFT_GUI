#pragma once

/**
 * UiUtils.h — Shared UI utility functions extracted from ManualSeedSelector.
 *
 * Contains: icon rendering, CSV parsing, path resolution, windowing helpers,
 * * axis mapping, icon creation, CSV parsing, and windowing utilities.
 */

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>
#include <vector>

class QDoubleSpinBox;
class QSlider;
class QWidget;
class RangeSlider;

namespace UiUtils
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Qt item data role for storing file paths in list widgets.
constexpr int kPathRole = Qt::UserRole;
/// Qt item data role for storing the source image path on mask list items.
constexpr int kMaskSourceImageRole = Qt::UserRole + 1;
/// Number of discrete ticks used by the window range slider.
constexpr int kWindowSliderTicks = 4096;

// ---------------------------------------------------------------------------
// Icon rendering
// ---------------------------------------------------------------------------

/// Identifiers for the built-in button icons used in the NIfTI panel.
enum class NiftiButtonIcon
{
    Add,
    AddCsv,
    ExportCsv,
    Remove,
    RemoveAll,
    Load,
    Refresh,
    Ruler
};

/// Create a fallback (non-SVG) icon for the given button type.
QIcon makeFallbackButtonIcon(NiftiButtonIcon type, const QSize &size);

/// Render a monochrome icon from inline SVG data (requires Qt SVG).
/// Falls back to @p fallback if SVG is unavailable or invalid.
QIcon makeMonochromeIcon(const char *svgData, const QSize &size, const QIcon &fallback = QIcon());

// SVG data strings for each icon type.
extern const char *kAddIconSvg;
extern const char *kAddCsvIconSvg;
extern const char *kRemoveIconSvg;
extern const char *kExportCsvIconSvg;
extern const char *kRemoveAllIconSvg;
extern const char *kLoadIconSvg;
extern const char *kRefreshIconSvg;
extern const char *kRulerIconSvg;

// ---------------------------------------------------------------------------
// Slider controls
// ---------------------------------------------------------------------------

/// Edge length in px of the "-"/"+" step buttons flanking a stepper slider.
constexpr int kSliderStepButtonSize = 18;
/// Hold-to-repeat timings (ms) for the step buttons.
constexpr int kSliderStepRepeatDelay = 300;
constexpr int kSliderStepRepeatInterval = 60;

/// Wrap @p slider in a row with "-" / "+" single-step buttons on either side.
/// The buttons auto-repeat while held and grey out at the range ends.
/// Ownership of @p slider moves to the returned row widget.
QWidget *makeSliderStepperRow(QSlider *slider, QWidget *parent = nullptr);

// ---------------------------------------------------------------------------
// CSV helpers
// ---------------------------------------------------------------------------

/// Trim and unquote a single CSV cell value.
QString normalizeCsvCell(QString value);

// ---------------------------------------------------------------------------
// Openable image formats
//
// Kept in one place so the file dialogs, the CSV importer and the folder scan
// all agree on what counts as an image instead of each repeating the list.
// ---------------------------------------------------------------------------

/// True when the path's extension is one the GUI can open as a volume.
bool isSupportedImagePath(const QString &path);

/// QFileDialog filter for opening images (NIfTI, DICOM and NumPy).
QString imageOpenFileFilter();

/// QFileDialog filter for opening masks (NIfTI and NumPy; DICOM holds no labels).
QString maskOpenFileFilter();

/// Strip a known image extension (.nii, .nii.gz, .npz, .npy) from a filename.
QString stripImageSuffix(const QString &fileName);

/// Escape a value for CSV output (handles commas, quotes, newlines).
QString csvEscapeCell(const QString &value);

/// Parse a single CSV row into a list of field values.
QStringList parseCsvRow(const QString &line);

/// Return true if the cell value looks like a path to an openable image.
bool isImagePathCell(const QString &value);

/// Return true if the filename looks like a mask volume (NIfTI or NumPy).
bool isMaskFilenameCandidate(const QString &fileName);

/// Return true if the filename looks like a seed file (ends in .txt).
bool isSeedFilenameCandidate(const QString &fileName);

/// Heuristically choose the column most likely to contain image paths.
int chooseImageColumn(const QStringList &headers, const std::vector<int> &pathCounts);

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

/// Walk up directory levels looking for @p relativePath starting from @p startDir.
QString findPathByAscending(const QString &startDir, const QString &relativePath, int maxLevels = 12);

/// Find the super-resolution Python script path.
QString resolveSuperResolutionScriptPath();

/// Find the super-resolution model weights path.
QString resolveSuperResolutionModelPath(const QString &scriptPath);

/// Find the mask post-processing Python script path.
QString resolveMaskPostprocessScriptPath();

/// Resolve a Python interpreter (sets @p program and optional @p prefixArgs).
bool resolvePythonCommand(QString *program, QStringList *prefixArgs);

/// Resolve an arbitrary project-relative script path.
QString resolveProjectScriptPath(const QString &relativePath);

/// Open the containing folder in the OS file manager, optionally selecting the file.
bool revealPathInFileManager(const QString &path, QString *openedPath = nullptr, QString *errorMessage = nullptr);

// ---------------------------------------------------------------------------
// Progress / display helpers
// ---------------------------------------------------------------------------

/// Build an ASCII progress bar string like "[====      ]".
std::string makeTerminalProgressBar(int done, int total, int width = 30);

// ---------------------------------------------------------------------------
// Axis / depth mapping
// ---------------------------------------------------------------------------

/// Map an index from one axis size to another (nearest-neighbour resampling).
unsigned int mapDepthIndex(unsigned int sourceIndex, unsigned int sourceDepth, unsigned int targetDepth);

// ---------------------------------------------------------------------------
// Window / level controls
// ---------------------------------------------------------------------------

/// Configure the range slider and spin boxes for a given image intensity range.
void configureWindowControls(float gmin,
                             float gmax,
                             RangeSlider *windowSlider,
                             QDoubleSpinBox *windowLevelSpin,
                             QDoubleSpinBox *windowWidthSpin,
                             float *windowGlobalMin,
                             float *windowGlobalMax,
                             float *windowLow,
                             float *windowHigh);

/// Convert an intensity value to a slider tick position.
int windowValueToSliderTick(float value, float gmin, float gmax);

/// Convert a slider tick position back to an intensity value.
float sliderTickToWindowValue(int tick, float gmin, float gmax);

} // namespace UiUtils
