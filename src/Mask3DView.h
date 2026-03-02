#pragma once

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <vector>
#include <map>
#include <vtkSmartPointer.h>

QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QSlider)

class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkFlyingEdges3D;
class vtkGenericOpenGLRenderWindow;
class vtkGlyph3DMapper;
class vtkLookupTable;
class vtkPolyData;
class vtkPolyDataMapper;
class vtkRenderer;
class vtkWindowedSincPolyDataFilter;
class QRubberBand;

struct SeedRenderData
{
    int x = 0;
    int y = 0;
    int z = 0;
    int label = 1;
    int seedIndex = -1;
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
    void setMaskData(const std::vector<int> &mask,
                     unsigned int sizeX,
                     unsigned int sizeY,
                     unsigned int sizeZ,
                     double spacingX,
                     double spacingY,
                     double spacingZ);
    void setVoxelSpacing(double spacingX, double spacingY, double spacingZ);
    void setSeedData(const std::vector<SeedRenderData> &seeds);
    void setMaskVisible(bool visible);
    void setSeedsVisible(bool visible);
    void clearMask();
    void setSeedRectangleEraseEnabled(bool enabled);
    CameraState captureCameraState() const;
    void restoreCameraState(const CameraState &state, bool render = true);

signals:
    void eraseSeedsInRectangle(const QVector<int> &seedIndices);

private slots:
    void onVisibilityToggled(bool checked);
    void onOpacityChanged(int value);
    void onLabelSelectionChanged(int index);
    void onColorButtonClicked();

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    QVector<int> collectSeedIndicesInRect(const QRect &rect) const;
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
    vtkSmartPointer<vtkFlyingEdges3D> m_flyingEdges;
    vtkSmartPointer<vtkWindowedSincPolyDataFilter> m_smoother;
    vtkSmartPointer<vtkLookupTable> m_lookupTable;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;

    float m_opacity = 0.4f;
    std::vector<SeedRenderData> m_seedRenderData;
    std::vector<int> m_activeLabels;
    std::map<int, QColor> m_labelColors;
    bool m_seedCameraFramed = false;
    bool m_seedRectEraseEnabled = false;
    bool m_maskVisible = true;
    bool m_seedsVisible = true;
    double m_spacingX = 1.0;
    double m_spacingY = 1.0;
    double m_spacingZ = 1.0;
    bool m_selectingRect = false;
    QPoint m_rectStart;
    QRubberBand *m_selectionBand = nullptr;
};
