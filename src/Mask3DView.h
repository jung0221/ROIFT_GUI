#pragma once

#include <QColor>
#include <QString>
#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <vector>
#include <map>
#include <vtkSmartPointer.h>

#include <array>

QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QSlider)

class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkTextActor;
class vtkCellPicker;
class vtkDiscreteFlyingEdges3D;
class vtkGenericOpenGLRenderWindow;
class vtkGlyph3DMapper;
class vtkLookupTable;
class vtkPolyData;
class vtkPolyDataMapper;
class vtkRenderer;
class vtkStaticCellLocator;
class vtkWindowedSincPolyDataFilter;
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
    /// Render one label volume. With several masks on screen the caller merges
    /// them into one volume of unique ids, and then the label values no longer
    /// carry their own colours or names — pass @p labelColors and @p labelNames
    /// so the surface and the label picker still say which mask each id is.
    void setMaskData(const std::vector<int> &mask,
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
    void setSeedsVisible(bool visible);
    // Set the 3D mask surface opacity, in [0, 1] (clamped). Renders immediately.
    void setMaskOpacity(float opacity);
    float maskOpacity() const { return m_opacity; }
    void clearMask();
    void setSeedRectangleEraseEnabled(bool enabled);
    CameraState captureCameraState() const;
    void restoreCameraState(const CameraState &state, bool render = true);

signals:
    void eraseSeedsInRectangle(const QVector<int> &seedIndices);
    // Shift+click hit the mask surface at this voxel.
    void surfacePointPicked(int x, int y, int z);
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
    void rebuildLookupTable();
    void updateLabelControls();
    void updateColorButtonStyle();
    void setStatusText(const QString &text);

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    QCheckBox *m_visibilityCheck = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QComboBox *m_labelCombo = nullptr;
    QPushButton *m_colorButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkActor> m_seedActor;
    vtkSmartPointer<vtkGlyph3DMapper> m_seedMapper;
    vtkSmartPointer<vtkPolyData> m_seedPolyData;
    vtkSmartPointer<vtkPolyDataMapper> m_mapper;
    vtkSmartPointer<vtkDiscreteFlyingEdges3D> m_flyingEdges;
    vtkSmartPointer<vtkWindowedSincPolyDataFilter> m_smoother;
    vtkSmartPointer<vtkLookupTable> m_lookupTable;
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
    // Above this many labels the surface is the union contoured once and coloured per
    // vertex; below it, one contour per label (touching labels keep their shared wall).
    static constexpr size_t kSurfacePerLabelLimit = 32;
    void paintSurfaceLabels(vtkPolyData *poly, const std::vector<int> &mask, unsigned int sizeX,
                            unsigned int sizeY, unsigned int sizeZ);
    void rebuildHighlightActors();
    NetworkGraph3D::Point toWorld(const NetworkGraph3D::Point &voxel) const;

    // Hover picking: throttled, and against a locator built once per surface,
    // so resting the pointer costs one ray cast, not one per mouse event.
    vtkSmartPointer<vtkStaticCellLocator> m_surfaceLocator;
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
    bool m_seedRectEraseEnabled = false;
    bool m_maskVisible = true;
    bool m_seedsVisible = true;
    double m_spacingX = 1.0;
    double m_spacingY = 1.0;
    double m_spacingZ = 1.0;
    // Rendered mask dimensions, used to clamp picked voxels.
    unsigned int m_dimX = 0;
    unsigned int m_dimY = 0;
    unsigned int m_dimZ = 0;
    bool m_shiftPickActive = false;
    bool m_selectingRect = false;
    QPoint m_rectStart;
    QRubberBand *m_selectionBand = nullptr;
};
