#pragma once

#include <QColor>
#include <QString>
#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <vector>
#include <map>
#include <set>
#include <vtkSmartPointer.h>

#include <array>
#include <memory>
#include <thread>

QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QSlider)

class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkTextActor;
class vtkCellPicker;
class vtkGenericOpenGLRenderWindow;
class vtkPointGaussianMapper;
class vtkPolyData;
class vtkRenderer;
class QRubberBand;
class QTimer;

struct SeedRenderData
{
    int x = 0;
    int y = 0;
    int z = 0;
    int label = 1;
    int seedIndex = -1;

    bool operator==(const SeedRenderData &other) const
    {
        return x == other.x && y == other.y && z == other.z && label == other.label && seedIndex == other.seedIndex;
    }
    bool operator!=(const SeedRenderData &other) const { return !(*this == other); }
};

/// A name drawn at a voxel, in the overlay layer: the surface never hides it.
struct Annotation3D
{
    int x = 0;
    int y = 0;
    int z = 0;
    QString text;
    QColor color = Qt::white;
    bool emphasised = false;
};

/// A rectangle drawn on the 3D view, with the projection it was drawn under. A voxel is inside
/// when its centre (index * spacing, like the surface) projects into it, at any depth.
struct ScreenRectangle
{
    std::array<double, 16> worldToView{}; // VTK composite projection, row-major: view = M * world
    std::array<double, 3> spacing{1.0, 1.0, 1.0};
    double left = 0.0, right = 0.0, bottom = 0.0, top = 0.0; // normalised view coordinates, -1..1
    bool containsVoxel(int x, int y, int z) const;
};

/// Which labels a rectangle drawn on the 3D view erases.
enum class RectangleErase
{
    SelectedLabel, // Shift+drag
    ShownLabels,   // Ctrl+Shift+drag: every label whose Show box is ticked
};

/// A node-and-line graph drawn inside the surface, e.g. a 1D solver network.
/// Positions are voxel indices, possibly fractional.
struct NetworkGraph3D
{
    using Point = std::array<double, 3>;
    struct Line
    {
        std::vector<Point> points;
        QColor color;
    };
    struct Node
    {
        Point position{};
        QColor color;
    };
    std::vector<Line> lines;
    std::vector<Node> nodes;
    bool empty() const { return lines.empty() && nodes.empty(); }
};

class Mask3DView : public QWidget
{
    Q_OBJECT
public:
    struct CameraState
    {
        bool valid = false;
        double position[3] = {0.0, 0.0, 1.0};
        double focalPoint[3] = {0.0, 0.0, 0.0};
        double viewUp[3] = {0.0, 1.0, 0.0};
        double clippingRange[2] = {0.1, 1000.0};
        double parallelScale = 1.0;
        double viewAngle = 30.0;
        int parallelProjection = 0;
    };

    explicit Mask3DView(QWidget *parent = nullptr);
    ~Mask3DView() override;
    /// Render one label volume, one shell per label. With several masks on screen the caller merges
    /// them into one volume of unique ids, and then the label values no longer
    /// carry their own colours or names — pass @p labelColors and @p labelNames
    /// so the surface and the label picker still say which mask each id is.
    /// The surface is built on a worker thread: this returns at once, the previous
    /// surface stays on screen until the new one replaces it, and a volume passed
    /// while one is being built replaces any volume still waiting. The camera is
    /// framed for the first surface only.
    void setMaskData(std::vector<int> mask,
                     unsigned int sizeX,
                     unsigned int sizeY,
                     unsigned int sizeZ,
                     double spacingX,
                     double spacingY,
                     double spacingZ,
                     const std::map<int, QColor> *labelColors = nullptr,
                     const std::map<int, QString> *labelNames = nullptr);
    void setVoxelSpacing(double spacingX, double spacingY, double spacingZ);
    void setSeedData(const std::vector<SeedRenderData> &seeds);
    /// Replace every text label; an empty list removes them. Renders immediately.
    /// Meant for a handful: every label is redrawn on each camera move.
    void setAnnotations(const std::vector<Annotation3D> &annotations);
    /// The graph, built once into two actors (all lines, all nodes); empty removes it.
    void setNetworkGraph(const NetworkGraph3D &graph);
    /// Emphasise one line and/or one node of it (the selection) and another pair
    /// more faintly (the hover). Empty inputs clear. Renders immediately.
    void setNetworkHighlight(const std::vector<NetworkGraph3D::Point> &selectedLine,
                             const NetworkGraph3D::Point *selectedNode,
                             const std::vector<NetworkGraph3D::Point> &hoveredLine,
                             const NetworkGraph3D::Point *hoveredNode);
    void setNetworkVisible(bool visible);
    void setMaskVisible(bool visible);
    /// Hide the shells of these label values and show the others: a visibility flag per shell, no
    /// rebuild. Applies to the surface on screen and to every later one.
    void setHiddenLabels(const std::set<int> &labels);
    /// True while the shell of @p label is drawn.
    bool labelShown(int label) const;
    void setSeedsVisible(bool visible);
    // Set the 3D mask surface opacity, in [0, 1] (clamped). Renders immediately.
    void setMaskOpacity(float opacity);
    float maskOpacity() const { return m_opacity; }
    void clearMask();
    /// True while a mask surface is shown, whether or not "Show 3D" is drawing it.
    bool hasMaskSurface() const { return !m_activeLabels.empty(); }
    /// True while a surface is being built or waits to be.
    bool surfaceBuildPending() const { return m_surfaceJob != nullptr; }
    /// Blocks until no surface is being built or waiting, and shows the last one.
    void waitForSurface();
    /// Surfaces built and shown since construction.
    int surfaceBuildCount() const { return m_surfaceBuildCount; }
    /// Seeds placed by the last setSeedData(), whether or not "Show Seeds" is drawing them.
    size_t seedCount() const { return m_seedRenderData.size(); }
    void setSeedRectangleEraseEnabled(bool enabled);
    CameraState captureCameraState() const;
    void restoreCameraState(const CameraState &state, bool render = true);

signals:
    void eraseSeedsInRectangle(const QVector<int> &seedIndices);
    // Shift+click hit the mask surface at this voxel.
    void surfacePointPicked(int x, int y, int z);
    // Shift+drag (or Ctrl+Shift+drag, scope ShownLabels) drew this rectangle.
    void labelRectangleSelected(const ScreenRectangle &rectangle, RectangleErase scope);
    // The pointer rests over the surface at this voxel; emitted when the voxel
    // changes, at most every hover interval, never while a button is down.
    void surfacePointHovered(int x, int y, int z);
    // The pointer left the surface or the view.
    void surfaceHoverLeft();

private slots:
    void onVisibilityToggled(bool checked);
    void onOpacityChanged(int value);
    void onLabelSelectionChanged(int index);
    void onColorButtonClicked();

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    QVector<int> collectSeedIndicesInRect(const QRect &rect) const;
    // Ray-cast the surface under a widget-space cursor; false when nothing hit.
    bool pickSurfaceVoxel(const QPoint &widgetPos, int &vx, int &vy, int &vz);
    void buildPipeline();
    void applyLabelColors();
    void applyLabelVisibility();
    void removeLabelActors();
    bool anyLabelShown() const;
    void showSurfaceStatus();
    void updateLabelControls();
    void updateColorButtonStyle();
    void setStatusText(const QString &text);
    // Logs the OpenGL renderer after the first frame, and flags a software one.
    void noteRenderer();

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    QCheckBox *m_visibilityCheck = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QComboBox *m_labelCombo = nullptr;
    QPushButton *m_colorButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_statusText;
    // Prefixed to every status line while the renderer is a software rasteriser.
    QString m_rendererWarning;

    vtkSmartPointer<vtkRenderer> m_renderer;
    // One actor per label shell, in m_activeLabels order, so showing a label never re-contours.
    struct LabelActor
    {
        int label = 0;
        vtkSmartPointer<vtkActor> actor;
    };
    std::vector<LabelActor> m_labelActors;
    std::set<int> m_hiddenLabels;
    vtkSmartPointer<vtkActor> m_seedActor;
    vtkSmartPointer<vtkPointGaussianMapper> m_seedMapper;
    vtkSmartPointer<vtkPolyData> m_seedPolyData;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkCellPicker> m_surfacePicker;

    float m_opacity = 0.4f;
    std::vector<SeedRenderData> m_seedRenderData;
    // Spacing the seed glyphs were last placed with; a new spacing invalidates them.
    double m_seedGlyphSpacing[3] = {0.0, 0.0, 0.0};
    std::vector<Annotation3D> m_annotations;
    std::vector<vtkSmartPointer<vtkTextActor>> m_annotationActors;
    // Drawn after the surface with the same camera, so the graph, its highlights
    // and the names stay visible through a dense, translucent tree.
    vtkSmartPointer<vtkRenderer> m_overlayRenderer;
    void rebuildAnnotationActors();

    NetworkGraph3D m_graph;
    bool m_graphVisible = true;
    vtkSmartPointer<vtkActor> m_graphLineActor;
    vtkSmartPointer<vtkActor> m_graphNodeActor;
    vtkSmartPointer<vtkActor> m_selectedLineActor;
    vtkSmartPointer<vtkActor> m_selectedNodeActor;
    vtkSmartPointer<vtkActor> m_hoveredLineActor;
    vtkSmartPointer<vtkActor> m_hoveredNodeActor;
    std::vector<NetworkGraph3D::Point> m_selectedLine;
    std::vector<NetworkGraph3D::Point> m_hoveredLine;
    bool m_hasSelectedNode = false;
    bool m_hasHoveredNode = false;
    NetworkGraph3D::Point m_selectedNode{};
    NetworkGraph3D::Point m_hoveredNode{};
    void rebuildNetworkActors();
    void rebuildHighlightActors();
    NetworkGraph3D::Point toWorld(const NetworkGraph3D::Point &voxel) const;

    // Hover picking: throttled, and against locators built once per surface,
    // so resting the pointer costs one ray cast, not one per mouse event.
    QTimer *m_hoverTimer = nullptr;
    QPoint m_hoverPos;
    bool m_hoverInside = false;
    int m_lastHover[3] = {-1, -1, -1};
    void resolveHover();
    std::vector<int> m_activeLabels;
    std::map<int, QColor> m_labelColors;
    // Names for the label picker when the ids are merged-mask ids and "Label 7"
    // would mean nothing. Empty while a single mask is rendered.
    std::map<int, QString> m_labelNames;
    bool m_seedCameraFramed = false;
    // Set by the first surface shown: later surfaces keep the camera the user left.
    bool m_surfaceCameraFramed = false;

    // One surface is built at a time, on m_surfaceWorker; the newest volume passed meanwhile
    // waits in m_queuedSurfaceJob and the ones it replaced are never built.
    struct SurfaceJob;
    std::shared_ptr<SurfaceJob> m_surfaceJob;
    std::shared_ptr<SurfaceJob> m_queuedSurfaceJob;
    std::thread m_surfaceWorker;
    int m_surfaceBuildCount = 0;
    void startSurfaceBuild(std::shared_ptr<SurfaceJob> job);
    void finishSurfaceBuild(const std::shared_ptr<SurfaceJob> &job);
    void showSurface(SurfaceJob &job);
    bool m_seedRectEraseEnabled = false;
    bool m_maskVisible = true;
    bool m_seedsVisible = true;
    double m_spacingX = 1.0;
    double m_spacingY = 1.0;
    double m_spacingZ = 1.0;
    // Spacing the shown surface was built with, for its status line.
    std::array<double, 3> m_surfaceSpacing{1.0, 1.0, 1.0};
    // Rendered mask dimensions, used to clamp picked voxels.
    unsigned int m_dimX = 0;
    unsigned int m_dimY = 0;
    unsigned int m_dimZ = 0;
    // Shift+left: a click locates the surface point, a drag past the start distance draws a
    // rectangle instead; which one is known only on release.
    bool m_shiftPickActive = false;
    bool m_shiftDragging = false;
    QPoint m_shiftStart;
    RectangleErase m_shiftEraseScope = RectangleErase::SelectedLabel; // chosen at the press
    ScreenRectangle screenRectangle(const QRect &widgetRect) const;
    bool m_selectingRect = false;
    QPoint m_rectStart;
    QRubberBand *m_selectionBand = nullptr;
};
