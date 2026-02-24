#include "Mask3DView.h"
#include "ColorUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMouseEvent>
#include <QRubberBand>
#include <QPushButton>
#include <QSlider>
#include <QColorDialog>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkFlyingEdges3D.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkLight.h>
#include <vtkLookupTable.h>
#include <vtkGlyph3DMapper.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>
#include <vtkUnsignedCharArray.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkSmartPointer.h>

#include <set>
#include <algorithm>
#include <cmath>

Mask3DView::Mask3DView(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    setStyleSheet("background-color:#111111; color: #ffffff;");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setMinimumHeight(280);
    layout->addWidget(m_vtkWidget, 1);
    m_vtkWidget->installEventFilter(this);
    m_selectionBand = new QRubberBand(QRubberBand::Rectangle, m_vtkWidget);

    // Controls removed by request: keep the 3D canvas clean and read-only.
    m_visibilityCheck = nullptr;
    m_opacitySlider = nullptr;
    m_labelCombo = nullptr;
    m_colorButton = nullptr;

    m_statusLabel = new QLabel("Nenhuma máscara carregada");
    layout->addWidget(m_statusLabel);

    buildPipeline();
    clearMask();
}

void Mask3DView::buildPipeline()
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow.Get());

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.012, 0.012, 0.012);
    m_renderer->GradientBackgroundOn();

    // Add lights for better visualization
    vtkSmartPointer<vtkLight> keyLight = vtkSmartPointer<vtkLight>::New();
    keyLight->SetLightTypeToSceneLight();
    keyLight->SetColor(1.0, 0.9, 0.8);
    keyLight->SetIntensity(0.7);
    keyLight->SetPosition(1.0, 1.0, 1.0);
    keyLight->SetFocalPoint(0.0, 0.0, 0.0);
    m_renderer->AddLight(keyLight);

    vtkSmartPointer<vtkLight> fillLight = vtkSmartPointer<vtkLight>::New();
    fillLight->SetLightTypeToSceneLight();
    fillLight->SetColor(0.5, 0.6, 0.8);
    fillLight->SetIntensity(0.35);
    fillLight->SetPosition(-1.0, -0.5, 0.5);
    fillLight->SetFocalPoint(0.0, 0.0, 0.0);
    m_renderer->AddLight(fillLight);

    m_renderWindow->SetMultiSamples(0);
    m_renderWindow->AddRenderer(m_renderer);

    m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->GetProperty()->SetInterpolationToPhong();
    m_actor->GetProperty()->SetAmbient(0.35);
    m_actor->GetProperty()->SetDiffuse(0.65);
    m_actor->GetProperty()->SetSpecular(0.5);
    m_actor->GetProperty()->SetSpecularPower(25.0);
    m_actor->GetProperty()->SetOpacity(m_opacity);
    m_renderer->AddActor(m_actor);

    m_seedPolyData = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkSphereSource> seedSphere = vtkSmartPointer<vtkSphereSource>::New();
    seedSphere->SetRadius(1.6);
    seedSphere->SetThetaResolution(10);
    seedSphere->SetPhiResolution(10);

    m_seedMapper = vtkSmartPointer<vtkGlyph3DMapper>::New();
    m_seedMapper->SetInputData(m_seedPolyData);
    m_seedMapper->SetSourceConnection(seedSphere->GetOutputPort());
    m_seedMapper->SetScalarModeToUsePointData();
    m_seedMapper->ScalarVisibilityOn();
    m_seedMapper->SetColorModeToDirectScalars();

    m_seedActor = vtkSmartPointer<vtkActor>::New();
    m_seedActor->SetMapper(m_seedMapper);
    m_seedActor->GetProperty()->SetAmbient(1.0);
    m_seedActor->GetProperty()->SetDiffuse(0.0);
    m_seedActor->GetProperty()->SetSpecular(0.0);
    m_seedActor->PickableOff();
    m_seedActor->VisibilityOff();
    m_renderer->AddActor(m_seedActor);

    m_lookupTable = vtkSmartPointer<vtkLookupTable>::New();
    m_lookupTable->SetNumberOfTableValues(256);
    m_lookupTable->SetRange(0, 255);
    m_lookupTable->Build();

    m_mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_mapper->SetScalarModeToUsePointFieldData();
    m_mapper->SelectColorArray("Scalars");
    m_mapper->ScalarVisibilityOn();
    m_mapper->SetLookupTable(m_lookupTable);
    m_mapper->SetColorModeToMapScalars();
    m_actor->SetMapper(m_mapper);

    // FlyingEdges3D - GPU-accelerated alternative to Marching Cubes
    m_flyingEdges = vtkSmartPointer<vtkFlyingEdges3D>::New();
    m_flyingEdges->SetComputeNormals(true);
    m_flyingEdges->SetComputeScalars(true);
    m_flyingEdges->SetComputeGradients(true);
    m_flyingEdges->InterpolateAttributesOn();

    // WindowedSinc smoother - GPU-friendly, better than Laplacian
    m_smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
    m_smoother->SetNumberOfIterations(15);
    m_smoother->BoundarySmoothingOn();
    m_smoother->FeatureEdgeSmoothingOff();
    m_smoother->SetFeatureAngle(120.0);
    m_smoother->SetPassBand(0.1);
    m_smoother->NonManifoldSmoothingOn();
    m_smoother->NormalizeCoordinatesOn();

    m_smoother->SetInputConnection(m_flyingEdges->GetOutputPort());
    m_mapper->SetInputConnection(m_smoother->GetOutputPort());
}

void Mask3DView::setMaskData(const std::vector<int> &mask,
                             unsigned int sizeX,
                             unsigned int sizeY,
                             unsigned int sizeZ,
                             double spacingX,
                             double spacingY,
                             double spacingZ)
{
    if (mask.empty() || sizeX == 0 || sizeY == 0 || sizeZ == 0)
    {
        clearMask();
        return;
    }

    size_t expected = size_t(sizeX) * size_t(sizeY) * size_t(sizeZ);
    if (mask.size() != expected)
    {
        setStatusText("Máscara 3D inconsistente com as dimensões.");
        m_actor->VisibilityOff();
        if (m_renderWindow)
            m_renderWindow->Render();
        return;
    }

    setVoxelSpacing(spacingX, spacingY, spacingZ);

    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(int(sizeX), int(sizeY), int(sizeZ));
    image->SetSpacing(m_spacingX, m_spacingY, m_spacingZ);
    image->AllocateScalars(VTK_INT, 1);

    int *dst = static_cast<int *>(image->GetScalarPointer());
    for (size_t i = 0; i < expected; ++i)
        dst[i] = mask[i];

    std::set<int> labels;
    for (int value : mask)
    {
        if (value > 0)
            labels.insert(value);
    }
    m_activeLabels.assign(labels.begin(), labels.end());

    if (m_activeLabels.empty())
    {
        setStatusText("Máscara 3D vazia");
        m_actor->VisibilityOff();
        updateLabelControls();
        if (m_renderWindow)
            m_renderWindow->Render();
        return;
    }

    for (int lbl : m_activeLabels)
    {
        if (m_labelColors.find(lbl) == m_labelColors.end())
            m_labelColors[lbl] = colorForLabel(lbl);
    }

    m_flyingEdges->SetInputData(image);
    m_flyingEdges->SetNumberOfContours(static_cast<int>(m_activeLabels.size()));
    for (int i = 0; i < static_cast<int>(m_activeLabels.size()); ++i)
        m_flyingEdges->SetValue(i, m_activeLabels[static_cast<size_t>(i)]);

    m_flyingEdges->Modified();
    m_actor->SetVisibility(m_maskVisible && !m_activeLabels.empty());
    rebuildLookupTable();
    updateLabelControls();
    const double physX = static_cast<double>(sizeX) * m_spacingX;
    const double physY = static_cast<double>(sizeY) * m_spacingY;
    const double physZ = static_cast<double>(sizeZ) * m_spacingZ;
    setStatusText(QString("Labels visíveis: %1 (GPU) | Voxel(mm): %2 x %3 x %4 | Dim(mm): %5 x %6 x %7")
                      .arg(m_activeLabels.size())
                      .arg(m_spacingX, 0, 'f', 3)
                      .arg(m_spacingY, 0, 'f', 3)
                      .arg(m_spacingZ, 0, 'f', 3)
                      .arg(physX, 0, 'f', 1)
                      .arg(physY, 0, 'f', 1)
                      .arg(physZ, 0, 'f', 1));
    m_renderer->ResetCamera();
    m_seedCameraFramed = true;
    if (m_renderWindow)
        m_renderWindow->Render();
}

void Mask3DView::setVoxelSpacing(double spacingX, double spacingY, double spacingZ)
{
    const double newSpacingX = (std::isfinite(spacingX) && spacingX > 0.0) ? spacingX : 1.0;
    const double newSpacingY = (std::isfinite(spacingY) && spacingY > 0.0) ? spacingY : 1.0;
    const double newSpacingZ = (std::isfinite(spacingZ) && spacingZ > 0.0) ? spacingZ : 1.0;
    const bool changed = (newSpacingX != m_spacingX) || (newSpacingY != m_spacingY) || (newSpacingZ != m_spacingZ);

    m_spacingX = newSpacingX;
    m_spacingY = newSpacingY;
    m_spacingZ = newSpacingZ;

    if (changed && !m_seedRenderData.empty())
        setSeedData(m_seedRenderData);
}

void Mask3DView::clearMask()
{
    m_activeLabels.clear();
    m_actor->VisibilityOff();
    updateLabelControls();
    setStatusText("Nenhuma máscara 3D disponível");
    if (m_renderWindow)
        m_renderWindow->Render();
}

void Mask3DView::setSeedData(const std::vector<SeedRenderData> &seeds)
{
    if (!m_seedPolyData || !m_seedActor)
        return;

    m_seedRenderData = seeds;
    const bool hadVisibleSeeds = (m_seedPolyData && m_seedPolyData->GetNumberOfPoints() > 0);

    if (m_seedRenderData.empty())
    {
        m_seedPolyData->Initialize();
        m_seedActor->VisibilityOff();
        m_seedCameraFramed = false;
        if (m_renderWindow)
            m_renderWindow->Render();
        return;
    }

    vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkUnsignedCharArray> colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("SeedColors");
    colors->SetNumberOfComponents(3);
    colors->SetNumberOfTuples(static_cast<vtkIdType>(m_seedRenderData.size()));

    for (vtkIdType i = 0; i < static_cast<vtkIdType>(m_seedRenderData.size()); ++i)
    {
        const SeedRenderData &seed = m_seedRenderData[static_cast<size_t>(i)];
        points->InsertNextPoint(
            static_cast<double>(seed.x) * m_spacingX,
            static_cast<double>(seed.y) * m_spacingY,
            static_cast<double>(seed.z) * m_spacingZ);

        const int label = std::max(0, std::min(255, seed.label));
        const QColor c = colorForLabel(label);
        const unsigned char rgb[3] = {
            static_cast<unsigned char>(c.red()),
            static_cast<unsigned char>(c.green()),
            static_cast<unsigned char>(c.blue())};
        colors->SetTypedTuple(i, rgb);
    }

    m_seedPolyData->SetPoints(points);
    m_seedPolyData->GetPointData()->SetScalars(colors);
    m_seedPolyData->Modified();
    m_seedActor->SetVisibility(m_seedsVisible ? 1 : 0);
    // Auto-frame seeds only once when they become visible without a mask.
    if (!hadVisibleSeeds && !m_seedCameraFramed && m_actor && !m_actor->GetVisibility())
    {
        m_renderer->ResetCamera();
        m_seedCameraFramed = true;
    }

    if (m_renderWindow)
        m_renderWindow->Render();
}

void Mask3DView::setMaskVisible(bool visible)
{
    m_maskVisible = visible;
    if (m_actor)
        m_actor->SetVisibility(m_maskVisible && !m_activeLabels.empty());
    if (m_renderWindow)
        m_renderWindow->Render();
}

void Mask3DView::setSeedsVisible(bool visible)
{
    m_seedsVisible = visible;
    if (m_seedActor)
        m_seedActor->SetVisibility(m_seedsVisible && !m_seedRenderData.empty());
    if (m_renderWindow)
        m_renderWindow->Render();
}

void Mask3DView::setSeedRectangleEraseEnabled(bool enabled)
{
    m_seedRectEraseEnabled = enabled;
    if (!enabled && m_selectionBand)
    {
        m_selectionBand->hide();
        m_selectingRect = false;
    }
}

bool Mask3DView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_vtkWidget || !m_seedRectEraseEnabled || !m_vtkWidget)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            m_selectingRect = true;
            m_rectStart = mouseEvent->pos();
            m_selectionBand->setGeometry(QRect(m_rectStart, QSize()));
            m_selectionBand->show();
            return true;
        }
    }
    else if (event->type() == QEvent::MouseMove)
    {
        if (m_selectingRect)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            m_selectionBand->setGeometry(QRect(m_rectStart, mouseEvent->pos()).normalized());
            return true;
        }
    }
    else if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_selectingRect && mouseEvent->button() == Qt::LeftButton)
        {
            m_selectingRect = false;
            const QRect selectionRect = QRect(m_rectStart, mouseEvent->pos()).normalized();
            if (m_selectionBand)
                m_selectionBand->hide();
            if (selectionRect.width() > 2 && selectionRect.height() > 2)
            {
                // Ensure camera/projection matrices are current before projecting points.
                if (m_renderWindow)
                    m_renderWindow->Render();
                const QVector<int> indices = collectSeedIndicesInRect(selectionRect);
                if (!indices.isEmpty())
                    emit eraseSeedsInRectangle(indices);
            }
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

QVector<int> Mask3DView::collectSeedIndicesInRect(const QRect &rect) const
{
    QVector<int> indices;
    if (!m_renderer || !m_renderWindow || !m_vtkWidget || m_seedRenderData.empty())
        return indices;

    const int widgetWidth = m_vtkWidget->width();
    const int widgetHeight = m_vtkWidget->height();
    const int *renderSize = m_renderWindow->GetSize();
    const int renderWidth = renderSize ? renderSize[0] : 0;
    const int renderHeight = renderSize ? renderSize[1] : 0;
    if (widgetWidth <= 0 || widgetHeight <= 0 || renderWidth <= 0 || renderHeight <= 0)
        return indices;

    // Map Qt widget-space selection to the VTK framebuffer space.
    const double scaleX = static_cast<double>(renderWidth) / static_cast<double>(widgetWidth);
    const double scaleY = static_cast<double>(renderHeight) / static_cast<double>(widgetHeight);
    QRectF rectPx(
        rect.left() * scaleX,
        rect.top() * scaleY,
        rect.width() * scaleX,
        rect.height() * scaleY);
    rectPx = rectPx.normalized();
    rectPx.adjust(-2.0, -2.0, 2.0, 2.0); // small tolerance near the border

    for (const SeedRenderData &seed : m_seedRenderData)
    {
        m_renderer->SetWorldPoint(
            static_cast<double>(seed.x) * m_spacingX,
            static_cast<double>(seed.y) * m_spacingY,
            static_cast<double>(seed.z) * m_spacingZ,
            1.0);
        m_renderer->WorldToDisplay();
        double displayPoint[3] = {0.0, 0.0, 0.0};
        m_renderer->GetDisplayPoint(displayPoint);

        // VTK display origin is bottom-left; Qt selection origin is top-left.
        const double xPx = displayPoint[0];
        const double yTopPx = static_cast<double>(renderHeight - 1) - displayPoint[1];
        if (rectPx.contains(QPointF(xPx, yTopPx)) && seed.seedIndex >= 0)
            indices.push_back(seed.seedIndex);
    }

    return indices;
}

void Mask3DView::rebuildLookupTable()
{
    if (!m_lookupTable)
        return;
    int maxLabel = 0;
    for (int lbl : m_activeLabels)
        maxLabel = std::max(maxLabel, lbl);
    int tableSize = std::max(256, maxLabel + 2);
    m_lookupTable->SetNumberOfTableValues(tableSize);
    m_lookupTable->SetRange(0, tableSize - 1);
    for (int i = 0; i < tableSize; ++i)
        m_lookupTable->SetTableValue(i, 0.0, 0.0, 0.0, 0.0);
    for (const auto &entry : m_labelColors)
    {
        int lbl = entry.first;
        if (lbl < 0 || lbl >= tableSize)
            continue;
        QColor col = entry.second;
        m_lookupTable->SetTableValue(lbl, col.redF(), col.greenF(), col.blueF(), 1.0);
    }
    m_lookupTable->Build();
    if (m_mapper)
        m_mapper->SetScalarRange(0.0, static_cast<double>(tableSize - 1));
}

void Mask3DView::updateLabelControls()
{
    if (!m_labelCombo)
        return;
    m_labelCombo->blockSignals(true);
    m_labelCombo->clear();
    if (m_activeLabels.empty())
    {
        m_labelCombo->setEnabled(false);
        m_colorButton->setEnabled(false);
        m_opacitySlider->setEnabled(false);
    }
    else
    {
        for (int lbl : m_activeLabels)
            m_labelCombo->addItem(QString("Label %1").arg(lbl), lbl);
        m_labelCombo->setEnabled(true);
        m_colorButton->setEnabled(true);
        m_opacitySlider->setEnabled(true);
        m_labelCombo->setCurrentIndex(0);
    }
    m_labelCombo->blockSignals(false);
    updateColorButtonStyle();
}

void Mask3DView::updateColorButtonStyle()
{
    if (!m_colorButton)
        return;
    if (m_activeLabels.empty())
    {
        m_colorButton->setStyleSheet("");
        return;
    }
    int idx = m_labelCombo->currentIndex();
    if (idx < 0)
        return;
    int label = m_labelCombo->itemData(idx).toInt();
    auto it = m_labelColors.find(label);
    QColor color = (it != m_labelColors.end()) ? it->second : QColor(Qt::white);
    QString textColor = (color.valueF() > 0.5f) ? "#000000" : "#ffffff";
    m_colorButton->setStyleSheet(QString("background:%1; color:%2;").arg(color.name()).arg(textColor));
}

void Mask3DView::setStatusText(const QString &text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text);
}

void Mask3DView::onVisibilityToggled(bool checked)
{
    setMaskVisible(checked);
}

void Mask3DView::onOpacityChanged(int value)
{
    m_opacity = float(value) / 100.0f;
    if (m_actor)
        m_actor->GetProperty()->SetOpacity(m_opacity);
    if (m_renderWindow)
        m_renderWindow->Render();
}

void Mask3DView::onLabelSelectionChanged(int index)
{
    Q_UNUSED(index);
    updateColorButtonStyle();
}

void Mask3DView::onColorButtonClicked()
{
    if (m_activeLabels.empty())
        return;
    int idx = m_labelCombo->currentIndex();
    if (idx < 0)
        return;
    int label = m_labelCombo->itemData(idx).toInt();
    auto it = m_labelColors.find(label);
    QColor current = (it != m_labelColors.end()) ? it->second : colorForLabel(label);
    QColor picked = QColorDialog::getColor(current, this, "Selecionar cor da label");
    if (!picked.isValid())
        return;
    m_labelColors[label] = picked;
    rebuildLookupTable();
    updateColorButtonStyle();
    if (m_renderWindow)
        m_renderWindow->Render();
}
