#include "Mask3DView.h"
#include "ColorUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QColorDialog>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkDecimatePro.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkLight.h>
#include <vtkLookupTable.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkSmartPointer.h>

#include <set>
#include <algorithm>

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

    auto *controls = new QHBoxLayout();
    m_visibilityCheck = new QCheckBox("Mostrar máscara 3D");
    m_visibilityCheck->setChecked(true);
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setValue(int(m_opacity * 100.0f));
    QLabel *opacityLabel = new QLabel("Opacidade");
    m_labelCombo = new QComboBox();
    m_colorButton = new QPushButton("Cor por label");

    controls->addWidget(m_visibilityCheck);
    controls->addWidget(opacityLabel);
    controls->addWidget(m_opacitySlider);
    controls->addWidget(m_labelCombo);
    controls->addWidget(m_colorButton);
    layout->addLayout(controls);

    m_statusLabel = new QLabel("Nenhuma máscara carregada");
    layout->addWidget(m_statusLabel);

    buildPipeline();
    clearMask();

    connect(m_visibilityCheck, &QCheckBox::toggled, this, &Mask3DView::onVisibilityToggled);
    connect(m_opacitySlider, &QSlider::valueChanged, this, &Mask3DView::onOpacityChanged);
    connect(m_labelCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &Mask3DView::onLabelSelectionChanged);
    connect(m_colorButton, &QPushButton::clicked, this, &Mask3DView::onColorButtonClicked);
}

void Mask3DView::buildPipeline()
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.012, 0.012, 0.012);
    m_renderer->GradientBackgroundOn();
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

    m_marchingCubes = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
    m_marchingCubes->SetComputeNormals(true);
    m_marchingCubes->SetComputeScalars(true);
    m_marchingCubes->SetComputeGradients(true);

    m_decimate = vtkSmartPointer<vtkDecimatePro>::New();
    m_decimate->SetTargetReduction(0.55);
    m_decimate->PreserveTopologyOn();
    m_decimate->SetMaximumError(0.0005);

    m_smoother = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
    m_smoother->SetNumberOfIterations(20);
    m_smoother->SetRelaxationFactor(0.12);
    m_smoother->FeatureEdgeSmoothingOff();
    m_smoother->BoundarySmoothingOn();

    m_decimate->SetInputConnection(m_marchingCubes->GetOutputPort());
    m_smoother->SetInputConnection(m_decimate->GetOutputPort());
    m_mapper->SetInputConnection(m_smoother->GetOutputPort());
}

void Mask3DView::setMaskData(const std::vector<int> &mask, unsigned int sizeX, unsigned int sizeY, unsigned int sizeZ)
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

    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(int(sizeX), int(sizeY), int(sizeZ));
    image->SetSpacing(1.0, 1.0, 1.0);
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

    m_marchingCubes->SetInputData(image);
    m_marchingCubes->SetNumberOfContours(static_cast<int>(m_activeLabels.size()));
    for (int i = 0; i < static_cast<int>(m_activeLabels.size()); ++i)
        m_marchingCubes->SetValue(i, m_activeLabels[static_cast<size_t>(i)]);

    m_marchingCubes->Modified();
    m_actor->SetVisibility(m_visibilityCheck ? m_visibilityCheck->isChecked() : true);
    rebuildLookupTable();
    updateLabelControls();
    setStatusText(QString("Labels visíveis: %1").arg(m_activeLabels.size()));
    m_renderer->ResetCamera();
    if (m_renderWindow)
        m_renderWindow->Render();
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
    if (m_actor)
    {
        m_actor->SetVisibility(checked && !m_activeLabels.empty());
        if (m_renderWindow)
            m_renderWindow->Render();
    }
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
