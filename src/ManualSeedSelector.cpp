/**
 * ManualSeedSelector.cpp - TABBED UI VERSION
 *
 * Implementation of the manual seed selector window for ROIFT segmentation.
 * Uses Qt widgets for GUI and ITK for image I/O.
 *
 * All controls are inline in tabs (NO popup dialogs).
 */

#include "ManualSeedSelector.h"
#include "SegmentationRunner.h"
#include "ColorUtils.h"
#include "Mask3DView.h"
#include "RangeSlider.h"

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSizePolicy>
#include <QApplication>
#include <QCursor>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QPainter>
#include <QInputDialog>
#include <QMessageBox>
#include <QTabWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QKeyEvent>
#include <QFrame>
#include <QToolButton>
#include <QSplitter>
#include <QListWidget>
#include <QTreeWidget>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_set>
#include <filesystem>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIterator.h>
#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>
#include <zlib.h>

ManualSeedSelector::ManualSeedSelector(const std::string &niftiPath, QWidget *parent)
    : QMainWindow(parent), m_path(niftiPath)
{
    setupUi();
    if (!niftiPath.empty())
    {
        if (!m_image.load(niftiPath))
        {
            std::cerr << "Failed to load " << niftiPath << std::endl;
        }
        else
        {
            // Add initial image to the list
            ImageData imgData;
            imgData.imagePath = niftiPath;
            imgData.color = getColorForImageIndex(0);
            m_images.push_back(imgData);

            std::string filename = std::filesystem::path(niftiPath).filename().string();
            m_niftiList->addItem(QString::fromStdString(filename));
            m_niftiList->setCurrentRow(0);
            m_currentImageIndex = 0;

            // Clear any previously loaded mask data when a new image is loaded
            if (!m_maskData.empty())
            {
                std::cerr << "ManualSeedSelector: clearing existing mask buffer due to new image load" << std::endl;
                m_maskData.clear();
                m_mask3DDirty = true;
            }
            // Update slider ranges
            m_axialSlider->setRange(0, static_cast<int>(m_image.getSizeZ()) - 1);
            m_axialSlider->setValue(static_cast<int>(m_image.getSizeZ()) / 2);

            m_sagittalSlider->setRange(0, static_cast<int>(m_image.getSizeX()) - 1);
            m_sagittalSlider->setValue(static_cast<int>(m_image.getSizeX()) / 2);

            m_coronalSlider->setRange(0, static_cast<int>(m_image.getSizeY()) - 1);
            m_coronalSlider->setValue(static_cast<int>(m_image.getSizeY()) / 2);

            // Window/level setup
            float gmin = m_image.getGlobalMin();
            float gmax = m_image.getGlobalMax();
            m_windowGlobalMin = gmin;
            m_windowGlobalMax = gmax;
            if (m_windowSlider)
            {
                m_windowSlider->setRange(static_cast<int>(std::floor(gmin)), static_cast<int>(std::ceil(gmax)));
                m_windowSlider->setLowerValue(static_cast<int>(gmin));
                m_windowSlider->setUpperValue(static_cast<int>(gmax));
            }
            if (m_windowLevelSpin)
            {
                m_windowLevelSpin->setRange(static_cast<double>(gmin), static_cast<double>(gmax));
                m_windowLevelSpin->setValue(0.5 * (gmin + gmax));
            }
            if (m_windowWidthSpin)
            {
                m_windowWidthSpin->setRange(1.0, static_cast<double>(gmax - gmin));
                m_windowWidthSpin->setValue(static_cast<double>(gmax - gmin));
            }
            m_windowLow = gmin;
            m_windowHigh = gmax;

            updateViews();
        }
    }
}

ManualSeedSelector::~ManualSeedSelector() = default;

void ManualSeedSelector::setupUi()
{
    // =====================================================
    // MODERN DARK THEME STYLESHEET
    // =====================================================
    setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #1e1e1e;
            color: #e0e0e0;
            font-family: 'Segoe UI', Arial, sans-serif;
            font-size: 9px;
        }
        QPushButton {
            background-color: #3c3c3c;
            border: 1px solid #555555;
            border-radius: 3px;
            padding: 6px 14px;
            min-width: 110px;
            font-size: 10px;
            color: #ffffff;
        }
        QPushButton:hover {
            background-color: #4a4a4a;
            border-color: #0078d4;
        }
        QPushButton:pressed {
            background-color: #0078d4;
        }
        QPushButton:checked {
            background-color: #0078d4;
            border-color: #0078d4;
        }
        QPushButton:disabled {
            background-color: #2d2d2d;
            color: #666666;
        }
        QGroupBox {
            border: 1px solid #444444;
            border-radius: 4px;
            margin-top: 6px;
            padding: 4px 6px 4px 6px;
            font-weight: bold;
            font-size: 10px;
            color: #ffffff;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
            color: #0078d4;
            font-size: 10px;
        }
        QTabWidget::pane {
            border: 1px solid #444444;
            border-top: none;
            background: #252526;
        }
        QTabBar::tab {
            background: #2d2d2d;
            border: 1px solid #444444;
            border-bottom: none;
            padding: 6px 14px;
            margin-right: 2px;
            min-width: 60px;
            font-size: 11px;
            color: #ffffff;
        }
        QTabBar::tab:selected {
            background: #252526;
            border-bottom: 1px solid #252526;
            color: white;
        }
        QTabBar::tab:hover:!selected {
            background: #3c3c3c;
        }
        QListWidget {
            background-color: #252526;
            border: 1px solid #444444;
            border-radius: 3px;
        }
        QListWidget::item {
            padding: 4px;
        }
        QListWidget::item:selected {
            background-color: #0078d4;
        }
        QListWidget::item:hover:!selected {
            background-color: #3c3c3c;
        }
        QTreeWidget {
            background-color: #252526;
            border: 1px solid #444444;
            border-radius: 3px;
        }
        QTreeWidget::item {
            padding: 3px;
        }
        QTreeWidget::item:selected {
            background-color: #0078d4;
        }
        QTreeWidget::item:hover:!selected {
            background-color: #3c3c3c;
        }
        QMenu {
            background-color: #2d2d2d;
            border: 1px solid #444444;
            padding: 4px 0px;
        }
        QMenu::item {
            padding: 6px 24px;
        }
        QMenu::item:selected {
            background-color: #0078d4;
        }
        QSlider::groove:horizontal {
            border: 1px solid #555555;
            height: 6px;
            background: #2d2d2d;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #0078d4;
            border: 1px solid #0078d4;
            width: 14px;
            margin: -4px 0;
            border-radius: 7px;
        }
        QSlider::handle:horizontal:hover {
            background: #1084d8;
        }
        QSpinBox, QDoubleSpinBox {
            background-color: #2d2d2d;
            border: 1px solid #555555;
            border-radius: 3px;
            padding: 4px;
            font-size: 10px;
            color: #ffffff;
        }
        QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: #0078d4;
        }
        QCheckBox {
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
            border: 1px solid #555555;
            background-color: #2d2d2d;
        }
        QCheckBox::indicator:checked {
            background-color: #0078d4;
            border-color: #0078d4;
        }
        QLabel {
            color: #c0c0c0;
            font-size: 10px;
        }
        QToolTip {
            background-color: #2d2d2d;
            color: #e0e0e0;
            border: 1px solid #555555;
            padding: 4px;
            border-radius: 3px;
        }
    )");

    setWindowTitle("ROIFT GUI");
    resize(1400, 950);

    QWidget *central = new QWidget();
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    QTabWidget *ribbon = new QTabWidget();
    ribbon->setMaximumHeight(120);

    QWidget *filesTab = new QWidget();
    QHBoxLayout *filesLayout = new QHBoxLayout(filesTab);
    filesLayout->setSpacing(10);
    filesLayout->setContentsMargins(8, 6, 8, 6);

    // Grupo de operações NIfTI - Abrir e salvar imagens médicas
    QGroupBox *niftiGroup = new QGroupBox("NIfTI Image");
    QHBoxLayout *niftiLayout = new QHBoxLayout(niftiGroup);
    niftiLayout->setSpacing(8);

    QPushButton *btnOpen = new QPushButton("Open");
    btnOpen->setToolTip("Open a NIfTI image (Ctrl+O)");
    btnOpen->setShortcut(QKeySequence("Ctrl+O"));
    connect(btnOpen, &QPushButton::clicked, this, &ManualSeedSelector::openImage);
    niftiLayout->addWidget(btnOpen);

    QPushButton *btnSave = new QPushButton("Save");
    btnSave->setToolTip("Save current image as NIfTI");
    connect(btnSave, &QPushButton::clicked, this, [this]()
            {
        QString f = QFileDialog::getSaveFileName(this, "Save NIfTI", "", "NIfTI files (*.nii *.nii.gz)");
        if (!f.isEmpty() && !saveImageToFile(f.toStdString()))
            QMessageBox::warning(this, "Save NIfTI", "Failed to save image."); });
    niftiLayout->addWidget(btnSave);

    filesLayout->addWidget(niftiGroup);
    filesLayout->addStretch();

    ribbon->addTab(filesTab, "Files");

    // --- TAB 1: VIEW (Navigation & Window/Level) ---
    QWidget *sliderTab = new QWidget();
    QHBoxLayout *sliderLayout = new QHBoxLayout(sliderTab);
    sliderLayout->setSpacing(10);
    sliderLayout->setContentsMargins(8, 6, 8, 6);

    // Slice Navigation group
    QGroupBox *sliceGroup = new QGroupBox("Slice Navigation");
    QGridLayout *sliceGrid = new QGridLayout(sliceGroup);
    sliceGrid->setSpacing(4);

    m_axialSlider = new QSlider(Qt::Horizontal);
    m_axialSlider->setToolTip("Axial slice (W/S keys)");
    m_sagittalSlider = new QSlider(Qt::Horizontal);
    m_sagittalSlider->setToolTip("Sagittal slice (A/D keys)");
    m_coronalSlider = new QSlider(Qt::Horizontal);
    m_coronalSlider->setToolTip("Coronal slice (Q/E keys)");

    m_axialLabel = new QLabel("Axial: 0/0");
    m_sagittalLabel = new QLabel("Sagittal: 0/0");
    m_coronalLabel = new QLabel("Coronal: 0/0");

    sliceGrid->addWidget(m_axialLabel, 0, 0);
    sliceGrid->addWidget(m_axialSlider, 0, 1);
    sliceGrid->addWidget(m_sagittalLabel, 1, 0);
    sliceGrid->addWidget(m_sagittalSlider, 1, 1);
    sliceGrid->addWidget(m_coronalLabel, 2, 0);
    sliceGrid->addWidget(m_coronalSlider, 2, 1);
    sliceGrid->setColumnStretch(1, 1);
    sliderLayout->addWidget(sliceGroup, 1);

    // Window/Level group
    QGroupBox *windowGroup = new QGroupBox("Window/Level");
    QGridLayout *windowGrid = new QGridLayout(windowGroup);
    windowGrid->setSpacing(4);

    m_windowSlider = new RangeSlider(Qt::Horizontal);
    m_windowSlider->setToolTip("Drag handles to adjust brightness/contrast");
    m_windowSlider->setMinimumHeight(28);
    windowGrid->addWidget(m_windowSlider, 0, 0, 1, 4);

    windowGrid->addWidget(new QLabel("WL:"), 1, 0);
    m_windowLevelSpin = new QDoubleSpinBox();
    m_windowLevelSpin->setDecimals(1);
    m_windowLevelSpin->setSingleStep(10.0);
    m_windowLevelSpin->setToolTip("Window Level");
    windowGrid->addWidget(m_windowLevelSpin, 1, 1);

    windowGrid->addWidget(new QLabel("WW:"), 1, 2);
    m_windowWidthSpin = new QDoubleSpinBox();
    m_windowWidthSpin->setDecimals(1);
    m_windowWidthSpin->setSingleStep(10.0);
    m_windowWidthSpin->setToolTip("Window Width");
    windowGrid->addWidget(m_windowWidthSpin, 1, 3);

    sliderLayout->addWidget(windowGroup, 1);

    // View controls group
    QGroupBox *viewGroup = new QGroupBox("View");
    QHBoxLayout *viewLayout = new QHBoxLayout(viewGroup);

    QPushButton *btnResetWindow = new QPushButton("Reset WL");
    btnResetWindow->setToolTip("Reset window to full range");
    connect(btnResetWindow, &QPushButton::clicked, this, &ManualSeedSelector::resetWindowToFullRange);
    viewLayout->addWidget(btnResetWindow);

    QPushButton *btnResetZoom = new QPushButton("Reset Zoom");
    btnResetZoom->setToolTip("Reset all views to default zoom (Ctrl+R)");
    btnResetZoom->setShortcut(QKeySequence("Ctrl+R"));
    connect(btnResetZoom, &QPushButton::clicked, [this]()
            {
        m_axialView->resetView();
        m_sagittalView->resetView();
        m_coronalView->resetView(); });
    viewLayout->addWidget(btnResetZoom);

    sliderLayout->addWidget(viewGroup);

    ribbon->addTab(sliderTab, "View");

    // --- TAB 2: SEEDS ---
    QWidget *seedsTab = new QWidget();
    QHBoxLayout *seedsLayout = new QHBoxLayout(seedsTab);
    seedsLayout->setSpacing(10);
    seedsLayout->setContentsMargins(8, 6, 8, 6);

    // Mode group
    QGroupBox *seedModeGroup = new QGroupBox("Drawing Mode");
    QHBoxLayout *seedModeLayout = new QHBoxLayout(seedModeGroup);

    m_btnSeedDraw = new QPushButton("Draw");
    m_btnSeedDraw->setCheckable(true);
    m_btnSeedDraw->setChecked(true);
    m_btnSeedDraw->setToolTip("Draw seed points (left click)");
    seedModeLayout->addWidget(m_btnSeedDraw);

    m_btnSeedErase = new QPushButton("Erase");
    m_btnSeedErase->setCheckable(true);
    m_btnSeedErase->setToolTip("Erase seed points (left click or right click)");
    seedModeLayout->addWidget(m_btnSeedErase);

    QButtonGroup *seedModeButtons = new QButtonGroup(this);
    seedModeButtons->addButton(m_btnSeedDraw, 1);  // mode 1 = draw
    seedModeButtons->addButton(m_btnSeedErase, 2); // mode 2 = erase
    connect(seedModeButtons, QOverload<int>::of(&QButtonGroup::idClicked), [this](int id)
            { m_seedMode = id; });
    m_seedMode = 1; // default: draw

    seedsLayout->addWidget(seedModeGroup);

    // Brush group
    QGroupBox *seedBrushGroup = new QGroupBox("Brush");
    QGridLayout *seedBrushLayout = new QGridLayout(seedBrushGroup);
    seedBrushLayout->setSpacing(4);

    seedBrushLayout->addWidget(new QLabel("Radius:"), 0, 0);
    m_seedBrushSpin = new QSpinBox();
    m_seedBrushSpin->setRange(1, 50);
    m_seedBrushSpin->setValue(3);
    m_seedBrushSpin->setToolTip("Seed brush radius for erasing");
    connect(m_seedBrushSpin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int r)
            { m_seedBrushRadius = r; });
    seedBrushLayout->addWidget(m_seedBrushSpin, 0, 1);

    seedsLayout->addWidget(seedBrushGroup);

    // File operations group
    QGroupBox *seedFileGroup = new QGroupBox("File");
    QHBoxLayout *seedFileLayout = new QHBoxLayout(seedFileGroup);

    QPushButton *btnSeedSave = new QPushButton("Save");
    btnSeedSave->setToolTip("Save seeds to file");
    connect(btnSeedSave, &QPushButton::clicked, this, &ManualSeedSelector::saveSeeds);
    seedFileLayout->addWidget(btnSeedSave);

    QPushButton *btnSeedLoad = new QPushButton("Load");
    btnSeedLoad->setToolTip("Load seeds from file");
    connect(btnSeedLoad, &QPushButton::clicked, this, &ManualSeedSelector::loadSeeds);
    seedFileLayout->addWidget(btnSeedLoad);

    QPushButton *btnSeedClear = new QPushButton("Clear");
    btnSeedClear->setToolTip("Clear all seeds");
    connect(btnSeedClear, &QPushButton::clicked, [this]()
            {
        m_seeds.clear();
        updateViews(); });
    seedFileLayout->addWidget(btnSeedClear);

    seedsLayout->addWidget(seedFileGroup);
    seedsLayout->addStretch();

    ribbon->addTab(seedsTab, "Seeds");

    // --- TAB 3: MASK ---
    QWidget *maskTab = new QWidget();
    QHBoxLayout *maskLayout = new QHBoxLayout(maskTab);
    maskLayout->setSpacing(10);
    maskLayout->setContentsMargins(8, 6, 8, 6);

    // Mode group
    QGroupBox *maskModeGroup = new QGroupBox("Drawing Mode");
    QHBoxLayout *maskModeLayout = new QHBoxLayout(maskModeGroup);

    m_btnMaskDraw = new QPushButton("Draw");
    m_btnMaskDraw->setCheckable(true);
    m_btnMaskDraw->setToolTip("Draw mask regions");
    maskModeLayout->addWidget(m_btnMaskDraw);

    m_btnMaskErase = new QPushButton("Erase");
    m_btnMaskErase->setCheckable(true);
    m_btnMaskErase->setToolTip("Erase mask regions");
    maskModeLayout->addWidget(m_btnMaskErase);

    QPushButton *btnMaskOff = new QPushButton("Off");
    btnMaskOff->setCheckable(true);
    btnMaskOff->setChecked(true);
    btnMaskOff->setToolTip("Disable mask editing");
    maskModeLayout->addWidget(btnMaskOff);

    QButtonGroup *maskModeButtons = new QButtonGroup(this);
    maskModeButtons->addButton(btnMaskOff, 0);     // mode 0 = off
    maskModeButtons->addButton(m_btnMaskDraw, 1);  // mode 1 = draw
    maskModeButtons->addButton(m_btnMaskErase, 2); // mode 2 = erase
    connect(maskModeButtons, QOverload<int>::of(&QButtonGroup::idClicked), [this](int id)
            { setMaskMode(id); });

    maskLayout->addWidget(maskModeGroup);

    // Brush group
    QGroupBox *maskBrushGroup = new QGroupBox("Brush & Opacity");
    QGridLayout *maskBrushLayout = new QGridLayout(maskBrushGroup);
    maskBrushLayout->setSpacing(4);
    maskBrushLayout->setContentsMargins(8, 6, 8, 6);
    maskBrushLayout->setVerticalSpacing(6);

    maskBrushLayout->addWidget(new QLabel("Radius:"), 0, 0);
    m_maskBrushSpin = new QSlider(Qt::Horizontal);
    m_maskBrushSpin->setRange(1, 50);
    m_maskBrushSpin->setValue(5);
    m_maskBrushSpin->setSingleStep(1);
    m_maskBrushSpin->setPageStep(10);
    m_maskBrushSpin->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Fixed);
    m_maskBrushSpin->setToolTip("Mask brush radius");

    QLabel *radiusValue = new QLabel("5");
    radiusValue->setMinimumWidth(36);
    radiusValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    connect(m_maskBrushSpin, &QSlider::valueChanged, [this, radiusValue](int r)
            {
        m_maskBrushRadius = r;
    radiusValue->setText(QString("%1").arg(r)); });
    maskBrushLayout->addWidget(m_maskBrushSpin, 0, 1);
    maskBrushLayout->addWidget(radiusValue, 0, 2);

    maskBrushLayout->addWidget(new QLabel("Opacity:"), 1, 0);
    m_maskOpacitySlider = new QSlider(Qt::Horizontal);
    m_maskOpacitySlider->setRange(0, 100);
    m_maskOpacitySlider->setValue(50);
    m_maskOpacitySlider->setSingleStep(1);
    m_maskOpacitySlider->setPageStep(10);
    m_maskOpacitySlider->setSizePolicy(QSizePolicy::Expanding,
                                       QSizePolicy::Fixed);
    m_maskOpacitySlider->setToolTip("Mask overlay opacity");

    QLabel *opacityValue = new QLabel("50%");
    opacityValue->setMinimumWidth(36);
    opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    connect(m_maskOpacitySlider, &QSlider::valueChanged, [this, opacityValue](int v)
            {
        m_maskOpacity = float(v) / 100.0f;
        opacityValue->setText(QString("%1%").arg(v));
        updateViews(); });
    maskBrushLayout->addWidget(m_maskOpacitySlider, 1, 1);
    maskBrushLayout->addWidget(opacityValue, 1, 2);

    maskBrushLayout->addWidget(new QLabel("Show 3D:"), 2, 0);
    QCheckBox *chkShow3D = new QCheckBox("Show 3D mask");
    chkShow3D->setToolTip("Enable 3D mask visualization (may slow down drawing)");
    chkShow3D->setChecked(false);
    connect(chkShow3D, &QCheckBox::toggled, [this](bool checked)
            {
        m_enable3DView = checked;
        if (checked && m_mask3DDirty) {
            update3DMaskView();
            m_mask3DDirty = false;
        } });
    maskBrushLayout->addWidget(chkShow3D, 2, 0, 1, 3);

    maskLayout->addWidget(maskBrushGroup);

    // File operations group
    QGroupBox *maskFileGroup = new QGroupBox("File");
    QHBoxLayout *maskFileLayout = new QHBoxLayout(maskFileGroup);

    QPushButton *btnMaskSave = new QPushButton("Save");
    btnMaskSave->setToolTip("Save mask to NIfTI");
    connect(btnMaskSave, &QPushButton::clicked, [this]()
            {
        QString f = QFileDialog::getSaveFileName(this, "Save Mask", "", "NIfTI files (*.nii *.nii.gz)");
        if (!f.isEmpty())
            saveMaskToFile(f.toStdString()); });
    maskFileLayout->addWidget(btnMaskSave);

    QPushButton *btnMaskLoad = new QPushButton("Load");
    btnMaskLoad->setToolTip("Load mask from NIfTI");
    connect(btnMaskLoad, &QPushButton::clicked, [this]()
            {
        QString f = QFileDialog::getOpenFileName(this, "Open Mask", "", "NIfTI files (*.nii *.nii.gz)");
        if (!f.isEmpty()) {
            loadMaskFromFile(f.toStdString());
            updateViews();
        } });
    maskFileLayout->addWidget(btnMaskLoad);

    QPushButton *btnMaskClear = new QPushButton("Clear");
    btnMaskClear->setToolTip("Clear mask");
    connect(btnMaskClear, &QPushButton::clicked, [this]()
            {
        cleanMask();
        updateViews(); });
    maskFileLayout->addWidget(btnMaskClear);

    maskLayout->addWidget(maskFileGroup);
    maskLayout->addStretch();

    ribbon->addTab(maskTab, "Mask");

    // --- TAB 4: SEGMENTATION ---
    QWidget *segTab = new QWidget();
    QHBoxLayout *segLayout = new QHBoxLayout(segTab);
    segLayout->setSpacing(10);
    segLayout->setContentsMargins(8, 6, 8, 6);

    // Parameters group
    QGroupBox *paramsGroup = new QGroupBox("Parameters");
    QGridLayout *paramsGrid = new QGridLayout(paramsGroup);
    paramsGrid->setSpacing(4);

    paramsGrid->addWidget(new QLabel("Polarity:"), 0, 0);
    m_polSlider = new QSlider(Qt::Horizontal);
    m_polSlider->setRange(-100, 100);
    m_polSlider->setValue(100);
    m_polSlider->setToolTip("+1.0=bright inside, -1.0=dark inside");
    paramsGrid->addWidget(m_polSlider, 0, 1);
    m_polValue = new QLabel("1.00");
    m_polValue->setMinimumWidth(40);
    paramsGrid->addWidget(m_polValue, 0, 2);

    paramsGrid->addWidget(new QLabel("Relax iters:"), 1, 0);
    m_niterSlider = new QSlider(Qt::Horizontal);
    m_niterSlider->setRange(0, 100);
    m_niterSlider->setValue(0);
    m_niterSlider->setToolTip("Relaxation iterations");
    paramsGrid->addWidget(m_niterSlider, 1, 1);
    m_niterValue = new QLabel("0");
    m_niterValue->setMinimumWidth(40);
    paramsGrid->addWidget(m_niterValue, 1, 2);

    paramsGrid->addWidget(new QLabel("Percentile:"), 2, 0);
    m_percSlider = new QSlider(Qt::Horizontal);
    m_percSlider->setRange(0, 100);
    m_percSlider->setValue(0);
    m_percSlider->setToolTip("Arc-weight percentile threshold");
    paramsGrid->addWidget(m_percSlider, 2, 1);
    m_percValue = new QLabel("0");
    m_percValue->setMinimumWidth(40);
    paramsGrid->addWidget(m_percValue, 2, 2);

    connect(m_polSlider, &QSlider::valueChanged, [this](int v)
            { m_polValue->setText(QString::number(v / 100.0, 'f', 2)); });
    connect(m_niterSlider, &QSlider::valueChanged, [this](int v)
            { m_niterValue->setText(QString::number(v)); });
    connect(m_percSlider, &QSlider::valueChanged, [this](int v)
            { m_percValue->setText(QString::number(v)); });

    segLayout->addWidget(paramsGroup);

    // Options group
    QGroupBox *optionsGroup = new QGroupBox("Batch Options");
    QVBoxLayout *optionsLayout = new QVBoxLayout(optionsGroup);
    optionsLayout->setSpacing(4);

    m_segmentAllBox = new QCheckBox("Segment all labels");
    m_segmentAllBox->setToolTip("Process all seed labels in batch");
    optionsLayout->addWidget(m_segmentAllBox);

    m_polSweepBox = new QCheckBox("Polarity sweep");
    m_polSweepBox->setToolTip("Test polarity range -1.0 to +1.0");
    optionsLayout->addWidget(m_polSweepBox);

    m_useGPUBox = new QCheckBox("Use GPU");
    m_useGPUBox->setToolTip("Use GPU acceleration");
    optionsLayout->addWidget(m_useGPUBox);

    connect(m_segmentAllBox, &QCheckBox::toggled, [this](bool on)
            {
        m_polSweepBox->setChecked(false);
        m_polSweepBox->setEnabled(!on); });

    segLayout->addWidget(optionsGroup);

    // Run button
    QGroupBox *runGroup = new QGroupBox("Execute");
    QVBoxLayout *runLayout = new QVBoxLayout(runGroup);

    QPushButton *btnRunSegment = new QPushButton("Run");
    btnRunSegment->setToolTip("Start ROIFT segmentation (Ctrl+Shift+S)");
    btnRunSegment->setShortcut(QKeySequence("Ctrl+Shift+S"));
    btnRunSegment->setStyleSheet(R"(
        QPushButton {
            background-color: #0e639c;
            color: white;
            font-weight: bold;
            padding: 8px 16px;
        }
        QPushButton:hover {
            background-color: #1177bb;
        }
        QPushButton:pressed {
            background-color: #0d5a8a;
        }
    )");
    connect(btnRunSegment, &QPushButton::clicked, [this]()
            { SegmentationRunner::runSegmentation(this); });
    runLayout->addWidget(btnRunSegment);

    segLayout->addWidget(runGroup);
    segLayout->addStretch();

    ribbon->addTab(segTab, "Segmentation");

    mainLayout->addWidget(ribbon);

    // =====================================================
    // MAIN CONTENT: View Grid + Right Sidebar
    // =====================================================
    QHBoxLayout *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(6);

    // =====================================================
    // CENTER: 2x2 View Grid
    // =====================================================
    QGridLayout *viewGrid = new QGridLayout();
    m_axialView = new OrthogonalView();
    m_sagittalView = new OrthogonalView();
    m_coronalView = new OrthogonalView();
    m_mask3DView = new Mask3DView();

    for (OrthogonalView *view : {m_axialView, m_sagittalView, m_coronalView})
    {
        view->setFocusPolicy(Qt::StrongFocus);
        view->installEventFilter(this);
    }

    m_axialView->setMinimumSize(360, 280);
    m_sagittalView->setMinimumSize(320, 280);
    m_coronalView->setMinimumSize(320, 280);
    m_mask3DView->setMinimumSize(320, 240);

    viewGrid->addWidget(m_axialView, 0, 0);
    viewGrid->addWidget(m_sagittalView, 0, 1);
    viewGrid->addWidget(m_coronalView, 1, 0);
    viewGrid->addWidget(m_mask3DView, 1, 1);
    viewGrid->setColumnStretch(0, 1);
    viewGrid->setColumnStretch(1, 1);
    viewGrid->setRowStretch(0, 1);
    viewGrid->setRowStretch(1, 1);
    viewGrid->setSpacing(6);

    QWidget *viewContainer = new QWidget();
    viewContainer->setLayout(viewGrid);
    viewContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout->addWidget(viewContainer, 1);

    // =====================================================
    // RIGHT SIDEBAR: File Management
    // =====================================================
    QWidget *sidebar = new QWidget();
    sidebar->setMaximumWidth(250);
    sidebar->setMinimumWidth(200);
    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setSpacing(8);
    sidebarLayout->setContentsMargins(4, 4, 4, 4);

    // Label selector at top
    QGroupBox *labelGroup = new QGroupBox("Label");
    QHBoxLayout *labelLayout = new QHBoxLayout(labelGroup);
    labelLayout->setSpacing(6);

    m_labelSelector = new QSpinBox();
    m_labelSelector->setRange(1, 255);
    m_labelSelector->setToolTip("Select label (1-255) for seeds and mask");
    labelLayout->addWidget(m_labelSelector);

    m_labelColorIndicator = new QLabel();
    m_labelColorIndicator->setFixedSize(24, 24);
    m_labelColorIndicator->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_labelColorIndicator->setToolTip("Color for current label");
    updateLabelColor(1);
    labelLayout->addWidget(m_labelColorIndicator);

    connect(m_labelSelector, QOverload<int>::of(&QSpinBox::valueChanged), this, &ManualSeedSelector::updateLabelColor);

    sidebarLayout->addWidget(labelGroup);

    // NIfTI Images section
    QGroupBox *niftiListGroup = new QGroupBox("NIfTI Images");
    QVBoxLayout *niftiListLayout = new QVBoxLayout(niftiListGroup);
    niftiListLayout->setSpacing(4);

    m_niftiList = new QListWidget();
    m_niftiList->setMaximumHeight(80);
    m_niftiList->setToolTip("Click to select which image to display");
    niftiListLayout->addWidget(m_niftiList);

    QHBoxLayout *niftiButtonsLayout = new QHBoxLayout();
    QPushButton *btnAddNifti = new QPushButton("Add");
    btnAddNifti->setToolTip("Add NIfTI images to the list");
    btnAddNifti->setMaximumWidth(60);
    connect(btnAddNifti, &QPushButton::clicked, this, &ManualSeedSelector::openImage);

    QPushButton *btnRemoveNifti = new QPushButton("Remove");
    btnRemoveNifti->setToolTip("Remove selected NIfTI image from list");
    btnRemoveNifti->setMaximumWidth(60);
    connect(btnRemoveNifti, &QPushButton::clicked, [this]()
            {
        int currentRow = m_niftiList->currentRow();
        if (currentRow >= 0 && currentRow < static_cast<int>(m_images.size())) {
            m_images.erase(m_images.begin() + currentRow);
            delete m_niftiList->takeItem(currentRow);
            if (currentRow == m_currentImageIndex) {
                m_currentImageIndex = -1;
                m_image = NiftiImage();
                m_path.clear();
                updateMaskSeedLists();
                updateViews();
            } else if (m_currentImageIndex > currentRow) {
                m_currentImageIndex--;
            }
        } });

    niftiButtonsLayout->addWidget(btnAddNifti);
    niftiButtonsLayout->addWidget(btnRemoveNifti);
    niftiButtonsLayout->addStretch();
    niftiListLayout->addLayout(niftiButtonsLayout);

    // Connect item selection to load the image
    connect(m_niftiList, &QListWidget::currentRowChanged, [this](int row)
            {
        if (row >= 0 && row < static_cast<int>(m_images.size())) {
            m_currentImageIndex = row;
            const std::string &path = m_images[row].imagePath;
            if (m_image.load(path)) {
                m_path = path;
                
                // Clear mask and seed data when switching images
                m_maskData.clear();
                m_seeds.clear();
                m_mask3DDirty = true;
                
                // Update slider ranges
                m_axialSlider->setRange(0, static_cast<int>(m_image.getSizeZ()) - 1);
                m_axialSlider->setValue(static_cast<int>(m_image.getSizeZ()) / 2);
                m_sagittalSlider->setRange(0, static_cast<int>(m_image.getSizeX()) - 1);
                m_sagittalSlider->setValue(static_cast<int>(m_image.getSizeX()) / 2);
                m_coronalSlider->setRange(0, static_cast<int>(m_image.getSizeY()) - 1);
                m_coronalSlider->setValue(static_cast<int>(m_image.getSizeY()) / 2);
                
                // Window/level setup
                float gmin = m_image.getGlobalMin();
                float gmax = m_image.getGlobalMax();
                m_windowGlobalMin = gmin;
                m_windowGlobalMax = gmax;
                if (m_windowSlider) {
                    m_windowSlider->setRange(static_cast<int>(std::floor(gmin)), static_cast<int>(std::ceil(gmax)));
                    m_windowSlider->setLowerValue(static_cast<int>(gmin));
                    m_windowSlider->setUpperValue(static_cast<int>(gmax));
                }
                if (m_windowLevelSpin) {
                    m_windowLevelSpin->setRange(static_cast<double>(gmin), static_cast<double>(gmax));
                    m_windowLevelSpin->setValue(0.5 * (gmin + gmax));
                }
                if (m_windowWidthSpin) {
                    m_windowWidthSpin->setRange(1.0, static_cast<double>(gmax - gmin));
                    m_windowWidthSpin->setValue(static_cast<double>(gmax - gmin));
                }
                m_windowLow = gmin;
                m_windowHigh = gmax;
                
                // Update mask and seed lists for this image
                updateMaskSeedLists();
                updateViews();
                m_statusLabel->setText(QString("Loaded: %1").arg(QString::fromStdString(path)));
            }
        } });

    sidebarLayout->addWidget(niftiListGroup);

    // Masks section
    QGroupBox *maskListGroup = new QGroupBox("Masks");
    QVBoxLayout *maskListLayout = new QVBoxLayout(maskListGroup);
    maskListLayout->setSpacing(4);

    m_maskList = new QListWidget();
    m_maskList->setMaximumHeight(80);
    m_maskList->setToolTip("Click to load mask - Colors indicate which image they belong to");
    maskListLayout->addWidget(m_maskList);

    QPushButton *btnLoadMask = new QPushButton("Load Mask for Current Image");
    btnLoadMask->setToolTip("Load a mask file and associate it with the current image");
    connect(btnLoadMask, &QPushButton::clicked, [this]()
            {
        if (m_currentImageIndex < 0) {
            QMessageBox::warning(this, "Load Mask", "Please select an image first.");
            return;
        }
        QStringList files = QFileDialog::getOpenFileNames(this, "Open Masks", "", "NIfTI files (*.nii *.nii.gz)");
        for (const QString &f : files) {
            std::string path = f.toStdString();
            m_images[m_currentImageIndex].maskPaths.push_back(path);
        }
        updateMaskSeedLists(); });
    maskListLayout->addWidget(btnLoadMask);

    // Connect item selection to load the mask
    connect(m_maskList, &QListWidget::itemClicked, [this](QListWidgetItem *item)
            {
        if (!item || m_currentImageIndex < 0) return;
        
        // Find which mask was clicked
        for (size_t i = 0; i < m_images[m_currentImageIndex].maskPaths.size(); ++i) {
            std::string filename = std::filesystem::path(m_images[m_currentImageIndex].maskPaths[i]).filename().string();
            if (item->text().toStdString() == filename) {
                if (loadMaskFromFile(m_images[m_currentImageIndex].maskPaths[i])) {
                    updateViews();
                    m_statusLabel->setText(QString("Loaded mask: %1").arg(item->text()));
                }
                break;
            }
        } });

    sidebarLayout->addWidget(maskListGroup);

    // Seed Groups section
    QGroupBox *seedListGroup = new QGroupBox("Seed Groups");
    QVBoxLayout *seedListLayout = new QVBoxLayout(seedListGroup);
    seedListLayout->setSpacing(4);

    m_seedList = new QListWidget();
    m_seedList->setMaximumHeight(80);
    m_seedList->setToolTip("Click to load seeds - Colors indicate which image they belong to");
    seedListLayout->addWidget(m_seedList);

    QPushButton *btnLoadSeeds = new QPushButton("Load Seeds for Current Image");
    btnLoadSeeds->setToolTip("Load seed files and associate them with the current image");
    connect(btnLoadSeeds, &QPushButton::clicked, [this]()
            {
        if (m_currentImageIndex < 0) {
            QMessageBox::warning(this, "Load Seeds", "Please select an image first.");
            return;
        }
        QStringList files = QFileDialog::getOpenFileNames(this, "Open Seeds", "", "Text files (*.txt)");
        for (const QString &f : files) {
            std::string path = f.toStdString();
            m_images[m_currentImageIndex].seedPaths.push_back(path);
        }
        updateMaskSeedLists(); });
    seedListLayout->addWidget(btnLoadSeeds);

    // Connect item selection to load the seeds
    connect(m_seedList, &QListWidget::itemClicked, [this](QListWidgetItem *item)
            {
        if (!item || m_currentImageIndex < 0) return;
        
        // Find which seed was clicked
        for (size_t i = 0; i < m_images[m_currentImageIndex].seedPaths.size(); ++i) {
            std::string filename = std::filesystem::path(m_images[m_currentImageIndex].seedPaths[i]).filename().string();
            if (item->text().toStdString() == filename) {
                if (loadSeedsFromFile(m_images[m_currentImageIndex].seedPaths[i])) {
                    updateViews();
                    m_statusLabel->setText(QString("Loaded seeds: %1").arg(item->text()));
                }
                break;
            }
        } });

    sidebarLayout->addWidget(seedListGroup);

    sidebarLayout->addStretch();

    contentLayout->addWidget(sidebar);

    mainLayout->addLayout(contentLayout, 1);

    // =====================================================
    // BOTTOM: Status bar
    // =====================================================
    m_statusLabel = new QLabel("Ready - Load an image to begin");
    m_statusLabel->setStyleSheet(R"(
        QLabel {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            padding: 6px 12px;
            font-family: 'Consolas', 'Courier New', monospace;
        }
    )");
    mainLayout->addWidget(m_statusLabel);

    // =====================================================
    // SIGNAL CONNECTIONS
    // =====================================================

    // Slice sliders update labels and views
    connect(m_axialSlider, &QSlider::valueChanged, [this](int v)
            {
        m_axialLabel->setText(QString("Axial: %1/%2").arg(v).arg(m_axialSlider->maximum()));
        updateViews(); });
    connect(m_sagittalSlider, &QSlider::valueChanged, [this](int v)
            {
        m_sagittalLabel->setText(QString("Sagittal: %1/%2").arg(v).arg(m_sagittalSlider->maximum()));
        updateViews(); });
    connect(m_coronalSlider, &QSlider::valueChanged, [this](int v)
            {
        m_coronalLabel->setText(QString("Coronal: %1/%2").arg(v).arg(m_coronalSlider->maximum()));
        updateViews(); });

    // Window/Level controls
    connect(m_windowSlider, &RangeSlider::rangeChanged, [this](int low, int high)
            {
        if (m_blockWindowSignals) return;
        applyWindowFromValues(static_cast<float>(low), static_cast<float>(high), true); });

    connect(m_windowLevelSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double level)
            {
        if (m_blockWindowSignals) return;
        double width = m_windowWidthSpin->value();
        float lo = static_cast<float>(level - width / 2.0);
        float hi = static_cast<float>(level + width / 2.0);
        applyWindowFromValues(lo, hi, false); });

    connect(m_windowWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double width)
            {
        if (m_blockWindowSignals) return;
        double level = m_windowLevelSpin->value();
        float lo = static_cast<float>(level - width / 2.0);
        float hi = static_cast<float>(level + width / 2.0);
        applyWindowFromValues(lo, hi, false); });

    // Mouse events for views
    connect(m_axialView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (m_maskMode != 0 && b == Qt::LeftButton)
            paintAxialMask(x, y);
        else
            onAxialClicked(x, y, b); });
    connect(m_axialView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        if ((buttons & Qt::LeftButton) && m_maskMode != 0)
            paintAxialMask(x, y);
        else if ((buttons & Qt::LeftButton) && m_seedMode == 1)
            addSeed(x, y, m_axialSlider->value()); });

    connect(m_sagittalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (m_maskMode != 0 && b == Qt::LeftButton)
            paintSagittalMask(x, y);
        else
            onSagittalClicked(x, y, b); });
    connect(m_sagittalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        if ((buttons & Qt::LeftButton) && m_maskMode != 0)
            paintSagittalMask(x, y);
        else if ((buttons & Qt::LeftButton) && m_seedMode == 1)
            addSeed(m_sagittalSlider->value(), x, y); });

    connect(m_coronalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (m_maskMode != 0 && b == Qt::LeftButton)
            paintCoronalMask(x, y);
        else
            onCoronalClicked(x, y, b); });
    connect(m_coronalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        if ((buttons & Qt::LeftButton) && m_maskMode != 0)
            paintCoronalMask(x, y);
        else if ((buttons & Qt::LeftButton) && m_seedMode == 1)
            addSeed(x, m_coronalSlider->value(), y); });
}

// =============================================================================
// IMAGE I/O
// =============================================================================

void ManualSeedSelector::openImage()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Open NIfTI Images", "", "NIfTI files (*.nii *.nii.gz)");
    if (files.isEmpty())
        return;

    // Add all selected files to the list
    for (const QString &f : files)
    {
        std::string path = f.toStdString();

        ImageData imgData;
        imgData.imagePath = path;
        imgData.color = getColorForImageIndex(static_cast<int>(m_images.size()));
        m_images.push_back(imgData);

        std::string filename = std::filesystem::path(path).filename().string();
        m_niftiList->addItem(QString::fromStdString(filename));
    }

    // Automatically select and load the first newly added image
    if (!files.isEmpty() && m_niftiList->count() > 0)
    {
        int newIndex = m_niftiList->count() - files.size();
        m_niftiList->setCurrentRow(newIndex);
    }
}

bool ManualSeedSelector::saveImageToFile(const std::string &path)
{
    if (m_image.getSizeX() == 0 || m_image.getSizeY() == 0 || m_image.getSizeZ() == 0)
    {
        QMessageBox::warning(this, "Save Image", "No image loaded.");
        return false;
    }

    std::string outpath = path;
    auto has_suffix = [](const std::string &p, const std::string &suf)
    {
        if (p.size() < suf.size())
            return false;
        return p.compare(p.size() - suf.size(), suf.size(), suf) == 0;
    };
    if (!has_suffix(outpath, ".nii") && !has_suffix(outpath, ".nii.gz"))
    {
        outpath += ".nii.gz";
    }

    if (!m_image.save(outpath))
    {
        QMessageBox::critical(this, "Save Image", "Failed to save image.");
        return false;
    }
    return true;
}

// =============================================================================
// SEEDS I/O
// =============================================================================

void ManualSeedSelector::saveSeeds()
{
    QString f = QFileDialog::getSaveFileName(this, "Save Seeds", "", "Text files (*.txt);;All files (*)");
    if (f.isEmpty())
        return;
    std::ofstream out(f.toStdString());
    if (!out)
    {
        QMessageBox::warning(this, "Save Seeds", "Failed to open file for writing.");
        return;
    }
    for (auto &s : m_seeds)
    {
        out << s.x << " " << s.y << " " << s.z << " " << s.label << " " << s.internal << "\n";
    }
    out.close();
}

void ManualSeedSelector::loadSeeds()
{
    QString f = QFileDialog::getOpenFileName(this, "Load Seeds", "", "Text files (*.txt);;All files (*)");
    if (f.isEmpty())
        return;
    std::string path = f.toStdString();
    if (loadSeedsFromFile(path))
    {
        // Add to current image's seed list if we have a current image
        if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
        {
            m_images[m_currentImageIndex].seedPaths.push_back(path);
            updateMaskSeedLists();
        }
    }
}

bool ManualSeedSelector::loadSeedsFromFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in || !in.is_open())
    {
        QMessageBox::warning(this, "Load Seeds", "Failed to open file for reading.");
        return false;
    }

    // Read entire file
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    m_seeds.clear();
    int sx = static_cast<int>(m_image.getSizeX());
    int sy = static_cast<int>(m_image.getSizeY());
    int sz = static_cast<int>(m_image.getSizeZ());

    // Replace all \r\n, \r, or \n with a standard newline
    std::string normalized;
    normalized.reserve(content.size());
    for (size_t i = 0; i < content.size(); ++i)
    {
        if (content[i] == '\r')
        {
            if (i + 1 < content.size() && content[i + 1] == '\n')
                ++i; // Skip \r in \r\n
            normalized += '\n';
        }
        else if (content[i] == '\n')
        {
            normalized += '\n';
        }
        else
        {
            normalized += content[i];
        }
    }

    std::istringstream stream(normalized);
    std::string line;
    int lineCount = 0;
    int skippedLines = 0;
    bool firstLineIsCount = false;

    // Read first line
    if (std::getline(stream, line))
    {
        lineCount++;
        std::istringstream testIss(line);
        int count;
        std::string extra;
        // Try to read as single integer (seed count)
        if ((testIss >> count) && !(testIss >> extra))
        {
            std::cout << "[INFO] Seed file header indicates " << count << " seeds\n";
            firstLineIsCount = true;
        }
        else
        {
            // First line is not just a count, try to parse as seed
            std::istringstream iss(line);
            Seed s;
            if (iss >> s.x >> s.y >> s.z >> s.label >> s.internal)
            {
                s.x = std::max(0, std::min(s.x, sx - 1));
                s.y = std::max(0, std::min(s.y, sy - 1));
                s.z = std::max(0, std::min(s.z, sz - 1));
                m_seeds.push_back(s);
            }
            else
            {
                skippedLines++;
            }
        }
    }

    // Read remaining lines
    while (std::getline(stream, line))
    {
        lineCount++;

        if (line.empty())
        {
            skippedLines++;
            continue;
        }

        std::istringstream iss(line);
        Seed s;
        if (!(iss >> s.x >> s.y >> s.z >> s.label >> s.internal))
        {
            skippedLines++;
            continue;
        }
        // Clamp to image bounds
        s.x = std::max(0, std::min(s.x, sx - 1));
        s.y = std::max(0, std::min(s.y, sy - 1));
        s.z = std::max(0, std::min(s.z, sz - 1));
        m_seeds.push_back(s);
    }

    std::cout << "[INFO] Loaded " << m_seeds.size() << " seeds from file (" << lineCount << " lines read, " << skippedLines << " skipped)\n";

    // Update views to show seeds
    updateViews();

    // Force repaint of all views
    m_axialView->update();
    m_sagittalView->update();
    m_coronalView->update();

    return true;
}

// =============================================================================
// SEED OPERATIONS
// =============================================================================

bool ManualSeedSelector::hasImage() const
{
    return m_image.getSizeX() > 0 && m_image.getSizeY() > 0 && m_image.getSizeZ() > 0;
}

void ManualSeedSelector::onAxialClicked(int x, int y, Qt::MouseButton b)
{
    int z = m_axialSlider->value();
    if (b == Qt::LeftButton)
    {
        if (m_seedMode == 1)
            addSeed(x, y, z);
        else if (m_seedMode == 2)
            eraseNear(x, y, z, m_seedBrushRadius);
    }
    else if (b == Qt::RightButton)
    {
        eraseNear(x, y, z, m_seedBrushRadius);
    }
}

void ManualSeedSelector::onSagittalClicked(int x, int y, Qt::MouseButton b)
{
    int x_loc = m_sagittalSlider->value();
    int y_loc = x;
    int z_loc = y;
    if (b == Qt::LeftButton)
    {
        if (m_seedMode == 1)
            addSeed(x_loc, y_loc, z_loc);
        else if (m_seedMode == 2)
            eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
    }
    else if (b == Qt::RightButton)
    {
        eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
    }
}

void ManualSeedSelector::onCoronalClicked(int x, int y, Qt::MouseButton b)
{
    int y_loc = m_coronalSlider->value();
    int x_loc = x;
    int z_loc = y;
    if (b == Qt::LeftButton)
    {
        if (m_seedMode == 1)
            addSeed(x_loc, y_loc, z_loc);
        else if (m_seedMode == 2)
            eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
    }
    else if (b == Qt::RightButton)
    {
        eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
    }
}

void ManualSeedSelector::addSeed(int x, int y, int z)
{
    Seed s;
    s.x = x;
    s.y = y;
    s.z = z;
    s.label = m_labelSelector->value();
    s.internal = 1;
    m_seeds.push_back(s);
    updateViews();
}

void ManualSeedSelector::eraseNear(int x, int y, int z, int r)
{
    std::vector<Seed> keep;
    for (auto &s : m_seeds)
    {
        int dx = s.x - x;
        int dy = s.y - y;
        int dz = s.z - z;
        if (dx * dx + dy * dy + dz * dz > r * r)
            keep.push_back(s);
    }
    m_seeds.swap(keep);
    updateViews();
}

void ManualSeedSelector::updateLabelColor(int label)
{
    int l = std::max(1, std::min(254, label));
    QColor c = colorForLabel(l);
    QPixmap pm(m_labelColorIndicator->width(), m_labelColorIndicator->height());
    pm.fill(c);
    m_labelColorIndicator->setPixmap(pm);
    m_labelColorIndicator->setStyleSheet("border:1px solid black;");
}

// =============================================================================
// WINDOW/LEVEL
// =============================================================================

void ManualSeedSelector::resetWindowToFullRange()
{
    applyWindowFromValues(m_windowGlobalMin, m_windowGlobalMax, false);
}

void ManualSeedSelector::applyWindowFromValues(float low, float high, bool fromSlider)
{
    if (m_windowGlobalMax <= m_windowGlobalMin)
        m_windowGlobalMax = m_windowGlobalMin + 1.0f;

    float clampedLow = std::max(m_windowGlobalMin, std::min(low, m_windowGlobalMax));
    float clampedHigh = std::max(clampedLow + 1e-3f, std::min(high, m_windowGlobalMax));

    m_windowLow = clampedLow;
    m_windowHigh = clampedHigh;

    double level = 0.5 * (static_cast<double>(clampedLow) + static_cast<double>(clampedHigh));
    double width = static_cast<double>(clampedHigh - clampedLow);

    m_blockWindowSignals = true;
    if (!fromSlider && m_windowSlider)
    {
        bool prevBlocked = m_windowSlider->blockSignals(true);
        m_windowSlider->setLowerValue(static_cast<int>(std::round(clampedLow)));
        m_windowSlider->setUpperValue(static_cast<int>(std::round(clampedHigh)));
        m_windowSlider->blockSignals(prevBlocked);
    }
    if (m_windowLevelSpin)
        m_windowLevelSpin->setValue(level);
    if (m_windowWidthSpin)
        m_windowWidthSpin->setValue(width);
    m_blockWindowSignals = false;

    updateViews();
}

// =============================================================================
// VIEW UPDATES
// =============================================================================

static QImage makeQImageFromRGB(const std::vector<unsigned char> &rgb, int w, int h)
{
    size_t expected = size_t(w) * size_t(h) * 3;
    if (rgb.size() < expected)
    {
        std::cerr << "[ERROR] makeQImageFromRGB: rgb buffer too small\n";
        return QImage();
    }
    QImage img(w, h, QImage::Format_RGB888);
    int bpl = img.bytesPerLine();
    const unsigned char *src = rgb.data();
    unsigned char *dst = img.bits();
    for (int y = 0; y < h; ++y)
    {
        std::memcpy(dst + size_t(y) * bpl, src + size_t(y) * w * 3, size_t(w * 3));
    }
    return img;
}

void ManualSeedSelector::updateViews()
{
    unsigned int sizeX = m_image.getSizeX();
    unsigned int sizeY = m_image.getSizeY();
    unsigned int sizeZ = m_image.getSizeZ();

    // Renderização 3D controlada pelo checkbox "Show 3D"
    if (m_enable3DView && m_mask3DDirty)
    {
        update3DMaskView();
        m_mask3DDirty = false;
    }

    if (sizeX == 0 || sizeY == 0 || sizeZ == 0)
    {
        std::cerr << "[WARN] updateViews: image dimensions invalid\n";
        return;
    }

    int z = m_axialSlider->value();
    float lo = m_windowLow;
    float hi = m_windowHigh;
    if (hi <= lo)
    {
        lo = m_windowGlobalMin;
        hi = m_windowGlobalMax;
    }

    // Validate mask buffer size
    if (!m_maskData.empty())
    {
        size_t expected = size_t(sizeX) * size_t(sizeY) * size_t(sizeZ);
        if (m_maskData.size() != expected)
        {
            std::cerr << "updateViews: mask buffer size mismatch, clearing\n";
            m_maskData.clear();
            m_mask3DDirty = true;
        }
    }

    // Axial view
    auto axial_rgb = m_image.getAxialSliceAsRGB(z, lo, hi);
    if (!m_maskData.empty())
    {
        for (unsigned int yy = 0; yy < sizeY; ++yy)
        {
            for (unsigned int xx = 0; xx < sizeX; ++xx)
            {
                size_t idx3 = size_t(xx) + size_t(yy) * sizeX + size_t(z) * sizeX * sizeY;
                int lbl = m_maskData[idx3];
                if (lbl != 0)
                {
                    int dl = std::max(1, std::min(254, lbl));
                    QColor col = colorForLabel(dl);
                    unsigned char r = static_cast<unsigned char>(col.red());
                    unsigned char g = static_cast<unsigned char>(col.green());
                    unsigned char b = static_cast<unsigned char>(col.blue());
                    size_t pix = (yy * sizeX + xx) * 3;
                    axial_rgb[pix + 0] = static_cast<unsigned char>(m_maskOpacity * r + (1.0f - m_maskOpacity) * axial_rgb[pix + 0]);
                    axial_rgb[pix + 1] = static_cast<unsigned char>(m_maskOpacity * g + (1.0f - m_maskOpacity) * axial_rgb[pix + 1]);
                    axial_rgb[pix + 2] = static_cast<unsigned char>(m_maskOpacity * b + (1.0f - m_maskOpacity) * axial_rgb[pix + 2]);
                }
            }
        }
    }
    QImage axial = makeQImageFromRGB(axial_rgb, int(sizeX), int(sizeY));
    m_axialView->setImage(axial);

    // Sagittal view
    int sagX = m_sagittalSlider->value();
    auto sagittal_rgb = m_image.getSagittalSliceAsRGB(sagX, lo, hi);
    if (!m_maskData.empty())
    {
        for (unsigned int zz = 0; zz < sizeZ; ++zz)
        {
            for (unsigned int yy = 0; yy < sizeY; ++yy)
            {
                size_t idx3 = size_t(sagX) + size_t(yy) * sizeX + size_t(zz) * sizeX * sizeY;
                int lbl = m_maskData[idx3];
                if (lbl != 0)
                {
                    int dl = std::max(1, std::min(254, lbl));
                    QColor col = colorForLabel(dl);
                    unsigned char r = static_cast<unsigned char>(col.red());
                    unsigned char g = static_cast<unsigned char>(col.green());
                    unsigned char b = static_cast<unsigned char>(col.blue());
                    size_t pix = (zz * sizeY + yy) * 3;
                    sagittal_rgb[pix + 0] = static_cast<unsigned char>(m_maskOpacity * r + (1.0f - m_maskOpacity) * sagittal_rgb[pix + 0]);
                    sagittal_rgb[pix + 1] = static_cast<unsigned char>(m_maskOpacity * g + (1.0f - m_maskOpacity) * sagittal_rgb[pix + 1]);
                    sagittal_rgb[pix + 2] = static_cast<unsigned char>(m_maskOpacity * b + (1.0f - m_maskOpacity) * sagittal_rgb[pix + 2]);
                }
            }
        }
    }
    QImage sagittal = makeQImageFromRGB(sagittal_rgb, int(sizeY), int(sizeZ));
    m_sagittalView->setImage(sagittal);

    // Coronal view
    int corY = m_coronalSlider->value();
    auto coronal_rgb = m_image.getCoronalSliceAsRGB(corY, lo, hi);
    if (!m_maskData.empty())
    {
        for (unsigned int zz = 0; zz < sizeZ; ++zz)
        {
            for (unsigned int xx = 0; xx < sizeX; ++xx)
            {
                size_t idx3 = size_t(xx) + size_t(corY) * sizeX + size_t(zz) * sizeX * sizeY;
                int lbl = m_maskData[idx3];
                if (lbl != 0)
                {
                    int dl = std::max(1, std::min(254, lbl));
                    QColor col = colorForLabel(dl);
                    unsigned char r = static_cast<unsigned char>(col.red());
                    unsigned char g = static_cast<unsigned char>(col.green());
                    unsigned char b = static_cast<unsigned char>(col.blue());
                    size_t pix = (zz * sizeX + xx) * 3;
                    coronal_rgb[pix + 0] = static_cast<unsigned char>(m_maskOpacity * r + (1.0f - m_maskOpacity) * coronal_rgb[pix + 0]);
                    coronal_rgb[pix + 1] = static_cast<unsigned char>(m_maskOpacity * g + (1.0f - m_maskOpacity) * coronal_rgb[pix + 1]);
                    coronal_rgb[pix + 2] = static_cast<unsigned char>(m_maskOpacity * b + (1.0f - m_maskOpacity) * coronal_rgb[pix + 2]);
                }
            }
        }
    }
    QImage coronal = makeQImageFromRGB(coronal_rgb, int(sizeX), int(sizeZ));
    m_coronalView->setImage(coronal);

    // Seed overlays
    m_axialView->setOverlayDraw([this, z](QPainter &p, float scale)
                                {
        for (auto &s : m_seeds)
        {
            if (s.z != z)
                continue;
            int lbl = std::max(1, std::min(254, s.label));
            QColor qc = colorForLabel(lbl);
            p.setPen(QPen(qc));
            p.setBrush(qc);
            p.drawEllipse(QPoint(int(s.x * scale), int(s.y * scale)), 2, 2);
        } });

    m_sagittalView->setOverlayDraw([this, sagX](QPainter &p, float scale)
                                   {
        for (auto &s : m_seeds)
        {
            if (s.x != sagX)
                continue;
            int lbl = std::max(1, std::min(254, s.label));
            QColor qc = colorForLabel(lbl);
            p.setPen(QPen(qc));
            p.setBrush(qc);
            p.drawEllipse(QPoint(int(s.y * scale), int(s.z * scale)), 2, 2);
        } });

    m_coronalView->setOverlayDraw([this, corY](QPainter &p, float scale)
                                  {
        for (auto &s : m_seeds)
        {
            if (s.y != corY)
                continue;
            int lbl = std::max(1, std::min(254, s.label));
            QColor qc = colorForLabel(lbl);
            p.setPen(QPen(qc));
            p.setBrush(qc);
            p.drawEllipse(QPoint(int(s.x * scale), int(s.z * scale)), 2, 2);
        } });
}

void ManualSeedSelector::update3DMaskView()
{
    if (!m_mask3DView)
        return;
    unsigned int sx = m_image.getSizeX();
    unsigned int sy = m_image.getSizeY();
    unsigned int sz = m_image.getSizeZ();
    m_mask3DView->setMaskData(m_maskData, sx, sy, sz);
}

// =============================================================================
// MASK OPERATIONS
// =============================================================================

void ManualSeedSelector::setMaskMode(int mode)
{
    m_maskMode = mode;
}

void ManualSeedSelector::cleanMask()
{
    m_maskData.clear();
    m_mask3DDirty = true;
}

bool ManualSeedSelector::saveMaskToFile(const std::string &path)
{
    try
    {
        using PixelType = int16_t;
        using ImageType = itk::Image<PixelType, 3>;
        using WriterType = itk::ImageFileWriter<ImageType>;

        ImageType::Pointer out = ImageType::New();
        ImageType::RegionType region;
        ImageType::IndexType start;
        start.Fill(0);
        ImageType::SizeType size;
        unsigned int sx = m_image.getSizeX();
        unsigned int sy = m_image.getSizeY();
        unsigned int sz = m_image.getSizeZ();
        if (sx == 0 || sy == 0 || sz == 0)
        {
            QMessageBox::warning(this, "Save Mask", "No image loaded.");
            return false;
        }
        size[0] = static_cast<ImageType::SizeValueType>(sx);
        size[1] = static_cast<ImageType::SizeValueType>(sy);
        size[2] = static_cast<ImageType::SizeValueType>(sz);
        region.SetIndex(start);
        region.SetSize(size);
        out->SetRegions(region);
        out->Allocate();

        if (m_maskData.empty())
            m_maskData.assign(size_t(sx) * size_t(sy) * size_t(sz), 0);

        itk::ImageRegionIterator<ImageType> it(out, region);
        size_t idx = 0;
        for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++idx)
        {
            int v = m_maskData[idx];
            if (v < std::numeric_limits<PixelType>::min())
                v = std::numeric_limits<PixelType>::min();
            if (v > std::numeric_limits<PixelType>::max())
                v = std::numeric_limits<PixelType>::max();
            it.Set(static_cast<PixelType>(v));
        }

        std::string outpath = path;
        auto has_suffix = [](const std::string &p, const std::string &suf)
        {
            if (p.size() < suf.size())
                return false;
            return p.compare(p.size() - suf.size(), suf.size(), suf) == 0;
        };
        if (!has_suffix(outpath, ".nii") && !has_suffix(outpath, ".nii.gz"))
        {
            outpath += ".nii.gz";
        }

        WriterType::Pointer writer = WriterType::New();
        itk::NiftiImageIO::Pointer nio = itk::NiftiImageIO::New();
        writer->SetImageIO(nio);
        writer->SetFileName(outpath);
        writer->SetInput(out);
        writer->Update();
        return true;
    }
    catch (const std::exception &e)
    {
        QMessageBox::critical(this, "Save Mask", QString("Failed: %1").arg(e.what()));
        return false;
    }
}

bool ManualSeedSelector::loadMaskFromFile(const std::string &path)
{
    try
    {
        using ImageType = itk::Image<int32_t, 3>;
        using ReaderType = itk::ImageFileReader<ImageType>;
        ReaderType::Pointer reader = ReaderType::New();
        reader->SetFileName(path);
        reader->Update();
        ImageType::Pointer img = reader->GetOutput();
        ImageType::RegionType region = img->GetLargestPossibleRegion();
        ImageType::SizeType size = region.GetSize();
        unsigned int sx = static_cast<unsigned int>(size[0]);
        unsigned int sy = static_cast<unsigned int>(size[1]);
        unsigned int sz = static_cast<unsigned int>(size[2]);
        size_t tot = size_t(sx) * size_t(sy) * size_t(sz);
        m_maskData.clear();
        m_maskData.resize(tot);
        itk::ImageRegionConstIterator<ImageType> it(img, region);
        size_t idx = 0;
        for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++idx)
        {
            m_maskData[idx] = static_cast<int>(it.Get());
        }
        m_mask3DDirty = true;
        return true;
    }
    catch (const std::exception &e)
    {
        QMessageBox::critical(this, "Load Mask", QString("Failed: %1").arg(e.what()));
        return false;
    }
}

void ManualSeedSelector::paintAxialMask(int x, int y)
{
    if (m_image.getSizeX() == 0)
        return;
    int z = m_axialSlider->value();
    bool erase = (m_maskMode == 2);
    applyBrushToMask({x, y, z}, {0, 1}, m_maskBrushRadius, m_labelSelector->value(), erase);
    updateViews();
}

void ManualSeedSelector::paintSagittalMask(int x, int y)
{
    int sx = m_sagittalSlider->value();
    bool erase = (m_maskMode == 2);
    applyBrushToMask({sx, x, y}, {1, 2}, m_maskBrushRadius, m_labelSelector->value(), erase);
    updateViews();
}

void ManualSeedSelector::paintCoronalMask(int x, int y)
{
    int cy = m_coronalSlider->value();
    bool erase = (m_maskMode == 2);
    applyBrushToMask({x, cy, y}, {0, 2}, m_maskBrushRadius, m_labelSelector->value(), erase);
    updateViews();
}

void ManualSeedSelector::applyBrushToMask(const std::array<int, 3> &center, const std::pair<int, int> &axes, int radius, int labelValue, bool erase)
{
    unsigned int sx = m_image.getSizeX();
    unsigned int sy = m_image.getSizeY();
    unsigned int sz = m_image.getSizeZ();
    if (sx == 0)
        return;
    if (m_maskData.empty())
        m_maskData.assign(sx * sy * sz, 0);

    int a0 = axes.first;
    int a1 = axes.second;
    int min0 = std::max(0, center[a0] - radius);
    int max0 = std::min(int((a0 == 0 ? sx : (a0 == 1 ? sy : sz))) - 1, center[a0] + radius);
    int min1 = std::max(0, center[a1] - radius);
    int max1 = std::min(int((a1 == 0 ? sx : (a1 == 1 ? sy : sz))) - 1, center[a1] + radius);

    for (int i = min0; i <= max0; ++i)
    {
        for (int j = min1; j <= max1; ++j)
        {
            int di = i - center[a0];
            int dj = j - center[a1];
            if (di * di + dj * dj <= radius * radius)
            {
                int xi = (a0 == 0) ? i : ((a1 == 0) ? j : center[0]);
                int yi = (a0 == 1) ? i : ((a1 == 1) ? j : center[1]);
                int zi = (a0 == 2) ? i : ((a1 == 2) ? j : center[2]);
                if (xi < 0 || yi < 0 || zi < 0 || xi >= int(sx) || yi >= int(sy) || zi >= int(sz))
                    continue;
                size_t idx = size_t(xi) + size_t(yi) * sx + size_t(zi) * sx * sy;
                if (erase)
                {
                    if (m_maskData[idx] == labelValue)
                        m_maskData[idx] = 0;
                }
                else
                {
                    m_maskData[idx] = labelValue;
                }
            }
        }
        m_mask3DDirty = true;
    }
}

// =============================================================================
// KEY EVENTS
// =============================================================================

void ManualSeedSelector::keyPressEvent(QKeyEvent *event)
{
    // F11 = toggle fullscreen
    if (event->key() == Qt::Key_F11)
    {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
        return;
    }

    // Check if focus is on an image view
    QWidget *fw = QApplication::focusWidget();
    auto isDescendant = [](QWidget *child, QWidget *ancestor)
    {
        while (child)
        {
            if (child == ancestor)
                return true;
            child = child->parentWidget();
        }
        return false;
    };
    if (fw && fw != this && !isDescendant(fw, m_axialView) && !isDescendant(fw, m_sagittalView) && !isDescendant(fw, m_coronalView))
    {
        QPoint gpos = QCursor::pos();
        bool overView = false;
        for (QWidget *v : {m_axialView, m_sagittalView, m_coronalView})
        {
            if (!v)
                continue;
            QPoint local = v->mapFromGlobal(gpos);
            if (v->rect().contains(local))
            {
                overView = true;
                break;
            }
        }
        if (!overView)
        {
            QMainWindow::keyPressEvent(event);
            return;
        }
    }

    int axial = m_axialSlider->value();
    int sag = m_sagittalSlider->value();
    int cor = m_coronalSlider->value();
    bool handled = true;

    switch (event->key())
    {
    case Qt::Key_W:
        axial = std::min<int>(axial + 1, int(m_image.getSizeZ()) - 1);
        m_axialSlider->setValue(axial);
        break;
    case Qt::Key_S:
        axial = std::max<int>(axial - 1, 0);
        m_axialSlider->setValue(axial);
        break;
    case Qt::Key_D:
        sag = std::min<int>(sag + 1, int(m_image.getSizeX()) - 1);
        m_sagittalSlider->setValue(sag);
        break;
    case Qt::Key_A:
        sag = std::max<int>(sag - 1, 0);
        m_sagittalSlider->setValue(sag);
        break;
    case Qt::Key_E:
        cor = std::min<int>(cor + 1, int(m_image.getSizeY()) - 1);
        m_coronalSlider->setValue(cor);
        break;
    case Qt::Key_Q:
        cor = std::max<int>(cor - 1, 0);
        m_coronalSlider->setValue(cor);
        break;
    default:
        handled = false;
    }
    if (!handled)
        QMainWindow::keyPressEvent(event);
}

bool ManualSeedSelector::handleSliceKey(QKeyEvent *event)
{
    if (!hasImage())
        return false;
    int axial = m_axialSlider->value();
    int sag = m_sagittalSlider->value();
    int cor = m_coronalSlider->value();
    bool handled = true;
    switch (event->key())
    {
    case Qt::Key_W:
        axial = std::min<int>(axial + 1, int(m_image.getSizeZ()) - 1);
        m_axialSlider->setValue(axial);
        break;
    case Qt::Key_S:
        axial = std::max<int>(axial - 1, 0);
        m_axialSlider->setValue(axial);
        break;
    case Qt::Key_D:
        sag = std::min<int>(sag + 1, int(m_image.getSizeX()) - 1);
        m_sagittalSlider->setValue(sag);
        break;
    case Qt::Key_A:
        sag = std::max<int>(sag - 1, 0);
        m_sagittalSlider->setValue(sag);
        break;
    case Qt::Key_E:
        cor = std::min<int>(cor + 1, int(m_image.getSizeY()) - 1);
        m_coronalSlider->setValue(cor);
        break;
    case Qt::Key_Q:
        cor = std::max<int>(cor - 1, 0);
        m_coronalSlider->setValue(cor);
        break;
    case Qt::Key_BracketLeft:
        m_axialSlider->setValue(std::max(0, axial - 1));
        m_sagittalSlider->setValue(std::max(0, sag - 1));
        m_coronalSlider->setValue(std::max(0, cor - 1));
        break;
    case Qt::Key_BracketRight:
        m_axialSlider->setValue(std::min<int>(axial + 1, int(m_image.getSizeZ()) - 1));
        m_sagittalSlider->setValue(std::min<int>(sag + 1, int(m_image.getSizeX()) - 1));
        m_coronalSlider->setValue(std::min<int>(cor + 1, int(m_image.getSizeY()) - 1));
        break;
    default:
        handled = false;
    }
    return handled;
}

bool ManualSeedSelector::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        if (obj == m_axialView || obj == m_sagittalView || obj == m_coronalView)
        {
            QKeyEvent *ke = static_cast<QKeyEvent *>(event);
            if (handleSliceKey(ke))
                return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}
QColor ManualSeedSelector::getColorForImageIndex(int index)
{
    // Generate distinct colors for each image
    static const QColor colors[] = {
        QColor(100, 180, 255), // Light blue
        QColor(255, 150, 100), // Orange
        QColor(150, 255, 150), // Light green
        QColor(255, 100, 255), // Magenta
        QColor(255, 255, 100), // Yellow
        QColor(150, 150, 255), // Purple
        QColor(100, 255, 255), // Cyan
        QColor(255, 150, 150), // Pink
    };
    return colors[index % 8];
}

void ManualSeedSelector::updateMaskSeedLists()
{
    // Clear lists
    m_maskList->clear();
    m_seedList->clear();

    if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size()))
        return;

    const ImageData &currentImage = m_images[m_currentImageIndex];
    QColor color = currentImage.color;

    // Populate mask list with masks for current image
    for (const auto &maskPath : currentImage.maskPaths)
    {
        std::string filename = std::filesystem::path(maskPath).filename().string();
        QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(filename));
        item->setForeground(QBrush(color));
        m_maskList->addItem(item);
    }

    // Populate seed list with seeds for current image
    for (const auto &seedPath : currentImage.seedPaths)
    {
        std::string filename = std::filesystem::path(seedPath).filename().string();
        QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(filename));
        item->setForeground(QBrush(color));
        m_seedList->addItem(item);
    }
}