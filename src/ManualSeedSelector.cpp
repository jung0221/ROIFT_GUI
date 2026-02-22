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
#include <limits>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_set>
#include <utility>
#include <filesystem>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QVector>
#if defined(ROIFT_HAS_QT_SVG)
#include <QSvgRenderer>
#endif

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIterator.h>
#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>
#include <zlib.h>

namespace
{
enum class NiftiButtonIcon
{
    Add,
    AddCsv,
    ExportCsv,
    Remove,
    RemoveAll
};

QIcon makeFallbackButtonIcon(NiftiButtonIcon type, const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0)
        return QIcon();

    auto renderIcon = [type](int edge) -> QPixmap
    {
        QPixmap pixmap(edge, edge);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const QColor strokeColor(216, 216, 216);
        const qreal strokeWidth = std::max(1.4, edge * 0.095);
        QPen pen(strokeColor, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);

        const qreal w = static_cast<qreal>(edge);
        const qreal h = static_cast<qreal>(edge);

        switch (type)
        {
        case NiftiButtonIcon::Add:
            painter.drawLine(QPointF(0.22 * w, 0.50 * h), QPointF(0.78 * w, 0.50 * h));
            painter.drawLine(QPointF(0.50 * w, 0.22 * h), QPointF(0.50 * w, 0.78 * h));
            break;
        case NiftiButtonIcon::AddCsv:
        {
            QRectF pageRect(0.15 * w, 0.14 * h, 0.46 * w, 0.72 * h);
            painter.drawRoundedRect(pageRect, 1.1, 1.1);
            painter.drawLine(QPointF(0.46 * w, 0.14 * h), QPointF(0.61 * w, 0.29 * h));
            painter.drawLine(QPointF(0.66 * w, 0.58 * h), QPointF(0.86 * w, 0.58 * h));
            painter.drawLine(QPointF(0.76 * w, 0.48 * h), QPointF(0.76 * w, 0.68 * h));
            break;
        }
        case NiftiButtonIcon::ExportCsv:
        {
            QRectF pageRect(0.14 * w, 0.14 * h, 0.50 * w, 0.72 * h);
            painter.drawRoundedRect(pageRect, 1.1, 1.1);
            painter.drawLine(QPointF(0.46 * w, 0.14 * h), QPointF(0.64 * w, 0.32 * h));
            painter.drawLine(QPointF(0.77 * w, 0.40 * h), QPointF(0.77 * w, 0.72 * h));
            painter.drawLine(QPointF(0.67 * w, 0.62 * h), QPointF(0.77 * w, 0.72 * h));
            painter.drawLine(QPointF(0.87 * w, 0.62 * h), QPointF(0.77 * w, 0.72 * h));
            break;
        }
        case NiftiButtonIcon::Remove:
            painter.drawLine(QPointF(0.22 * w, 0.50 * h), QPointF(0.78 * w, 0.50 * h));
            break;
        case NiftiButtonIcon::RemoveAll:
            painter.drawEllipse(QRectF(0.18 * w, 0.18 * h, 0.64 * w, 0.64 * h));
            painter.drawLine(QPointF(0.34 * w, 0.34 * h), QPointF(0.66 * w, 0.66 * h));
            painter.drawLine(QPointF(0.66 * w, 0.34 * h), QPointF(0.34 * w, 0.66 * h));
            break;
        }

        return pixmap;
    };

    const int edge = std::max(size.width(), size.height());
    const int medium = std::max(edge + 6, static_cast<int>(std::round(edge * 1.5)));
    const int large = std::max(edge + 12, edge * 2);

    QIcon icon;
    icon.addPixmap(renderIcon(edge));
    icon.addPixmap(renderIcon(medium));
    icon.addPixmap(renderIcon(large));
    return icon;
}

QIcon makeMonochromeIcon(const char *svgData, const QSize &size, const QIcon &fallback = QIcon())
{
#if defined(ROIFT_HAS_QT_SVG)
    if (!svgData || size.isEmpty())
        return fallback;

    QSvgRenderer renderer(QByteArray(svgData));
    if (!renderer.isValid())
        return fallback;

    auto renderSvg = [&renderer](int edge) -> QPixmap
    {
        QPixmap pixmap(edge, edge);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, QRectF(0, 0, edge, edge));
        return pixmap;
    };

    const int edge = std::max(size.width(), size.height());
    const int medium = std::max(edge + 6, static_cast<int>(std::round(edge * 1.5)));
    const int large = std::max(edge + 12, edge * 2);

    QIcon icon;
    icon.addPixmap(renderSvg(edge));
    icon.addPixmap(renderSvg(medium));
    icon.addPixmap(renderSvg(large));
    return icon;
#else
    Q_UNUSED(svgData);
    Q_UNUSED(size);
    return fallback;
#endif
}

constexpr const char *kAddIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M12 5.2V18.8M5.2 12H18.8" stroke="#d8d8d8" stroke-width="1.8" stroke-linecap="round"/>
</svg>
)svg";

constexpr const char *kAddCsvIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M7 3.8H13L17 7.8V20.2H7V3.8Z" stroke="#d8d8d8" stroke-width="1.4" stroke-linejoin="round"/>
  <path d="M13 3.8V7.8H17" stroke="#d8d8d8" stroke-width="1.4" stroke-linejoin="round"/>
  <path d="M12 11.5V16.8M9.3 14.15H14.7" stroke="#d8d8d8" stroke-width="1.6" stroke-linecap="round"/>
</svg>
)svg";

constexpr const char *kRemoveIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <circle cx="12" cy="12" r="8.1" stroke="#d8d8d8" stroke-width="1.4"/>
  <path d="M8.2 12H15.8" stroke="#d8d8d8" stroke-width="1.8" stroke-linecap="round"/>
</svg>
)svg";

constexpr const char *kExportCsvIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M5.4 3.9H12.4L16.6 8.1V19.8H5.4V3.9Z" stroke="#d8d8d8" stroke-width="1.35" stroke-linejoin="round"/>
  <path d="M12.4 3.9V8.1H16.6" stroke="#d8d8d8" stroke-width="1.35" stroke-linejoin="round"/>
  <path d="M18.2 10.6V16.7" stroke="#d8d8d8" stroke-width="1.6" stroke-linecap="round"/>
  <path d="M16.2 14.9L18.2 16.9L20.2 14.9" stroke="#d8d8d8" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
)svg";

constexpr const char *kRemoveAllIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M8.6 8.2H15.4M10 8.2V6.7C10 6.31 10.31 6 10.7 6H13.3C13.69 6 14 6.31 14 6.7V8.2M9 10V16.5C9 16.89 9.31 17.2 9.7 17.2H14.3C14.69 17.2 15 16.89 15 16.5V10"
        stroke="#d8d8d8" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M6.4 10H17.6" stroke="#d8d8d8" stroke-width="1.4" stroke-linecap="round"/>
  <path d="M6.7 18.6H17.3" stroke="#d8d8d8" stroke-width="1.4" stroke-linecap="round"/>
</svg>
)svg";

QString normalizeCsvCell(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
        value = value.mid(1, value.size() - 2);
    return value.trimmed();
}

QString csvEscapeCell(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r'))
        escaped = "\"" + escaped + "\"";
    return escaped;
}

QStringList parseCsvRow(const QString &line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i)
    {
        const QChar ch = line[i];
        if (ch == '"')
        {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
            {
                current += '"';
                ++i;
            }
            else
            {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == ',' && !inQuotes)
        {
            fields.push_back(normalizeCsvCell(current));
            current.clear();
            continue;
        }

        current += ch;
    }

    fields.push_back(normalizeCsvCell(current));
    return fields;
}

bool isNiftiPathCell(const QString &value)
{
    const QString lower = normalizeCsvCell(value).toLower();
    return lower.endsWith(".nii") || lower.endsWith(".nii.gz");
}

bool isNiftiMaskFilenameCandidate(const QString &fileName)
{
    const QString lower = fileName.trimmed().toLower();
    if (!(lower.endsWith(".nii") || lower.endsWith(".nii.gz")))
        return false;

    return lower.startsWith("left_lung") ||
           lower.startsWith("right_lung") ||
           lower.startsWith("trachea") ||
           lower.startsWith("ribs");
}

bool isSeedFilenameCandidate(const QString &fileName)
{
    return fileName.trimmed().toLower().endsWith(".txt");
}

int chooseNiftiColumn(const QStringList &headers, const std::vector<int> &niftiCounts)
{
    int selected = -1;
    int bestScore = -1;

    for (int i = 0; i < headers.size(); ++i)
    {
        if (i < 0 || i >= static_cast<int>(niftiCounts.size()) || niftiCounts[i] <= 0)
            continue;

        const QString header = headers[i].trimmed().toLower();
        int score = niftiCounts[i] * 10;

        if (header == "path" || header == "filepath" || header == "file_path" || header == "image_path" || header == "nifti_path")
            score += 1000;
        if (header.contains("path"))
            score += 400;
        if (header.contains("nifti"))
            score += 250;
        if (header.contains("image"))
            score += 150;
        if (header.contains("file"))
            score += 100;

        if (score > bestScore)
        {
            bestScore = score;
            selected = i;
        }
    }

    return selected;
}
} // namespace

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
            font-size: 8px;
            padding: 1px 2px;
            border-radius: 2px;
        }
        QSplitter::handle {
            background-color: #3a3a3a;
        }
        QSplitter::handle:hover {
            background-color: #0078d4;
        }
    )");

    setWindowTitle("ROIFT GUI");
    resize(1400, 950);

    QWidget *central = new QWidget();
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    m_ribbonTabs = new QTabWidget();
    m_ribbonTabs->setMaximumHeight(120);

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

    QPushButton *btnOpenCsv = new QPushButton("Open CSV");
    btnOpenCsv->setToolTip("Open a CSV and add NIfTI paths listed in it");
    connect(btnOpenCsv, &QPushButton::clicked, this, &ManualSeedSelector::openImagesFromCsv);
    niftiLayout->addWidget(btnOpenCsv);

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

    m_ribbonTabs->addTab(filesTab, "Files");

    // --- TAB 1: VIEW (Window/Level & View controls) ---
    QWidget *sliderTab = new QWidget();
    QHBoxLayout *sliderLayout = new QHBoxLayout(sliderTab);
    sliderLayout->setSpacing(10);
    sliderLayout->setContentsMargins(8, 6, 8, 6);

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

    m_ribbonTabs->addTab(sliderTab, "View");

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
            {
        m_seedMode = id;
        if (m_mask3DView)
            m_mask3DView->setSeedRectangleEraseEnabled(isSeedsTabActive() && m_seedMode == 2); });
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

    m_seedTabIndex = m_ribbonTabs->addTab(seedsTab, "Seeds");

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
    QGroupBox *maskBrushGroup = new QGroupBox("Brush");
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

    m_maskTabIndex = m_ribbonTabs->addTab(maskTab, "Mask");

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

    m_ribbonTabs->addTab(segTab, "Segmentation");

    mainLayout->addWidget(m_ribbonTabs);

    // =====================================================
    // MAIN CONTENT: View Grid + Right Sidebar (resizable)
    // =====================================================
    QSplitter *contentSplitter = new QSplitter(Qt::Horizontal);
    contentSplitter->setChildrenCollapsible(false);
    contentSplitter->setHandleWidth(8);
    contentSplitter->setToolTip("Drag this divider to resize the right panel.");

    // =====================================================
    // CENTER: 2x2 View Grid
    // =====================================================
    QGridLayout *viewGrid = new QGridLayout();
    m_axialView = new OrthogonalView();
    m_sagittalView = new OrthogonalView();
    m_coronalView = new OrthogonalView();
    m_mask3DView = new Mask3DView();

    // Slice navigation now lives inside each view panel so it is always visible.
    m_axialSlider = new QSlider(Qt::Horizontal);
    m_axialSlider->setToolTip("Axial slice (W/S keys)");
    m_sagittalSlider = new QSlider(Qt::Horizontal);
    m_sagittalSlider->setToolTip("Sagittal slice (A/D keys)");
    m_coronalSlider = new QSlider(Qt::Horizontal);
    m_coronalSlider->setToolTip("Coronal slice (Q/E keys)");

    m_axialLabel = new QLabel("Axial: 0/0");
    m_sagittalLabel = new QLabel("Sagittal: 0/0");
    m_coronalLabel = new QLabel("Coronal: 0/0");

    for (OrthogonalView *view : {m_axialView, m_sagittalView, m_coronalView})
    {
        view->setFocusPolicy(Qt::StrongFocus);
        view->installEventFilter(this);
    }

    m_axialView->setMinimumSize(360, 280);
    m_sagittalView->setMinimumSize(320, 280);
    m_coronalView->setMinimumSize(320, 280);
    m_mask3DView->setMinimumSize(320, 240);

    auto createSlicePanel = [](const QString &title, OrthogonalView *view, QLabel *label, QSlider *slider) -> QWidget *
    {
        QWidget *panel = new QWidget();
        QVBoxLayout *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        panelLayout->setSpacing(4);

        label->setText(title + ": 0/0");
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        slider->setRange(0, 0);
        slider->setValue(0);
        slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        panelLayout->addWidget(view, 1);
        panelLayout->addWidget(label);
        panelLayout->addWidget(slider);
        return panel;
    };

    QWidget *axialPanel = createSlicePanel("Axial", m_axialView, m_axialLabel, m_axialSlider);
    QWidget *sagittalPanel = createSlicePanel("Sagittal", m_sagittalView, m_sagittalLabel, m_sagittalSlider);
    QWidget *coronalPanel = createSlicePanel("Coronal", m_coronalView, m_coronalLabel, m_coronalSlider);
    QWidget *renderPanel = new QWidget();
    QGridLayout *renderPanelLayout = new QGridLayout(renderPanel);
    renderPanelLayout->setContentsMargins(0, 0, 0, 0);
    renderPanelLayout->setSpacing(0);
    renderPanelLayout->addWidget(m_mask3DView, 0, 0);
    m_show3DCheck = new QCheckBox("Show 3D");
    m_show3DCheck->setToolTip("Enable 3D visualization for masks and seeds");
    m_show3DCheck->setChecked(false);
    m_show3DCheck->setStyleSheet("QCheckBox { background-color: rgba(0, 0, 0, 150); padding: 2px 6px; border-radius: 4px; }");
    renderPanelLayout->addWidget(m_show3DCheck, 0, 0, Qt::AlignRight | Qt::AlignBottom);

    viewGrid->addWidget(axialPanel, 0, 0);
    viewGrid->addWidget(sagittalPanel, 0, 1);
    viewGrid->addWidget(coronalPanel, 1, 0);
    viewGrid->addWidget(renderPanel, 1, 1);
    viewGrid->setColumnStretch(0, 1);
    viewGrid->setColumnStretch(1, 1);
    viewGrid->setRowStretch(0, 1);
    viewGrid->setRowStretch(1, 1);
    viewGrid->setSpacing(6);

    QWidget *viewContainer = new QWidget();
    viewContainer->setLayout(viewGrid);
    viewContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewContainer->setMinimumWidth(640);
    contentSplitter->addWidget(viewContainer);

    // =====================================================
    // RIGHT SIDEBAR: File Management
    // =====================================================
    QWidget *sidebar = new QWidget();
    sidebar->setMinimumWidth(200);
    sidebar->setMaximumWidth(520);
    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setSpacing(4);
    sidebarLayout->setContentsMargins(4, 4, 4, 4);
    QSplitter *sidebarSplitter = new QSplitter(Qt::Vertical, sidebar);
    sidebarSplitter->setChildrenCollapsible(false);
    sidebarSplitter->setHandleWidth(6);
    sidebarSplitter->setToolTip("Drag handles to resize each right-side block.");

    // Label selector at top
    QGroupBox *labelGroup = new QGroupBox("Label");
    labelGroup->setMinimumHeight(90);
    QHBoxLayout *labelLayout = new QHBoxLayout(labelGroup);
    labelLayout->setSpacing(6);

    m_labelSelector = new QSpinBox();
    m_labelSelector->setRange(0, 255);
    m_labelSelector->setToolTip("Select label (0-255) for seeds and mask");
    labelLayout->addWidget(m_labelSelector);

    m_labelColorIndicator = new QLabel();
    m_labelColorIndicator->setFixedSize(24, 24);
    m_labelColorIndicator->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_labelColorIndicator->setToolTip("Color for current label");
    updateLabelColor(0);
    labelLayout->addWidget(m_labelColorIndicator);

    connect(m_labelSelector, QOverload<int>::of(&QSpinBox::valueChanged), this, &ManualSeedSelector::updateLabelColor);

    sidebarSplitter->addWidget(labelGroup);

    // NIfTI Images section
    QGroupBox *niftiListGroup = new QGroupBox("NIfTI Images");
    niftiListGroup->setMinimumHeight(150);
    QVBoxLayout *niftiListLayout = new QVBoxLayout(niftiListGroup);
    niftiListLayout->setSpacing(4);

    m_niftiList = new QListWidget();
    m_niftiList->setMinimumHeight(70);
    m_niftiList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_niftiList->setToolTip("Click to select which image to display");
    niftiListLayout->addWidget(m_niftiList);

    QHBoxLayout *niftiButtonsLayout = new QHBoxLayout();
    niftiButtonsLayout->setContentsMargins(0, 3, 0, 0);
    niftiButtonsLayout->setSpacing(10);

    auto configureNiftiIconButton = [](QToolButton *button)
    {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFixedSize(20, 20);
        button->setIconSize(QSize(18, 18));
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(R"(
            QToolButton {
                background: transparent;
                border: none;
                padding: 0px;
            }
            QToolButton:hover {
                background: transparent;
                border: none;
            }
            QToolButton:pressed {
                background: transparent;
                border: none;
            }
            QToolButton:focus {
                outline: none;
                border: none;
            }
        )");
    };

    QToolButton *btnAddNifti = new QToolButton();
    btnAddNifti->setIcon(makeMonochromeIcon(kAddIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Add, QSize(16, 16))));
    btnAddNifti->setToolTip("Add");
    configureNiftiIconButton(btnAddNifti);
    connect(btnAddNifti, &QToolButton::clicked, this, &ManualSeedSelector::openImage);

    QToolButton *btnAddNiftiCsv = new QToolButton();
    btnAddNiftiCsv->setIcon(makeMonochromeIcon(kAddCsvIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::AddCsv, QSize(16, 16))));
    btnAddNiftiCsv->setToolTip("Add CSV");
    configureNiftiIconButton(btnAddNiftiCsv);
    connect(btnAddNiftiCsv, &QToolButton::clicked, this, &ManualSeedSelector::openImagesFromCsv);

    QToolButton *btnExportNiftiCsv = new QToolButton();
    btnExportNiftiCsv->setIcon(makeMonochromeIcon(kExportCsvIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::ExportCsv, QSize(16, 16))));
    btnExportNiftiCsv->setToolTip("Export CSV");
    configureNiftiIconButton(btnExportNiftiCsv);
    connect(btnExportNiftiCsv, &QToolButton::clicked, [this]()
            {
        if (m_images.empty()) {
            QMessageBox::information(this, "Export CSV", "There are no NIfTI paths to export.");
            return;
        }

        QString outputPath = QFileDialog::getSaveFileName(this, "Export NIfTI Paths CSV", "", "CSV files (*.csv);;All files (*)");
        if (outputPath.isEmpty())
            return;
        if (!outputPath.toLower().endsWith(".csv"))
            outputPath += ".csv";

        QFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Export CSV", QString("Failed to save CSV:\n%1").arg(outputPath));
            return;
        }

        QTextStream stream(&outputFile);
        stream << "nifti_path\n";
        for (const ImageData &imageData : m_images)
            stream << csvEscapeCell(QString::fromStdString(imageData.imagePath)) << '\n';

        if (m_statusLabel)
            m_statusLabel->setText(QString("Exported %1 NIfTI path(s) to %2").arg(m_images.size()).arg(outputPath));
    });

    QToolButton *btnRemoveNifti = new QToolButton();
    btnRemoveNifti->setIcon(makeMonochromeIcon(kRemoveIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Remove, QSize(16, 16))));
    btnRemoveNifti->setToolTip("Remove");
    configureNiftiIconButton(btnRemoveNifti);
    connect(btnRemoveNifti, &QToolButton::clicked, [this]()
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

    QToolButton *btnRemoveAllNifti = new QToolButton();
    btnRemoveAllNifti->setIcon(makeMonochromeIcon(kRemoveAllIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::RemoveAll, QSize(16, 16))));
    btnRemoveAllNifti->setToolTip("Remove All");
    configureNiftiIconButton(btnRemoveAllNifti);
    connect(btnRemoveAllNifti, &QToolButton::clicked, [this]()
            {
        if (m_images.empty())
            return;

        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            "Confirm Remove All",
            "Are you sure you want to remove all NIfTI images from the list?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;

        m_images.clear();
        m_niftiList->clear();
        m_currentImageIndex = -1;
        m_image = NiftiImage();
        m_path.clear();
        m_maskData.clear();
        m_seeds.clear();
        m_mask3DDirty = true;

        m_axialSlider->setRange(0, 0);
        m_sagittalSlider->setRange(0, 0);
        m_coronalSlider->setRange(0, 0);

        m_axialView->setImage(QImage());
        m_sagittalView->setImage(QImage());
        m_coronalView->setImage(QImage());

        updateMaskSeedLists();
        updateViews();
        if (m_statusLabel)
            m_statusLabel->setText("All NIfTI images removed.");
    });

    niftiButtonsLayout->addStretch(1);
    niftiButtonsLayout->addWidget(btnAddNifti);
    niftiButtonsLayout->addWidget(btnAddNiftiCsv);
    niftiButtonsLayout->addWidget(btnExportNiftiCsv);
    niftiButtonsLayout->addWidget(btnRemoveNifti);
    niftiButtonsLayout->addWidget(btnRemoveAllNifti);
    niftiButtonsLayout->addStretch(1);
    niftiListLayout->addLayout(niftiButtonsLayout);

    // Connect item selection to load the image
    connect(m_niftiList, &QListWidget::currentRowChanged, [this](int row)
            {
        if (row >= 0 && row < static_cast<int>(m_images.size())) {
            m_currentImageIndex = row;
            const std::string &path = m_images[row].imagePath;
            if (m_image.load(path)) {
                m_path = path;
                autoDetectAssociatedFilesForImage(row);
                
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

    sidebarSplitter->addWidget(niftiListGroup);

    // Masks section
    QGroupBox *maskListGroup = new QGroupBox("Masks");
    maskListGroup->setMinimumHeight(140);
    QVBoxLayout *maskListLayout = new QVBoxLayout(maskListGroup);
    maskListLayout->setSpacing(4);

    m_maskList = new QListWidget();
    m_maskList->setMinimumHeight(70);
    m_maskList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

    sidebarSplitter->addWidget(maskListGroup);

    // Seed Groups section
    QGroupBox *seedListGroup = new QGroupBox("Seed Groups");
    seedListGroup->setMinimumHeight(140);
    QVBoxLayout *seedListLayout = new QVBoxLayout(seedListGroup);
    seedListLayout->setSpacing(4);

    m_seedList = new QListWidget();
    m_seedList->setMinimumHeight(70);
    m_seedList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

    sidebarSplitter->addWidget(seedListGroup);
    sidebarSplitter->setStretchFactor(0, 0);
    sidebarSplitter->setStretchFactor(1, 1);
    sidebarSplitter->setStretchFactor(2, 1);
    sidebarSplitter->setStretchFactor(3, 1);
    sidebarSplitter->setSizes({100, 300, 220, 220});
    sidebarLayout->addWidget(sidebarSplitter, 1);

    contentSplitter->addWidget(sidebar);
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 0);
    contentSplitter->setSizes({1040, 280});

    mainLayout->addWidget(contentSplitter, 1);

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

    connect(m_show3DCheck, &QCheckBox::toggled, [this](bool checked)
            {
        m_enable3DView = checked;
        if (checked && m_mask3DDirty)
        {
            update3DMaskView();
            m_mask3DDirty = false;
        } });

    // Mouse events for views
    connect(m_axialView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (isMaskTabActive() && m_maskMode != 0 && b == Qt::LeftButton)
            paintAxialMask(x, y);
        else if (!isSeedsTabActive() && !isMaskTabActive() && b == Qt::LeftButton)
            beginSliceDrag(m_axialSliceDrag, y, m_axialSlider);
        else
            onAxialClicked(x, y, b); });
    connect(m_axialView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        updateHoverStatus(SlicePlane::Axial, x, y, m_axialSlider->value());
        if (isMaskTabActive() && (buttons & Qt::LeftButton) && m_maskMode != 0)
            paintAxialMask(x, y);
        else if (isSeedsTabActive() && (buttons & Qt::LeftButton) && m_seedMode == 1)
            addSeed(x, y, m_axialSlider->value());
        else if (isSeedsTabActive() && (buttons & Qt::LeftButton) && m_seedMode == 2)
            eraseNear(x, y, m_axialSlider->value(), m_seedBrushRadius);
        else if (!isSeedsTabActive() && !isMaskTabActive() && (buttons & Qt::LeftButton))
            updateSliceDrag(m_axialSliceDrag, y, static_cast<int>(m_image.getSizeY()), m_axialSlider);
        else
            endSliceDrag(m_axialSliceDrag); });
    connect(m_axialView, &OrthogonalView::mouseReleased, this, [this](int, int, Qt::MouseButton)
            {
        endSliceDrag(m_axialSliceDrag); });

    connect(m_sagittalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (isMaskTabActive() && m_maskMode != 0 && b == Qt::LeftButton)
            paintSagittalMask(x, y);
        else if (!isSeedsTabActive() && !isMaskTabActive() && b == Qt::LeftButton)
            beginSliceDrag(m_sagittalSliceDrag, y, m_sagittalSlider);
        else
            onSagittalClicked(x, y, b); });
    connect(m_sagittalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        updateHoverStatus(SlicePlane::Sagittal, m_sagittalSlider->value(), x, y);
        if (isMaskTabActive() && (buttons & Qt::LeftButton) && m_maskMode != 0)
            paintSagittalMask(x, y);
        else if (isSeedsTabActive() && (buttons & Qt::LeftButton) && m_seedMode == 1)
            addSeed(m_sagittalSlider->value(), x, y);
        else if (isSeedsTabActive() && (buttons & Qt::LeftButton) && m_seedMode == 2)
            eraseNear(m_sagittalSlider->value(), x, y, m_seedBrushRadius);
        else if (!isSeedsTabActive() && !isMaskTabActive() && (buttons & Qt::LeftButton))
            updateSliceDrag(m_sagittalSliceDrag, y, static_cast<int>(m_image.getSizeZ()), m_sagittalSlider);
        else
            endSliceDrag(m_sagittalSliceDrag); });
    connect(m_sagittalView, &OrthogonalView::mouseReleased, this, [this](int, int, Qt::MouseButton)
            {
        endSliceDrag(m_sagittalSliceDrag); });

    connect(m_coronalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (isMaskTabActive() && m_maskMode != 0 && b == Qt::LeftButton)
            paintCoronalMask(x, y);
        else if (!isSeedsTabActive() && !isMaskTabActive() && b == Qt::LeftButton)
            beginSliceDrag(m_coronalSliceDrag, y, m_coronalSlider);
        else
            onCoronalClicked(x, y, b); });
    connect(m_coronalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        updateHoverStatus(SlicePlane::Coronal, x, m_coronalSlider->value(), y);
        if (isMaskTabActive() && (buttons & Qt::LeftButton) && m_maskMode != 0)
            paintCoronalMask(x, y);
        else if (isSeedsTabActive() && (buttons & Qt::LeftButton) && m_seedMode == 1)
            addSeed(x, m_coronalSlider->value(), y);
        else if (isSeedsTabActive() && (buttons & Qt::LeftButton) && m_seedMode == 2)
            eraseNear(x, m_coronalSlider->value(), y, m_seedBrushRadius);
        else if (!isSeedsTabActive() && !isMaskTabActive() && (buttons & Qt::LeftButton))
            updateSliceDrag(m_coronalSliceDrag, y, static_cast<int>(m_image.getSizeZ()), m_coronalSlider);
        else
            endSliceDrag(m_coronalSliceDrag); });
    connect(m_coronalView, &OrthogonalView::mouseReleased, this, [this](int, int, Qt::MouseButton)
            {
        endSliceDrag(m_coronalSliceDrag); });

    connect(m_ribbonTabs, &QTabWidget::currentChanged, this, [this](int)
            {
        if (m_mask3DView)
            m_mask3DView->setSeedRectangleEraseEnabled(isSeedsTabActive() && m_seedMode == 2); });

    connect(m_mask3DView, &Mask3DView::eraseSeedsInRectangle, this, [this](const QVector<int> &seedIndices)
            {
        if (seedIndices.isEmpty() || m_seeds.empty())
            return;

        std::vector<char> removeMask(m_seeds.size(), 0);
        bool hasRemovals = false;
        for (int idx : seedIndices)
        {
            if (idx >= 0 && idx < static_cast<int>(m_seeds.size()) && !removeMask[static_cast<size_t>(idx)])
            {
                removeMask[static_cast<size_t>(idx)] = 1;
                hasRemovals = true;
            }
        }
        if (!hasRemovals)
            return;

        std::vector<Seed> kept;
        kept.reserve(m_seeds.size());
        for (size_t i = 0; i < m_seeds.size(); ++i)
        {
            if (!removeMask[i])
                kept.push_back(m_seeds[i]);
        }
        m_seeds.swap(kept);
        updateViews(); });

    if (m_mask3DView)
        m_mask3DView->setSeedRectangleEraseEnabled(isSeedsTabActive() && m_seedMode == 2);
}

// =============================================================================
// IMAGE I/O
// =============================================================================

void ManualSeedSelector::openImage()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Open NIfTI Images", "", "NIfTI files (*.nii *.nii.gz)");
    if (files.isEmpty())
        return;

    int duplicateCount = 0;
    int missingCount = 0;
    const int added = addImagesToList(files, &duplicateCount, &missingCount);

    if (m_statusLabel)
    {
        QString status = QString("Added %1 NIfTI image(s)").arg(added);
        if (duplicateCount > 0)
            status += QString(", %1 duplicate(s) skipped").arg(duplicateCount);
        if (missingCount > 0)
            status += QString(", %1 missing file(s) skipped").arg(missingCount);
        m_statusLabel->setText(status);
    }
}

void ManualSeedSelector::openImagesFromCsv()
{
    const QString csvPath = QFileDialog::getOpenFileName(this, "Open CSV with NIfTI paths", "", "CSV files (*.csv);;All files (*)");
    if (csvPath.isEmpty())
        return;

    QString errorMessage;
    const QStringList paths = extractNiftiPathsFromCsv(csvPath, &errorMessage);
    if (paths.isEmpty())
    {
        QMessageBox::warning(this, "Open CSV", errorMessage.isEmpty() ? "No valid NIfTI paths were found in the selected CSV." : errorMessage);
        return;
    }

    int duplicateCount = 0;
    int missingCount = 0;
    const int added = addImagesToList(paths, &duplicateCount, &missingCount);

    QString summary = QString("CSV processed successfully.\nDetected paths: %1\nAdded images: %2")
                          .arg(paths.size())
                          .arg(added);
    if (duplicateCount > 0)
        summary += QString("\nDuplicates skipped: %1").arg(duplicateCount);
    if (missingCount > 0)
        summary += QString("\nMissing files skipped: %1").arg(missingCount);

    if (m_statusLabel)
        m_statusLabel->setText(QString("CSV imported: %1 added, %2 duplicate(s), %3 missing")
                                   .arg(added)
                                   .arg(duplicateCount)
                                   .arg(missingCount));

    if (added == 0)
        QMessageBox::warning(this, "Open CSV", summary);
    else
        QMessageBox::information(this, "Open CSV", summary);
}

int ManualSeedSelector::addImagesToList(const QStringList &paths, int *duplicateCount, int *missingCount)
{
    if (duplicateCount)
        *duplicateCount = 0;
    if (missingCount)
        *missingCount = 0;
    if (paths.isEmpty())
        return 0;

    std::unordered_set<std::string> existing;
    existing.reserve(m_images.size() + paths.size());
    for (const ImageData &image : m_images)
    {
        const QString normalized = QFileInfo(QString::fromStdString(image.imagePath)).absoluteFilePath();
        existing.insert(normalized.toStdString());
    }

    int firstAddedIndex = -1;
    int added = 0;

    for (const QString &entry : paths)
    {
        const QString cleaned = normalizeCsvCell(entry);
        if (cleaned.isEmpty())
            continue;

        const QFileInfo fileInfo(cleaned);
        const QString normalized = QDir::cleanPath(fileInfo.absoluteFilePath());
        const std::string key = normalized.toStdString();

        if (existing.find(key) != existing.end())
        {
            if (duplicateCount)
                ++(*duplicateCount);
            continue;
        }
        if (!QFileInfo::exists(normalized))
        {
            if (missingCount)
                ++(*missingCount);
            continue;
        }

        ImageData imageData;
        imageData.imagePath = key;
        imageData.color = getColorForImageIndex(static_cast<int>(m_images.size()));
        m_images.push_back(std::move(imageData));

        const QString filename = QFileInfo(normalized).fileName();
        m_niftiList->addItem(filename.isEmpty() ? normalized : filename);
        if (firstAddedIndex < 0)
            firstAddedIndex = m_niftiList->count() - 1;

        existing.insert(key);
        ++added;
    }

    if (added > 0 && firstAddedIndex >= 0)
        m_niftiList->setCurrentRow(firstAddedIndex);

    return added;
}

QStringList ManualSeedSelector::extractNiftiPathsFromCsv(const QString &csvPath, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    QFile csvFile(csvPath);
    if (!csvFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = QString("Failed to open CSV file:\n%1").arg(csvPath);
        return {};
    }

    QTextStream stream(&csvFile);
    QString headerLine;
    while (!stream.atEnd() && headerLine.trimmed().isEmpty())
        headerLine = stream.readLine();

    if (headerLine.trimmed().isEmpty())
    {
        if (errorMessage)
            *errorMessage = "CSV appears to be empty.";
        return {};
    }

    const QStringList headers = parseCsvRow(headerLine);
    if (headers.isEmpty())
    {
        if (errorMessage)
            *errorMessage = "CSV header could not be parsed.";
        return {};
    }

    std::vector<QStringList> rows;
    rows.reserve(512);
    std::vector<int> niftiCounts(headers.size(), 0);

    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        if (line.trimmed().isEmpty())
            continue;

        QStringList columns = parseCsvRow(line);
        if (columns.size() < headers.size())
        {
            while (columns.size() < headers.size())
                columns.push_back(QString());
        }
        if (columns.size() > headers.size())
            columns = columns.mid(0, headers.size());

        for (int i = 0; i < columns.size(); ++i)
        {
            if (isNiftiPathCell(columns[i]))
                ++niftiCounts[i];
        }

        rows.push_back(columns);
    }

    if (rows.empty())
    {
        if (errorMessage)
            *errorMessage = "CSV has no data rows.";
        return {};
    }

    const int pathColumn = chooseNiftiColumn(headers, niftiCounts);
    if (pathColumn < 0)
    {
        if (errorMessage)
            *errorMessage = "Could not find a column containing NIfTI paths (.nii/.nii.gz).";
        return {};
    }

    const QDir baseDir = QFileInfo(csvPath).dir();
    QStringList extractedPaths;
    extractedPaths.reserve(static_cast<int>(rows.size()));
    std::unordered_set<std::string> uniquePaths;
    uniquePaths.reserve(rows.size());

    for (const QStringList &row : rows)
    {
        if (pathColumn < 0 || pathColumn >= row.size())
            continue;

        const QString rawPath = normalizeCsvCell(row[pathColumn]);
        if (!isNiftiPathCell(rawPath))
            continue;

        const QFileInfo rowPathInfo(rawPath);
        const QString resolvedPath = rowPathInfo.isAbsolute()
                                         ? QDir::cleanPath(rowPathInfo.filePath())
                                         : QDir::cleanPath(baseDir.absoluteFilePath(rawPath));
        const std::string key = resolvedPath.toStdString();
        if (uniquePaths.insert(key).second)
            extractedPaths.push_back(resolvedPath);
    }

    if (extractedPaths.isEmpty() && errorMessage)
        *errorMessage = QString("Column '%1' was found, but no valid NIfTI paths were extracted.").arg(headers[pathColumn]);

    return extractedPaths;
}

void ManualSeedSelector::autoDetectAssociatedFilesForImage(int imageIndex)
{
    if (imageIndex < 0 || imageIndex >= static_cast<int>(m_images.size()))
        return;

    ImageData &imageData = m_images[imageIndex];
    const QString imagePath = QDir::cleanPath(QFileInfo(QString::fromStdString(imageData.imagePath)).absoluteFilePath());
    const QDir imageDir = QFileInfo(imagePath).dir();
    if (!imageDir.exists())
        return;

    std::vector<std::string> detectedMaskPaths;
    std::vector<std::string> detectedSeedPaths;
    detectedMaskPaths.reserve(32);
    detectedSeedPaths.reserve(32);

    std::unordered_set<std::string> maskKeys;
    std::unordered_set<std::string> seedKeys;
    maskKeys.reserve(64);
    seedKeys.reserve(64);

    auto appendUniqueExistingPath = [](const std::string &rawPath, std::vector<std::string> &target, std::unordered_set<std::string> &keySet)
    {
        const QString absolutePath = QDir::cleanPath(QFileInfo(QString::fromStdString(rawPath)).absoluteFilePath());
        if (!QFileInfo::exists(absolutePath))
            return;
        const std::string key = absolutePath.toStdString();
        if (keySet.insert(key).second)
            target.push_back(key);
    };

    const QFileInfoList files = imageDir.entryInfoList(QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fileInfo : files)
    {
        const QString fileName = fileInfo.fileName();
        const std::string absolutePath = QDir::cleanPath(fileInfo.absoluteFilePath()).toStdString();

        if (isNiftiMaskFilenameCandidate(fileName))
        {
            if (maskKeys.insert(absolutePath).second)
                detectedMaskPaths.push_back(absolutePath);
        }
        if (isSeedFilenameCandidate(fileName))
        {
            if (seedKeys.insert(absolutePath).second)
                detectedSeedPaths.push_back(absolutePath);
        }
    }

    // Preserve already-associated files (for example, manually loaded files outside the image folder).
    for (const std::string &existingMaskPath : imageData.maskPaths)
        appendUniqueExistingPath(existingMaskPath, detectedMaskPaths, maskKeys);
    for (const std::string &existingSeedPath : imageData.seedPaths)
        appendUniqueExistingPath(existingSeedPath, detectedSeedPaths, seedKeys);

    imageData.maskPaths = std::move(detectedMaskPaths);
    imageData.seedPaths = std::move(detectedSeedPaths);
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
                s.fromFile = true;
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
        s.fromFile = true;
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

bool ManualSeedSelector::isSeedsTabActive() const
{
    return m_ribbonTabs && m_seedTabIndex >= 0 && m_ribbonTabs->currentIndex() == m_seedTabIndex;
}

bool ManualSeedSelector::isMaskTabActive() const
{
    return m_ribbonTabs && m_maskTabIndex >= 0 && m_ribbonTabs->currentIndex() == m_maskTabIndex;
}

void ManualSeedSelector::beginSliceDrag(SliceDragState &state, int coord, QSlider *slider)
{
    if (!slider || coord < 0)
    {
        state.active = false;
        return;
    }
    state.active = true;
    state.startCoord = coord;
    state.startValue = slider->value();
}

void ManualSeedSelector::updateSliceDrag(SliceDragState &state, int coord, int coordRange, QSlider *slider)
{
    if (!state.active || !slider || coord < 0 || coordRange <= 1)
        return;

    const int minValue = slider->minimum();
    const int maxValue = slider->maximum();
    if (maxValue <= minValue)
        return;

    const double normalizedDelta = static_cast<double>(coord - state.startCoord) / static_cast<double>(coordRange - 1);
    const int scaledDelta = static_cast<int>(std::round(normalizedDelta * static_cast<double>(maxValue - minValue)));
    const int targetValue = std::clamp(state.startValue + scaledDelta, minValue, maxValue);
    if (targetValue != slider->value())
        slider->setValue(targetValue);
}

void ManualSeedSelector::endSliceDrag(SliceDragState &state)
{
    state.active = false;
}

const Seed *ManualSeedSelector::findSeedNearCursor(int x, int y, int z, SlicePlane plane, int maxDistance) const
{
    if (maxDistance < 0)
        maxDistance = 0;
    const int maxDistSquared = maxDistance * maxDistance;
    const Seed *nearest = nullptr;
    int nearestDistSquared = std::numeric_limits<int>::max();
    for (const Seed &seed : m_seeds)
    {
        int d0 = 0;
        int d1 = 0;
        switch (plane)
        {
        case SlicePlane::Axial:
            if (seed.z != z)
                continue;
            d0 = seed.x - x;
            d1 = seed.y - y;
            break;
        case SlicePlane::Sagittal:
            if (seed.x != x)
                continue;
            d0 = seed.y - y;
            d1 = seed.z - z;
            break;
        case SlicePlane::Coronal:
            if (seed.y != y)
                continue;
            d0 = seed.x - x;
            d1 = seed.z - z;
            break;
        }

        const int distSquared = d0 * d0 + d1 * d1;
        if (distSquared > maxDistSquared || distSquared >= nearestDistSquared)
            continue;

        nearest = &seed;
        nearestDistSquared = distSquared;
        if (distSquared == 0)
            break;
    }
    return nearest;
}

void ManualSeedSelector::updateHoverStatus(SlicePlane plane, int x, int y, int z)
{
    if (!m_statusLabel || !hasImage())
        return;

    if (x < 0 || y < 0 || z < 0)
        return;

    const int sizeX = static_cast<int>(m_image.getSizeX());
    const int sizeY = static_cast<int>(m_image.getSizeY());
    const int sizeZ = static_cast<int>(m_image.getSizeZ());
    if (x >= sizeX || y >= sizeY || z >= sizeZ)
        return;

    const float voxelValue = m_image.getVoxelValue(static_cast<unsigned int>(x), static_cast<unsigned int>(y), static_cast<unsigned int>(z));
    QString viewName;
    switch (plane)
    {
    case SlicePlane::Axial:
        viewName = "Axial";
        break;
    case SlicePlane::Sagittal:
        viewName = "Sagittal";
        break;
    case SlicePlane::Coronal:
        viewName = "Coronal";
        break;
    }

    QString hoverText = QString("%1 | x:%2 y:%3 z:%4 | Voxel:%5")
                            .arg(viewName)
                            .arg(x)
                            .arg(y)
                            .arg(z)
                            .arg(QString::number(static_cast<double>(voxelValue), 'f', 3));

    const Seed *seed = findSeedNearCursor(x, y, z, plane, 1);
    if (seed)
    {
        const QString seedType = (seed->internal != 0) ? "internal" : "external";
        hoverText += QString(" | Seed: label %1 (%2)").arg(seed->label).arg(seedType);
    }

    m_statusLabel->setText(hoverText);
}

void ManualSeedSelector::onAxialClicked(int x, int y, Qt::MouseButton b)
{
    if (!isSeedsTabActive())
        return;

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
    if (!isSeedsTabActive())
        return;

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
    if (!isSeedsTabActive())
        return;

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
    s.fromFile = false;
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
    int l = std::max(0, std::min(255, label));
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
    else if (m_enable3DView && m_mask3DView)
    {
        std::vector<SeedRenderData> seedRenderData;
        seedRenderData.reserve(m_seeds.size());
        for (size_t i = 0; i < m_seeds.size(); ++i)
        {
            const Seed &s = m_seeds[i];
            SeedRenderData d;
            d.x = s.x;
            d.y = s.y;
            d.z = s.z;
            d.label = s.label;
            d.seedIndex = static_cast<int>(i);
            seedRenderData.push_back(d);
        }
        m_mask3DView->setSeedData(seedRenderData);
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
                    int dl = std::max(0, std::min(255, lbl));
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
                    int dl = std::max(0, std::min(255, lbl));
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
                    int dl = std::max(0, std::min(255, lbl));
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
            int lbl = std::max(0, std::min(255, s.label));
            const QColor fillColor = colorForLabel(lbl);
            const QColor outlineColor = s.fromFile ? ((s.internal != 0) ? QColor(Qt::white) : QColor(Qt::black)) : fillColor;
            p.setPen(QPen(outlineColor, 1.0));
            p.setBrush(fillColor);
            p.drawEllipse(QPoint(int(s.x * scale), int(s.y * scale)), 2, 2);
        } });

    m_sagittalView->setOverlayDraw([this, sagX](QPainter &p, float scale)
                                   {
        for (auto &s : m_seeds)
        {
            if (s.x != sagX)
                continue;
            int lbl = std::max(0, std::min(255, s.label));
            const QColor fillColor = colorForLabel(lbl);
            const QColor outlineColor = s.fromFile ? ((s.internal != 0) ? QColor(Qt::white) : QColor(Qt::black)) : fillColor;
            p.setPen(QPen(outlineColor, 1.0));
            p.setBrush(fillColor);
            p.drawEllipse(QPoint(int(s.y * scale), int(s.z * scale)), 2, 2);
        } });

    m_coronalView->setOverlayDraw([this, corY](QPainter &p, float scale)
                                  {
        for (auto &s : m_seeds)
        {
            if (s.y != corY)
                continue;
            int lbl = std::max(0, std::min(255, s.label));
            const QColor fillColor = colorForLabel(lbl);
            const QColor outlineColor = s.fromFile ? ((s.internal != 0) ? QColor(Qt::white) : QColor(Qt::black)) : fillColor;
            p.setPen(QPen(outlineColor, 1.0));
            p.setBrush(fillColor);
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

    std::vector<SeedRenderData> seedRenderData;
    seedRenderData.reserve(m_seeds.size());
    for (size_t i = 0; i < m_seeds.size(); ++i)
    {
        const Seed &s = m_seeds[i];
        SeedRenderData d;
        d.x = s.x;
        d.y = s.y;
        d.z = s.z;
        d.label = s.label;
        d.seedIndex = static_cast<int>(i);
        seedRenderData.push_back(d);
    }
    m_mask3DView->setSeedData(seedRenderData);
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
