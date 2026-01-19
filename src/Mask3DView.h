#pragma once

#include <QWidget>
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
class vtkLookupTable;
class vtkPolyDataMapper;
class vtkRenderer;
class vtkWindowedSincPolyDataFilter;

class Mask3DView : public QWidget
{
    Q_OBJECT
public:
    explicit Mask3DView(QWidget *parent = nullptr);
    void setMaskData(const std::vector<int> &mask, unsigned int sizeX, unsigned int sizeY, unsigned int sizeZ);
    void clearMask();

private slots:
    void onVisibilityToggled(bool checked);
    void onOpacityChanged(int value);
    void onLabelSelectionChanged(int index);
    void onColorButtonClicked();

private:
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
    vtkSmartPointer<vtkPolyDataMapper> m_mapper;
    vtkSmartPointer<vtkFlyingEdges3D> m_flyingEdges;
    vtkSmartPointer<vtkWindowedSincPolyDataFilter> m_smoother;
    vtkSmartPointer<vtkLookupTable> m_lookupTable;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;

    float m_opacity = 0.4f;
    std::vector<int> m_activeLabels;
    std::map<int, QColor> m_labelColors;
};
