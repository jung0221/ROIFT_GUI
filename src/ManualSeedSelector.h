#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QColor>
#include <QStringList>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "NiftiImage.h"
#include "OrthogonalView.h"
#include "RangeSlider.h"

class QDoubleSpinBox;
class QCheckBox;
class QListWidget;
class QTabWidget;
class QProgressBar;
class QTimer;
class QResizeEvent;
class QMoveEvent;

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
    // convenience wrapper to load a mask and update views (used by segmentation runner)
    bool applyMaskFromPath(const std::string &path);
    // refresh mask/seed associations from disk for current image
    void refreshAssociatedFilesForCurrentImage(bool forceDetect = false);
    // add multiple NIfTI images to the list (used by CLI startup)
    int addImagesFromPaths(const QStringList &paths);

    // Expose segmentation parameters
    double getPolarity() const { return m_polSlider ? m_polSlider->value() / 100.0 : 1.0; }
    int getNiter() const { return m_niterSlider ? m_niterSlider->value() : 1; }
    int getPercentile() const { return m_percSlider ? m_percSlider->value() : 0; }
    bool getSegmentAll() const { return m_segmentAllBox ? m_segmentAllBox->isChecked() : false; }
    bool getPolaritySweep() const { return m_polSweepBox ? m_polSweepBox->isChecked() : false; }
    bool getUseGPU() const { return m_useGPUBox ? m_useGPUBox->isChecked() : false; }
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
    void saveSeeds();
    void loadSeeds();
    bool saveImageToFile(const std::string &path);
    void onAxialClicked(int x, int y, Qt::MouseButton b);
    void onSagittalClicked(int x, int y, Qt::MouseButton b);
    void onCoronalClicked(int x, int y, Qt::MouseButton b);
    void updateViews();
    void requestViewUpdate(bool immediate = false);

    // Mask features
    void setMaskMode(int mode); // 0=idle,1=draw,2=erase
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

    void setupUi();
    int addImagesToList(const QStringList &paths, int *duplicateCount = nullptr, int *missingCount = nullptr);
    void renumberNiftiListItems();
    int addMaskPathsToCurrentContext(const QStringList &paths, int *duplicateCount = nullptr, int *missingCount = nullptr);
    int resolveMaskTargetImageIndex() const;
    QStringList extractNiftiPathsFromCsv(const QString &csvPath, QString *errorMessage = nullptr);
    void autoDetectAssociatedFilesForImage(int imageIndex, bool force = false);
    bool appendNiftiImagePath(const QString &path, bool *isDuplicate = nullptr);
    bool saveHeatmapAsNifti(const std::vector<float> &heatmapData, int usedMasks, QString *outputPath = nullptr, QString *errorMessage = nullptr);
    void preloadMasksForPointQuery(bool force = false);
    bool autoLoadAnatomyMasksForCurrentImage(QString *summary = nullptr);
    bool handleSliceKey(QKeyEvent *event);
    bool isSeedsTabActive() const;
    bool isMaskTabActive() const;
    void showViewContextMenu(SlicePlane plane, int planeX, int planeY, const QPoint &globalPos);
    void showMasksOnPointDialog(int x, int y, int z);
    void rebuildPointQueryBuckets();
    void clearPointQueryCache();
    bool rebuildHeatmapForCurrentImage(QString *errorMessage = nullptr);
    void startHeatmapBuildAsync(bool showFailureDialog);
    void onHeatmapProgressTimer();
    void stopHeatmapWorker(bool waitForJoin);
    struct SliceDragState
    {
        bool active = false;
        int startCoord = 0;
        int startValue = 0;
    };
    void beginSliceDrag(SliceDragState &state, int coord, QSlider *slider);
    void updateSliceDrag(SliceDragState &state, int coord, int coordRange, QSlider *slider);
    void endSliceDrag(SliceDragState &state);
    const Seed *findSeedNearCursor(int x, int y, int z, SlicePlane plane, int maxDistance) const;
    void updateHoverStatus(SlicePlane plane, int x, int y, int z);
    void addSeed(int x, int y, int z);
    void eraseNear(int x, int y, int z, int r);
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
    // seed interaction mode: 0=idle,1=draw,2=erase
    int m_seedMode = 1;
    int m_seedBrushRadius = 5;
    int m_seedDisplayMinPixelSpacing = 4;

    // Tabbed UI: inline controls instead of dialogs
    QPushButton *m_btnSeedDraw = nullptr;
    QPushButton *m_btnSeedErase = nullptr;
    QPushButton *m_btnMaskDraw = nullptr;
    QPushButton *m_btnMaskErase = nullptr;
    QPushButton *m_btnMaskHeatmap = nullptr;
    QSpinBox *m_seedBrushSpin = nullptr;
    QSpinBox *m_seedDisplaySpacingSpin = nullptr;
    QSlider *m_maskBrushSpin = nullptr;
    QSlider *m_maskOpacitySlider = nullptr;
    QProgressBar *m_heatmapProgressBar = nullptr;
    QPushButton *m_heatmapCancelButton = nullptr;
    QTimer *m_heatmapProgressTimer = nullptr;
    QTimer *m_viewUpdateTimer = nullptr;
    bool m_viewUpdatePending = false;
    QCheckBox *m_show3DCheck = nullptr;
    QCheckBox *m_showMaskCheck = nullptr;
    QCheckBox *m_showSeedsCheck = nullptr;
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
    bool m_heatmapEnabled = false;
    std::vector<float> m_heatmapData;
    int m_heatmapMaskCount = 0;
    std::thread m_heatmapWorker;
    std::mutex m_heatmapMutex;
    std::atomic<bool> m_heatmapWorkerActive{false};
    std::atomic<bool> m_heatmapCancelRequested{false};
    std::atomic<int> m_heatmapProgressDone{0};
    std::atomic<int> m_heatmapProgressTotal{0};
    bool m_heatmapResultReady = false;
    bool m_heatmapResultSuccess = false;
    bool m_heatmapShowFailureDialog = false;
    QString m_heatmapResultError;
    std::vector<float> m_heatmapResultData;
    int m_heatmapResultMaskCount = 0;
    struct HeatmapPointQueryMaskCache
    {
        std::string path;
        std::string fileName;
        std::vector<int> minXPerZ;
        std::vector<int> maxXPerZ;
        std::vector<int> minYPerZ;
        std::vector<int> maxYPerZ;
    };
    std::vector<HeatmapPointQueryMaskCache> m_heatmapResultPointQueryCache;
    int m_heatmapBuildImageIndex = -1;
    QString m_heatmapBuildReferencePath;
    unsigned int m_heatmapBuildDimX = 0;
    unsigned int m_heatmapBuildDimY = 0;
    unsigned int m_heatmapBuildDimZ = 0;
    std::vector<std::string> m_heatmapBuildMaskPaths;
    std::vector<HeatmapPointQueryMaskCache> m_heatmapPointQueryCache;
    std::vector<std::string> m_heatmapPointQueryPaths;
    unsigned int m_heatmapPointQueryDimX = 0;
    unsigned int m_heatmapPointQueryDimY = 0;
    unsigned int m_heatmapPointQueryDimZ = 0;
    static constexpr unsigned int kPointQueryBucketSizeXY = 16;
    unsigned int m_heatmapPointQueryBucketCols = 0;
    unsigned int m_heatmapPointQueryBucketRows = 0;
    // key = (z << 32) | (by << 16) | bx, value = indices into m_heatmapPointQueryCache
    std::unordered_map<uint64_t, std::vector<int>> m_heatmapPointQueryBuckets;

    struct CachedMaskForPointQuery
    {
        std::string path;
        std::string fileName;
        unsigned int dimX = 0;
        unsigned int dimY = 0;
        unsigned int dimZ = 0;
        bool metadataLoaded = false;
        bool dimensionCompatible = false;
        QString error;
    };
    std::vector<CachedMaskForPointQuery> m_cachedMasksForPointQuery;
    std::vector<std::string> m_cachedMasksForPointQueryPaths;
    int m_cachedMasksForPointQueryImageIndex = -1;
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
    QCheckBox *m_segmentAllBox = nullptr;
    QCheckBox *m_polSweepBox = nullptr;
    QCheckBox *m_useGPUBox = nullptr;

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
    };

    QListWidget *m_niftiList = nullptr;
    QListWidget *m_maskList = nullptr;
    QListWidget *m_seedList = nullptr;
    QTabWidget *m_ribbonTabs = nullptr;
    int m_seedTabIndex = -1;
    int m_maskTabIndex = -1;
    std::vector<ImageData> m_images;
    std::vector<std::string> m_unassignedMaskPaths;
    std::string m_loadedMaskPath;
    int m_currentImageIndex = -1;
    bool m_clampingWindowGeometry = false;

    void updateMaskSeedLists();
    QColor getColorForImageIndex(int index);
};
