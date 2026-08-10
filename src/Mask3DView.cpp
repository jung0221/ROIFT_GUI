#include "Mask3DView.h"
#include "ColorUtils.h"
#include "Theme.h"

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
#include <vtkCamera.h>
#include <vtkCellPicker.h>
#include <vtkDiscreteFlyingEdges3D.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkLight.h>
#include <vtkLookupTable.h>
#include <vtkGlyph3DMapper.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
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
    // The render surface sits at bezel depth: it is the frame the geometry
    // floats in, not a panel laid on top of one.
    setStyleSheet(QString("Mask3DView { background-color: %1; } QLabel { color: %2; }")
                      .arg(Theme::kBezel)
                      .arg(Theme::kInk2));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setMinimumHeight(280);
    m_vtkWidget->setToolTip("Shift+click the surface to move all three 2D views to that point");
    layout->addWidget(m_vtkWidget, 1);
    m_vtkWidget->installEventFilter(this);
    m_selectionBand = new QRubberBand(QRubberBand::Rectangle, m_vtkWidget);

    // Controls removed by request: keep the 3D canvas clean and read-only.
    m_visibilityCheck = nullptr;
    m_opacitySlider = nullptr;
    m_labelCombo = nullptr;
    m_colorButton = nullptr;

    m_statusLabel = new QLabel("Nenhuma máscara carregada");
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_statusLabel->setMinimumWidth(0);
    m_statusLabel->setWordWrap(false);
    layout->addWidget(m_statusLabel);

    buildPipeline();
    clearMask();
}

void Mask3DView::buildPipeline()
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow.Get());

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    // Match the bezel so the 3D canvas reads as the same surface as the frame
    // around it instead of a darker hole cut into the window.
    const QColor bezel(Theme::kBezel);
    m_renderer->SetBackground(bezel.redF(), bezel.greenF(), bezel.blueF());
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
    // Prefer active point scalars for color mapping. We fallback to cell scalars
    // at runtime if point scalars are not present.
    m_mapper->SetScalarModeToUsePointData();
    m_mapper->ScalarVisibilityOn();
    m_mapper->SetLookupTable(m_lookupTable);
    m_mapper->SetColorModeToMapScalars();
    m_mapper->UseLookupTableScalarRangeOn();
    m_mapper->InterpolateScalarsBeforeMappingOff();
    m_actor->SetMapper(m_mapper);

    // Use the discrete variant so integer labels produce independent
    // categorical surfaces instead of cumulative isovalues.
    m_flyingEdges = vtkSmartPointer<vtkDiscreteFlyingEdges3D>::New();
    m_flyingEdges->SetComputeNormals(true);
    m_flyingEdges->SetComputeScalars(true);
    m_flyingEdges->SetComputeGradients(false);
    m_flyingEdges->InterpolateAttributesOff();

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

    // Ray-cast picker for shift+click locate; mask actor only, ignores opacity.
    m_surfacePicker = vtkSmartPointer<vtkCellPicker>::New();
    m_surfacePicker->SetTolerance(0.0005);
    m_surfacePicker->PickFromListOn();
    m_surfacePicker->AddPickList(m_actor);
}

void Mask3DView::setMaskData(const std::vector<int> &mask,
                             unsigned int sizeX,
                             unsigned int sizeY,
                             unsigned int sizeZ,
                             double spacingX,
                             double spacingY,
                             double spacingZ,
                             const std::map<int, QColor> *labelColors,
                             const std::map<int, QString> *labelNames)
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
    m_dimX = sizeX;
    m_dimY = sizeY;
    m_dimZ = sizeZ;

    // Pad the volume with a one-voxel background (label 0) collar on every
    // face before contouring. Flying-edges only emits a face where a voxel
    // transitions label->background *inside* the array, so a label that runs
    // off the array edge (body truncated by the scan FOV at the top/bottom
    // slice, or a lateral face on a wide patient) is left open -> the hollow
    // "bucket" surface. The collar gives every boundary voxel a background
    // neighbour so the truncated cross-sections get capped watertight.
    const int padX = int(sizeX) + 2;
    const int padY = int(sizeY) + 2;
    const int padZ = int(sizeZ) + 2;

    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(padX, padY, padZ);
    image->SetSpacing(m_spacingX, m_spacingY, m_spacingZ);
    // Shift the origin back one voxel so the original voxels keep their world
    // coordinates and the surface stays registered with the CT and the seeds
    // (both placed at index * spacing with origin 0).
    image->SetOrigin(-m_spacingX, -m_spacingY, -m_spacingZ);
    image->AllocateScalars(VTK_INT, 1);

    int *dst = static_cast<int *>(image->GetScalarPointer());
    std::fill(dst, dst + size_t(padX) * size_t(padY) * size_t(padZ), 0);
    for (unsigned int z = 0; z < sizeZ; ++z)
        for (unsigned int y = 0; y < sizeY; ++y)
        {
            const size_t srcRow = (size_t(z) * sizeY + y) * sizeX;
            const size_t dstRow =
                size_t(padX) * (size_t(padY) * size_t(z + 1) + size_t(y + 1)) + 1;
            for (unsigned int x = 0; x < sizeX; ++x)
                dst[dstRow + x] = mask[srcRow + x];
        }

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

    m_labelNames = labelNames ? *labelNames : std::map<int, QString>();
    for (int lbl : m_activeLabels)
    {
        // A supplied palette owns its ids: they are handed out per rebuild, so
        // a colour remembered from a previous merge would land on another mask.
        if (labelColors)
        {
            auto supplied = labelColors->find(lbl);
            m_labelColors[lbl] = (supplied != labelColors->end()) ? supplied->second : colorForLabel(lbl);
        }
        // Keep anatomy colors stable and high-contrast across updates.
        else if (lbl == 1 || lbl == 2 || lbl == 3)
            m_labelColors[lbl] = colorForLabel(lbl);
        else if (m_labelColors.find(lbl) == m_labelColors.end())
            m_labelColors[lbl] = colorForLabel(lbl);
    }

    m_flyingEdges->SetInputData(image);
    m_flyingEdges->SetNumberOfContours(static_cast<int>(m_activeLabels.size()));
    for (int i = 0; i < static_cast<int>(m_activeLabels.size()); ++i)
        m_flyingEdges->SetValue(i, m_activeLabels[static_cast<size_t>(i)]);

    m_flyingEdges->Modified();
    m_actor->SetVisibility(m_maskVisible && !m_activeLabels.empty());

    // Ensure mapper sees the scalar array emitted by the contour pipeline.
    // Depending on VTK build/filter behavior, scalars may come as point or cell data.
    m_smoother->Update();
    vtkPolyData *poly = m_smoother->GetOutput();
    if (poly && m_mapper)
    {
        vtkDataArray *pointScalars = poly->GetPointData() ? poly->GetPointData()->GetScalars() : nullptr;
        if (!pointScalars && poly->GetPointData())
        {
            vtkDataArray *namedPointScalars = poly->GetPointData()->GetArray("Scalars");
            if (namedPointScalars)
            {
                poly->GetPointData()->SetScalars(namedPointScalars);
                pointScalars = namedPointScalars;
            }
        }

        if (pointScalars)
        {
            m_mapper->SetScalarModeToUsePointData();
            m_mapper->ScalarVisibilityOn();
        }
        else
        {
            vtkDataArray *cellScalars = poly->GetCellData() ? poly->GetCellData()->GetScalars() : nullptr;
            if (cellScalars && poly->GetCellData()->GetArray("Scalars"))
                cellScalars = poly->GetCellData()->GetArray("Scalars");

            if (cellScalars)
            {
                m_mapper->SetScalarModeToUseCellData();
                m_mapper->ScalarVisibilityOn();
            }
            else
            {
                // Last-resort fallback: at least keep geometry visible.
                m_mapper->ScalarVisibilityOff();
                m_actor->GetProperty()->SetColor(0.85, 0.85, 0.85);
            }
        }
    }

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
    m_labelNames.clear();
    m_dimX = m_dimY = m_dimZ = 0;
    m_actor->VisibilityOff();
    updateLabelControls();
    setStatusText("Nenhuma máscara 3D disponível");
    if (m_renderWindow)
        m_renderWindow->Render();
}

Mask3DView::CameraState Mask3DView::captureCameraState() const
{
    CameraState state;
    if (!m_renderer)
        return state;

    vtkCamera *camera = m_renderer->GetActiveCamera();
    if (!camera)
        return state;

    camera->GetPosition(state.position);
    camera->GetFocalPoint(state.focalPoint);
    camera->GetViewUp(state.viewUp);
    camera->GetClippingRange(state.clippingRange);
    state.parallelProjection = camera->GetParallelProjection() ? 1 : 0;
    state.parallelScale = camera->GetParallelScale();
    state.viewAngle = camera->GetViewAngle();
    state.valid = true;
    return state;
}

void Mask3DView::restoreCameraState(const CameraState &state, bool render)
{
    if (!state.valid || !m_renderer)
        return;

    vtkCamera *camera = m_renderer->GetActiveCamera();
    if (!camera)
        return;

    camera->SetPosition(state.position);
    camera->SetFocalPoint(state.focalPoint);
    camera->SetViewUp(state.viewUp);
    camera->SetParallelProjection(state.parallelProjection);
    if (state.parallelProjection)
        camera->SetParallelScale(state.parallelScale);
    else
        camera->SetViewAngle(state.viewAngle);
    camera->SetClippingRange(state.clippingRange);
    m_renderer->ResetCameraClippingRange();

    if (render && m_renderWindow)
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
    if (watched != m_vtkWidget || !m_vtkWidget)
        return QWidget::eventFilter(watched, event);

    // Shift+left-click locates the surface point; the whole press/move/release
    // triple is swallowed so VTK does not read it as a camera pan.
    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        m_shiftPickActive = false; // any new press starts a fresh gesture
        if (mouseEvent->button() == Qt::LeftButton && (mouseEvent->modifiers() & Qt::ShiftModifier))
        {
            m_shiftPickActive = true;
            int vx = 0, vy = 0, vz = 0;
            if (pickSurfaceVoxel(mouseEvent->pos(), vx, vy, vz))
                emit surfacePointPicked(vx, vy, vz);
            return true;
        }
    }
    else if (m_shiftPickActive &&
             (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease))
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        // End on release, or on a move with the button already up (lost release).
        if (event->type() == QEvent::MouseButtonRelease || !(mouseEvent->buttons() & Qt::LeftButton))
        {
            m_shiftPickActive = false;
            if (event->type() == QEvent::MouseMove)
                return QWidget::eventFilter(watched, event);
        }
        return true;
    }

    if (!m_seedRectEraseEnabled)
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

bool Mask3DView::pickSurfaceVoxel(const QPoint &widgetPos, int &vx, int &vy, int &vz)
{
    if (!m_renderer || !m_renderWindow || !m_vtkWidget || !m_surfacePicker)
        return false;
    if (!m_actor || !m_actor->GetVisibility() || m_dimX == 0 || m_dimY == 0 || m_dimZ == 0)
        return false;

    const int widgetWidth = m_vtkWidget->width();
    const int widgetHeight = m_vtkWidget->height();
    const int *renderSize = m_renderWindow->GetSize();
    const int renderWidth = renderSize ? renderSize[0] : 0;
    const int renderHeight = renderSize ? renderSize[1] : 0;
    if (widgetWidth <= 0 || widgetHeight <= 0 || renderWidth <= 0 || renderHeight <= 0)
        return false;

    // Qt widget space (top-left origin) -> VTK framebuffer (bottom-left, HiDPI).
    const double scaleX = static_cast<double>(renderWidth) / static_cast<double>(widgetWidth);
    const double scaleY = static_cast<double>(renderHeight) / static_cast<double>(widgetHeight);
    const double displayX = widgetPos.x() * scaleX;
    const double displayY = static_cast<double>(renderHeight - 1) - widgetPos.y() * scaleY;

    if (m_surfacePicker->Pick(displayX, displayY, 0.0, m_renderer) == 0)
        return false;

    double world[3] = {0.0, 0.0, 0.0};
    m_surfacePicker->GetPickPosition(world);

    // Surface sits at index * spacing (origin 0), so the inverse is a division.
    auto toIndex = [](double coord, double spacing, unsigned int dim) {
        const int idx = static_cast<int>(std::lround(coord / spacing));
        return std::max(0, std::min(static_cast<int>(dim) - 1, idx));
    };
    vx = toIndex(world[0], m_spacingX, m_dimX);
    vy = toIndex(world[1], m_spacingY, m_dimY);
    vz = toIndex(world[2], m_spacingZ, m_dimZ);
    return true;
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
        {
            auto named = m_labelNames.find(lbl);
            m_labelCombo->addItem(named != m_labelNames.end() ? named->second
                                                              : QString("Label %1").arg(lbl),
                                  lbl);
        }
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
    QString textColor = (color.valueF() > 0.5f) ? Theme::kOnAccent : Theme::kInk;
    m_colorButton->setStyleSheet(QString("background: %1; color: %2; border: none; border-radius: %3px;")
                                     .arg(color.name())
                                     .arg(textColor)
                                     .arg(Theme::kRadiusField));
}

void Mask3DView::setStatusText(const QString &text)
{
    if (m_statusLabel)
    {
        m_statusLabel->setText(text);
        m_statusLabel->setToolTip(text);
    }
}

void Mask3DView::onVisibilityToggled(bool checked)
{
    setMaskVisible(checked);
}

void Mask3DView::onOpacityChanged(int value)
{
    setMaskOpacity(float(value) / 100.0f);
}

void Mask3DView::setMaskOpacity(float opacity)
{
    m_opacity = std::min(1.0f, std::max(0.0f, opacity));
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
