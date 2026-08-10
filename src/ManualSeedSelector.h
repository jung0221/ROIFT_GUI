#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QColor>
#include <QStringList>
#include <functional>
#include <deque>
#include <cstdint>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include "MaskLayers.h"
#include "NiftiImage.h"
#include "OrthogonalView.h"
#include "RangeSlider.h"

class QDoubleSpinBox;
class QCheckBox;
#include <QComboBox>
class QAction;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QTabWidget;
class QGroupBox;
class QVBoxLayout;
class QProgressBar;
class QPlainTextEdit;
class QTimer;
class QResizeEvent;
class QMoveEvent;
class QCloseEvent;
class QPainter;
class QSplitter;
class CollapsibleSection;

struct Seed
{
    int x, y, z, label, internal;
    bool fromFile = false;
};
class Mask3DView;

class ManualSeedSelector : public QMainWindow
{
    Q_OBJECT
public:
    ManualSeedSelector(const std::string &niftiPath, QWidget *parent = nullptr);
    ~ManualSeedSelector();
    // keyboard handling for slice navigation
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    // True if a saved window geometry was restored on startup, so the launcher
    // can skip its default sizing/centering.
    bool geometryWasRestored() const { return m_geometryRestored; }
    // catch key events on child views
    bool eventFilter(QObject *obj, QEvent *event) override;
    // load seeds from a supplied path (used by CLI). Returns true on success.
    bool loadSeedsFromFile(const std::string &path);
    // return true if an image is currently loaded
    bool hasImage() const;
    // initialize sliders and views after an image has been loaded
    void initializeImageWidgets();
    // expose current seeds for external helpers (read-only)
    const std::vector<Seed> &getSeeds() const { return m_seeds; }
    // expose image path
    std::string getImagePath() const { return m_path; }
    // Path of the mask in the editable buffer — the one a row click selects.
    // Empty when the buffer belongs to no file. Which masks are *drawn* is a
    // separate question; see MaskVisibility.
    QString activeMaskPath() const { return QString::fromStdString(m_loadedMaskPath); }

    // A path the native binaries and Python helpers can actually read. Those
    // consume files, not the in-memory volume, and none of them speak numpy —
    // so a .npz/.npy image is exported once to a temporary NIfTI carrying the
    // orientation and spacing it was imported with. Other formats pass through.
    std::string nativeImagePath();
    // convenience wrapper to load a mask and update views (used by segmentation runner)
    bool applyMaskFromPath(const std::string &path);
    // refresh mask/seed associations from disk for current image
    void refreshAssociatedFilesForCurrentImage(bool forceDetect = false);
    // add multiple NIfTI images to the list (used by CLI startup)
    int addImagesFromPaths(const QStringList &paths);
    bool isSegmentationTaskRunning() const { return m_segmentationWorkerActive.load(); }
    bool startSegmentationTask(std::function<void()> task,
                               const QString &initialMessage,
                               const QStringList &initialLogs = {},
                               const QString &progressLabel = QString(),
                               int progressTotal = 0);
    void appendSegmentationLog(const QString &message);
    void setSegmentationTaskProgress(const QString &message, int done = -1, int total = -1);
    void completeSegmentationTask(bool success,
                                  const QString &summary,
                                  const QString &sourceImagePath = QString(),
                                  const QStringList &generatedMaskPaths = {});

    // Expose segmentation parameters
    double getPolarity() const { return m_polSlider ? m_polSlider->value() / 100.0 : 1.0; }
    int getNiter() const { return m_niterSlider ? m_niterSlider->value() : 1; }
    int getPercentile() const { return m_percSlider ? m_percSlider->value() : 0; }
    bool useLegacyBinaryMode() const;
    bool getSegmentAll() const { return m_segmentAllBox ? m_segmentAllBox->isChecked() : false; }
    bool getPolaritySweep() const { return m_polSweepBox ? m_polSweepBox->isChecked() : false; }
    bool getUseGPU() const { return m_useGPUBox ? m_useGPUBox->isChecked() : false; }
    int getSegmentationMethod() const { return m_methodCombo ? m_methodCombo->currentIndex() : 0; }
    double getAlpha() const { return m_alphaSpin ? m_alphaSpin->value() : 0.5; }
    double getSigma() const { return m_sigmaSpin ? m_sigmaSpin->value() : 0.0; }
    // Gaussian pre-smoothing passes for Standard OIFT (CPU). Default 2 = historical double blur.
    int getBlurPasses() const { return m_blurCombo ? m_blurCombo->currentData().toInt() : 2; }
    int getGPUCostMode() const { return 1; }  // always additive (shortest path)
    double getWindowLevel() const { return m_windowLevelSpin ? m_windowLevelSpin->value() : 0.0; }
    double getWindowWidth() const { return m_windowWidthSpin ? m_windowWidthSpin->value() : 1.0; }
    double getImageMin() const { return m_image.getGlobalMin(); }
    double getImageMax() const { return m_image.getGlobalMax(); }

private slots:
    void openImage();
    void openImagesFromCsv();
    void openMasksFromCsv();
    void runLunasSeedGeneration();
    void runRibsSeedGeneration();
    void runSuperResolution();
    void runMaskPostProcessing();
    void runVesselGraph();
    void filterActiveMaskByThreshold();
    void saveSeeds();
    void loadSeeds();
    bool saveImageToFile(const std::string &path);
    void onAxialClicked(int x, int y, Qt::MouseButton b);
    void onSagittalClicked(int x, int y, Qt::MouseButton b);
    void onCoronalClicked(int x, int y, Qt::MouseButton b);
    void updateViews();
    void requestViewUpdate(bool immediate = false);

    // Mask features
    // The two drawing modes share one convention: 0=off, 1=draw, 2=erase.
    // Between them they decide what the left button does in the slice views;
    // setting either to a non-off mode switches the other off.
    void setMaskMode(int mode);
    void setSeedMode(int mode);
    void cleanMask();
    bool saveMaskToFile(const std::string &path);
    bool loadMaskFromFile(const std::string &path);
    void paintAxialMask(int x, int y);
    void paintSagittalMask(int x, int y);
    void paintCoronalMask(int x, int y);
    void applyBrushToMask(const std::array<int, 3> &center, const std::pair<int, int> &axes, int radius, int labelValue, bool erase = false);
    void resetWindowToFullRange();
    void applyWindowFromValues(float low, float high, bool fromSlider);

private:
    enum class SlicePlane
    {
        Axial,
        Sagittal,
        Coronal
    };
    // Seed subset drawn across all views; orthogonal to the per-view Show Seeds toggles.
    enum class SeedTypeFilter
    {
        All,
        Internal,
        External
    };
    // True if seed s passes the active seed-type filter (internal!=0 means internal/object).
    bool seedPassesTypeFilter(const Seed &s) const
    {
        switch (m_seedTypeFilter)
        {
        case SeedTypeFilter::Internal:
            return s.internal != 0;
        case SeedTypeFilter::External:
            return s.internal == 0;
        case SeedTypeFilter::All:
        default:
            return true;
        }
    }
    // Build a seed-type filter dropdown wired to the shared filter and registered for sync.
    QComboBox *makeSeedTypeFilterCombo();
    // Set the active seed-type filter, sync every dropdown, and refresh all views.
    void setSeedTypeFilter(SeedTypeFilter filter);

    void setupUi();
    int addImagesToList(const QStringList &paths, int *duplicateCount = nullptr, int *missingCount = nullptr);
    void renumberNiftiListItems();
    int addMaskPathsToCurrentContext(const QStringList &paths, int *duplicateCount = nullptr, int *missingCount = nullptr);
    int resolveMaskTargetImageIndex() const;
    QStringList extractNiftiPathsFromCsv(const QString &csvPath, QString *errorMessage = nullptr);
    void autoDetectAssociatedFilesForImage(int imageIndex, bool force = false);
    bool appendNiftiImagePath(const QString &path, bool *isDuplicate = nullptr);
    bool autoLoadAnatomyMasksForCurrentImage(QString *summary = nullptr);
    bool handleSliceKey(QKeyEvent *event);
    // Which tool consumes left-clicks in the slice views (sidebar replaces the
    // old "active tab" gating). isSeedsTabActive/isMaskTabActive map onto this.
    enum class InteractionTool { Navigate, Seeds, Mask };
    void setActiveTool(InteractionTool tool);
    // Recompute the active tool from the seed and mask drawing modes. There is
    // no control that sets the tool directly; it is always derived from those.
    void syncActiveTool();
    // Open a readable starting set of sections. Used only on a first run with
    // no persisted state — after that a session's own choices win.
    void expandDefaultSections();
    bool isSeedsTabActive() const;
    bool isMaskTabActive() const;
    // Persist/restore window geometry, splitter sizes and section expansion.
    void saveUiState();
    void restoreUiState();
    void showViewContextMenu(SlicePlane plane, int planeX, int planeY, const QPoint &globalPos);
    void stopSegmentationWorker(bool waitForJoin);
    void refreshSegmentationProgressDisplay();
    int findImageIndexByPath(const QString &imagePath) const;
    struct PendingSegmentationTask
    {
        std::function<void()> task;
        QString initialMessage;
        QStringList initialLogs;
        QString progressLabel;
        int progressTotal = 0;
    };
    void launchSegmentationTask(PendingSegmentationTask &&task);
    struct SliceDragState
    {
        bool active = false;
        int startCoord = 0;
        int startValue = 0;
    };
    void beginSliceDrag(SliceDragState &state, int coord, QSlider *slider);
    void updateSliceDrag(SliceDragState &state, int coord, int coordRange, QSlider *slider);
    void endSliceDrag(SliceDragState &state);
    struct RulerMeasurement
    {
        bool visible = false;
        bool dragging = false;
        int sliceIndex = -1;
        QPoint start;
        QPoint end;
    };
    void setRulerEnabled(bool enabled);
    void clearRulerMeasurements();
    void updateRulerCursor();
    bool handleRulerMousePress(SlicePlane plane, int planeX, int planeY, Qt::MouseButton button);
    bool handleRulerMouseMove(SlicePlane plane, int planeX, int planeY, Qt::MouseButtons buttons);
    bool handleRulerMouseRelease(SlicePlane plane, int planeX, int planeY, Qt::MouseButton button);
    void beginRulerMeasurement(SlicePlane plane, int planeX, int planeY);
    void updateRulerMeasurement(SlicePlane plane, int planeX, int planeY, bool finalize);
    void endRulerMeasurement(SlicePlane plane, int planeX, int planeY);
    RulerMeasurement &rulerForPlane(SlicePlane plane);
    const RulerMeasurement &rulerForPlane(SlicePlane plane) const;
    void drawRulerOverlay(QPainter &p,
                          float scaleX,
                          float scaleY,
                          const RulerMeasurement &ruler,
                          int activeSliceIndex,
                          double spacingU,
                          double spacingV) const;
    // Voxel located from the 3D view; marked with an X until any slider moves.
    struct LocatedPoint
    {
        bool valid = false;
        int x = 0;
        int y = 0;
        int z = 0;
    };
    LocatedPoint m_locatedPoint;
    void drawLocatedPointOverlay(QPainter &p, float scaleX, float scaleY, SlicePlane plane) const;
    // -- Mask layers ---------------------------------------------------------
    // The viewer edits one mask (m_maskData) and draws whichever masks have
    // their eye open — two independent things. m_maskLayers has an entry per
    // drawn mask, plus one for the mask being edited whether it is drawn or
    // not (it holds that mask's colour rule); the entry for the edited mask
    // carries an empty volume, because its voxels are the editable buffer.
    std::vector<MaskLayer> m_maskLayers;
    // Style for a buffer that belongs to no file yet: a mask being painted from
    // scratch, or the anatomy masks merged on load. It has no entry in
    // m_maskLayers because there is no list row to pin.
    MaskLayer m_unsavedMaskStyle;

    // One mask resolved for drawing: where its voxels are and how they colour.
    struct MaskRenderItem
    {
        const std::vector<int> *data = nullptr;
        unsigned int dimX = 0;
        unsigned int dimY = 0;
        unsigned int dimZ = 0;
        const MaskLayer *style = nullptr;
        bool active = false; // the label-visibility filter applies to this one
    };
    // Masks to draw, in paint order; the active mask comes last, on top.
    std::vector<MaskRenderItem> visibleMaskRenderItems() const;
    // Blend those masks onto one slice's RGB buffer.
    void blendMaskOverlays(std::vector<unsigned char> &rgb, SlicePlane plane, int sliceIndex) const;

    MaskLayer *findMaskLayer(const QString &absolutePath);
    const MaskLayer *findMaskLayer(const QString &absolutePath) const;
    // Style record for whatever is in the editable buffer: the loaded mask's
    // layer, or m_unsavedMaskStyle when the buffer came from no file. Kept in
    // step with the labels the buffer contains.
    MaskLayer *activeMaskStyle();
    const MaskLayer *activeMaskStyle() const;
    void adoptActiveMaskLayer(const QString &absolutePath);
    // Hand the editable buffer to the outgoing layer if that mask is drawn, and
    // drop the entry otherwise. Called before another mask takes the buffer.
    void releaseActiveMaskLayer();
    // Eye click: read this mask and draw it, or take it off screen again.
    void toggleMaskVisible(const QString &absolutePath);
    // Draw a mask that was loaded on the program's initiative rather than by a
    // click — a segmentation result, a CLI argument — so it is not silently
    // invisible. Returns false when it could not be shown.
    bool setActiveMaskVisible();
    // Forget a mask entirely, freeing whatever voxels it held.
    void dropMaskLayer(const QString &absolutePath);
    void clearMaskLayers();
    // True when a mask can share the grid the window already draws on. The
    // overlay maps depth only, so X/Y have to agree.
    bool maskVolumeCoregisters(const MaskVolume &volume, QString *reason = nullptr) const;
    // What the eye on this row should show.
    MaskVisibility maskVisibilityForPath(const QString &absolutePath) const;
    // Lowest palette slot no drawn mask is using.
    int nextFreeMaskColorSlot() const;
    // Ask before a pin pushes the drawn masks past a sane memory footprint.
    bool confirmMaskLayerMemory(std::size_t additionalVoxels);
    // Add a label to the active layer's label list as soon as it is painted, so
    // its colour rule (Auto) reacts to the mask becoming multi-label.
    void noteActiveMaskLabel(int label);
    // Mask-list context menu additions: pin, colour mode, colour override.
    // The two halves bracket the menu's exec(): one adds the entries, the other
    // applies whichever was chosen.
    struct MaskMenuActions
    {
        QAction *pin = nullptr;
        QAction *colorAuto = nullptr;
        QAction *colorPerMask = nullptr;
        QAction *colorPerLabel = nullptr;
        QAction *pickColor = nullptr;
    };
    MaskMenuActions appendMaskLayerMenuActions(QMenu &menu, const QString &absolutePath);
    bool applyMaskLayerMenuAction(const MaskMenuActions &actions, QAction *selected, const QString &absolutePath);

    // Mask-label filter helpers (see m_maskLabelVisibility).
    void rebuildMaskLabelFilter();                 // resync checkboxes with present labels
    void setAllMaskLabelsVisible(bool visible);    // "All"/"None" buttons
    bool maskLabelVisible(int label) const;        // background (0) is always hidden
    bool maskHasHiddenLabels() const;
    QString formatRulerDistance(double millimeters) const;
    const Seed *findSeedNearCursor(int x, int y, int z, SlicePlane plane, int maxDistance) const;
    void updateHoverStatus(SlicePlane plane, int x, int y, int z);
    void addSeed(int x, int y, int z);
    void eraseNear(int x, int y, int z, int r);
    // Move the three slice views onto one voxel; out-of-range is ignored.
    void jumpToVoxel(int x, int y, int z);
    void update3DMaskView();

    void updateLabelColor(int label);
    void clampWindowToCurrentScreen();

    NiftiImage m_image;
    std::string m_path;
    std::vector<Seed> m_seeds;

    OrthogonalView *m_axialView;
    OrthogonalView *m_sagittalView;
    OrthogonalView *m_coronalView;
    QSlider *m_axialSlider;
    QSlider *m_sagittalSlider;
    QSlider *m_coronalSlider;
    QLabel *m_axialLabel;
    QLabel *m_sagittalLabel;
    QLabel *m_coronalLabel;
    QSpinBox *m_labelSelector;
    QLabel *m_labelColorIndicator;
    QLabel *m_statusLabel;
    QPlainTextEdit *m_logConsole = nullptr;
    // backup copy used to undo destructive edits like threshold
    NiftiImage m_imageBackup;
    bool m_hasImageBackup = false;
    QPushButton *m_btnUndoThreshold = nullptr;
    bool m_mouseDown = false;
    int m_dragButton = 0;
    SliceDragState m_axialSliceDrag;
    SliceDragState m_sagittalSliceDrag;
    SliceDragState m_coronalSliceDrag;
    std::vector<std::array<int, 3>> m_colorLUT;
    // mask buffer: linearized X * Y * Z, 0 means empty, positive integers are label values
    std::vector<int> m_maskData;
    unsigned int m_maskDimX = 0;
    unsigned int m_maskDimY = 0;
    unsigned int m_maskDimZ = 0;
    int m_maskMode = 0;
    int m_maskBrushRadius = 6;
    float m_maskOpacity = 0.5f;
    // Mask-label visibility filter (Mask tab). Maps a label value to whether it
    // is shown in the 2D/3D viewers. Labels absent from the map default to
    // visible, so newly drawn labels appear automatically.
    std::map<int, bool> m_maskLabelVisibility;
    // seed interaction mode: 0=idle,1=draw,2=erase
    int m_seedMode = 1;
    int m_seedBrushRadius = 5;
    int m_seedDisplayMinPixelSpacing = 4;
    SeedTypeFilter m_seedTypeFilter = SeedTypeFilter::All;

    // Tabbed UI: inline controls instead of dialogs
    QPushButton *m_btnSeedDraw = nullptr;
    QPushButton *m_btnSeedErase = nullptr;
    // The two "Off" buttons are members because switching one drawing mode on
    // has to visibly switch the other row back to Off.
    QPushButton *m_btnSeedOff = nullptr;
    QPushButton *m_btnMaskDraw = nullptr;
    QPushButton *m_btnMaskErase = nullptr;
    QPushButton *m_btnMaskOff = nullptr;
    QSpinBox *m_seedBrushSpin = nullptr;
    QSpinBox *m_seedDisplaySpacingSpin = nullptr;
    QSlider *m_maskBrushSpin = nullptr;
    QSlider *m_maskOpacitySlider = nullptr;
    // Mask-label filter UI: the section is shown only when the mask has >1 label;
    // the layout holds one swatch+checkbox row per present label, rebuilt on
    // mask change.
    CollapsibleSection *m_maskLabelSection = nullptr;
    QVBoxLayout *m_maskLabelFilterLayout = nullptr;
    QProgressBar *m_segmentationProgressBar = nullptr;
    QTimer *m_viewUpdateTimer = nullptr;
    bool m_viewUpdatePending = false;
    QCheckBox *m_show3DCheck = nullptr;
    QCheckBox *m_showMaskCheck = nullptr;
    QCheckBox *m_showSeedsCheck = nullptr;
    std::vector<QComboBox *> m_seedTypeFilterCombos;
    QCheckBox *m_autoDetectAssociationsCheck = nullptr;

    Mask3DView *m_mask3DView = nullptr;
    bool m_mask3DDirty = false;
    bool m_enable3DView = false;
    double m_maskSpacingX = 1.0;
    double m_maskSpacingY = 1.0;
    double m_maskSpacingZ = 1.0;
    bool m_enableAxialMask = true;
    bool m_enableSagittalMask = true;
    bool m_enableCoronalMask = true;
    bool m_enableAxialSeeds = true;
    bool m_enableSagittalSeeds = true;
    bool m_enableCoronalSeeds = true;
    bool m_enable3DSeeds = true;
    bool m_autoDetectAssociatedFiles = false;
    RangeSlider *m_windowSlider = nullptr;
    QDoubleSpinBox *m_windowLevelSpin = nullptr;
    QDoubleSpinBox *m_windowWidthSpin = nullptr;
    float m_windowLow = 0.0f;
    float m_windowHigh = 1.0f;
    float m_windowGlobalMin = 0.0f;
    float m_windowGlobalMax = 1.0f;
    bool m_blockWindowSignals = false;

    // Segmentation UI elements
    QSlider *m_polSlider = nullptr;
    QLabel *m_polValue = nullptr;
    QSlider *m_niterSlider = nullptr;
    QLabel *m_niterValue = nullptr;
    QSlider *m_percSlider = nullptr;
    QLabel *m_percValue = nullptr;
    QComboBox *m_segmentationModeCombo = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QDoubleSpinBox *m_alphaSpin = nullptr;
    QDoubleSpinBox *m_sigmaSpin = nullptr;
    QComboBox *m_blurCombo = nullptr;
    QLabel *m_alphaLabel = nullptr;
    QLabel *m_sigmaLabel = nullptr;
    QLabel *m_blurLabel = nullptr;
    QCheckBox *m_segmentAllBox = nullptr;
    QCheckBox *m_polSweepBox = nullptr;
    QCheckBox *m_useGPUBox = nullptr;
    // GPU cost mode removed — always additive
    QPushButton *m_btnRunSegment = nullptr;

    // Vessel graph (Morse centreline of the current mask, rooted at a seed)
    QComboBox *m_vgraphDomainCombo = nullptr;
    QDoubleSpinBox *m_vgraphDeltaSpin = nullptr;
    QDoubleSpinBox *m_vgraphCenteringSpin = nullptr;
    QComboBox *m_vgraphLabelModeCombo = nullptr;
    QCheckBox *m_vgraphCenterlineBox = nullptr;
    QPushButton *m_btnRunVesselGraph = nullptr;

    std::deque<PendingSegmentationTask> m_pendingSegmentationTasks;
    std::thread m_segmentationWorker;
    std::atomic<bool> m_segmentationWorkerActive{false};
    QString m_segmentationProgressLabel;
    int m_segmentationProgressDone = -1;
    int m_segmentationProgressTotal = -1;

    // Multiple files support with image-specific masks and seeds
    struct ImageData
    {
        std::string imagePath;
        std::vector<std::string> maskPaths;
        std::vector<std::string> seedPaths;
        QColor color; // Color to identify this image's items
        int lastAxialSlice = -1;
        int lastSagittalSlice = -1;
        int lastCoronalSlice = -1;
        // Numpy containers hold no header, so how to read them is settled once
        // when the file is added and reused every time it is selected again.
        bool isNumpy = false;
        NpzImportOptions npzOptions;
    };

    // Load a list entry, honouring its numpy import options when it has any.
    // Writes back what an automatic axis order resolved to, so masks opened
    // afterwards can be read exactly the same way.
    bool loadImageData(ImageData &data);

    // Numpy import options for a mask of the current image: the image's own
    // axis order and mirroring, so both land on the same voxel grid.
    NpzImportOptions numpyOptionsForMask() const;

    // Cached NIfTI export of a numpy image, and the image it was made from.
    std::string m_nativeImagePath;
    std::string m_nativeImageSource;

    QListWidget *m_niftiList = nullptr;
    QListWidget *m_maskList = nullptr;
    QListWidget *m_seedList = nullptr;
    // Collapsible-sidebar shell (replaces the old ribbon QTabWidget).
    InteractionTool m_activeTool = InteractionTool::Navigate;
    bool m_geometryRestored = false;
    std::vector<CollapsibleSection *> m_toolSections;
    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_contentSplitter = nullptr;
    QSplitter *m_sidebarSplitter = nullptr;
    QPushButton *m_btnRuler = nullptr;
    std::vector<ImageData> m_images;
    std::vector<std::string> m_unassignedMaskPaths;
    std::string m_loadedMaskPath;
    // User-assigned display names, keyed by cleaned absolute path (in-memory, per session).
    std::map<QString, QString> m_displayNameOverrides;
    int m_currentImageIndex = -1;
    bool m_clampingWindowGeometry = false;
    bool m_rulerEnabled = false;
    RulerMeasurement m_axialRuler;
    RulerMeasurement m_sagittalRuler;
    RulerMeasurement m_coronalRuler;

    void updateMaskSeedLists();
    // Session-only display rename for a list item; the file on disk is left untouched.
    void promptRenameListItem(QListWidget *list, QListWidgetItem *item);
    // Custom display name for a path, or fallback when the user has not renamed it.
    QString displayNameForPath(const QString &absolutePath, const QString &fallback) const;
    QColor getColorForImageIndex(int index);
};
