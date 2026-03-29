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
#include <QGuiApplication>
#include <QClipboard>
#include <QCursor>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QPixmap>
#include <QPainter>
#include <QFontMetrics>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTabWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFrame>
#include <QToolButton>
#include <QSplitter>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QTime>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QWindow>

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
#include <thread>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>
#include <QUrl>
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

#include "UiUtils.h"

using namespace UiUtils;


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
            QListWidgetItem *niftiItem = new QListWidgetItem(QString::fromStdString(filename));
            niftiItem->setData(Qt::UserRole, QFileInfo(QString::fromStdString(niftiPath)).absoluteFilePath());
            m_niftiList->addItem(niftiItem);
            renumberNiftiListItems();
            m_niftiList->setCurrentRow(0);
            m_currentImageIndex = 0;

            // Clear any previously loaded mask data when a new image is loaded
            if (!m_maskData.empty())
            {
                std::cerr << "ManualSeedSelector: clearing existing mask buffer due to new image load" << std::endl;
                m_maskData.clear();
                m_maskDimX = 0;
                m_maskDimY = 0;
                m_maskDimZ = 0;
                m_mask3DDirty = true;
            }
            m_maskSpacingX = m_image.getSpacingX();
            m_maskSpacingY = m_image.getSpacingY();
            m_maskSpacingZ = m_image.getSpacingZ();
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
            configureWindowControls(gmin, gmax,
                                    m_windowSlider,
                                    m_windowLevelSpin,
                                    m_windowWidthSpin,
                                    &m_windowGlobalMin,
                                    &m_windowGlobalMax,
                                    &m_windowLow,
                                    &m_windowHigh);

            clearRulerMeasurements();
            updateViews();
        }
    }
}

ManualSeedSelector::~ManualSeedSelector()
{
    stopSegmentationWorker(true);
}

bool ManualSeedSelector::useLegacyBinaryMode() const
{
    return m_segmentationModeCombo && m_segmentationModeCombo->currentIndex() == 1;
}

void ManualSeedSelector::refreshSegmentationProgressDisplay()
{
    if (!m_segmentationProgressBar)
        return;

    const QString label = m_segmentationProgressLabel.trimmed().isEmpty()
                              ? "Segmentation running..."
                              : m_segmentationProgressLabel.trimmed();
    const int queuedCount = static_cast<int>(m_pendingSegmentationTasks.size());
    const QString queueSuffix =
        queuedCount > 0 ? QString(" | queue %1").arg(queuedCount) : QString();

    if (m_segmentationProgressTotal > 0)
    {
        const int clampedTotal = std::max(1, m_segmentationProgressTotal);
        const int clampedDone = std::clamp(m_segmentationProgressDone, 0, clampedTotal);
        m_segmentationProgressBar->setRange(0, clampedTotal);
        m_segmentationProgressBar->setValue(clampedDone);
        m_segmentationProgressBar->setFormat(QString("%1 %2/%3 (%p%)%4")
                                                 .arg(label)
                                                 .arg(clampedDone)
                                                 .arg(clampedTotal)
                                                 .arg(queueSuffix));
    }
    else
    {
        m_segmentationProgressBar->setRange(0, 0);
        m_segmentationProgressBar->setFormat(label + queueSuffix);
    }
    m_segmentationProgressBar->setVisible(m_segmentationWorkerActive.load());
}

void ManualSeedSelector::launchSegmentationTask(PendingSegmentationTask &&task)
{
    if (m_segmentationWorker.joinable())
        m_segmentationWorker.join();

    m_segmentationWorkerActive.store(true);

    if (!task.initialMessage.trimmed().isEmpty())
        appendSegmentationLog(task.initialMessage);
    for (const QString &line : task.initialLogs)
        appendSegmentationLog(line);
    if (m_statusLabel)
        m_statusLabel->setText("Segmentation started in background.");

    m_segmentationProgressLabel = task.progressLabel;
    m_segmentationProgressDone = 0;
    m_segmentationProgressTotal = task.progressTotal;
    refreshSegmentationProgressDisplay();

    m_segmentationWorker = std::thread([task = std::move(task.task)]() mutable
                                       { task(); });
}

bool ManualSeedSelector::startSegmentationTask(std::function<void()> task,
                                               const QString &initialMessage,
                                               const QStringList &initialLogs,
                                               const QString &progressLabel,
                                               int progressTotal)
{
    if (!task)
        return false;

    PendingSegmentationTask pendingTask{
        std::move(task),
        initialMessage,
        initialLogs,
        progressLabel,
        progressTotal};

    if (m_segmentationWorkerActive.load())
    {
        m_pendingSegmentationTasks.push_back(std::move(pendingTask));
        const int queueSize = static_cast<int>(m_pendingSegmentationTasks.size());
        appendSegmentationLog(QString("Queued segmentation task #%1.").arg(queueSize));
        if (m_statusLabel)
            m_statusLabel->setText(QString("Segmentation queued. Pending: %1").arg(queueSize));
        refreshSegmentationProgressDisplay();
        return true;
    }

    launchSegmentationTask(std::move(pendingTask));
    return true;
}

void ManualSeedSelector::appendSegmentationLog(const QString &message)
{
    if (!m_logConsole)
        return;

    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty())
        return;

    const QString stamped = QString("[%1] %2")
                                .arg(QTime::currentTime().toString("HH:mm:ss"),
                                     trimmed);
    m_logConsole->appendPlainText(stamped);
    m_logConsole->ensureCursorVisible();
}

void ManualSeedSelector::setSegmentationTaskProgress(const QString &message, int done, int total)
{
    m_segmentationProgressLabel = message.trimmed().isEmpty() ? "Segmentation running..." : message.trimmed();
    m_segmentationProgressDone = done;
    m_segmentationProgressTotal = total;
    refreshSegmentationProgressDisplay();
}

int ManualSeedSelector::findImageIndexByPath(const QString &imagePath) const
{
    const QString normalizedTarget = QDir::cleanPath(QFileInfo(imagePath).absoluteFilePath());
    if (normalizedTarget.isEmpty())
        return -1;

    for (int i = 0; i < static_cast<int>(m_images.size()); ++i)
    {
        const QString candidate = QDir::cleanPath(
            QFileInfo(QString::fromStdString(m_images[static_cast<size_t>(i)].imagePath)).absoluteFilePath());
        if (candidate == normalizedTarget)
            return i;
    }
    return -1;
}

void ManualSeedSelector::completeSegmentationTask(bool success,
                                                  const QString &summary,
                                                  const QString &sourceImagePath,
                                                  const QStringList &generatedMaskPaths)
{
    m_segmentationWorkerActive.store(false);
    if (m_segmentationWorker.joinable())
        m_segmentationWorker.join();

    const int sourceImageIndex = findImageIndexByPath(sourceImagePath);
    bool refreshCurrentLists = false;
    std::unordered_set<std::string> seenMaskPaths;
    for (const QString &maskPath : generatedMaskPaths)
    {
        const QString normalizedMaskPath = QDir::cleanPath(QFileInfo(maskPath).absoluteFilePath());
        if (normalizedMaskPath.isEmpty() || !QFileInfo::exists(normalizedMaskPath))
            continue;

        const std::string key = normalizedMaskPath.toStdString();
        if (!seenMaskPaths.insert(key).second)
            continue;

        if (sourceImageIndex >= 0 && sourceImageIndex < static_cast<int>(m_images.size()))
        {
            auto &maskPaths = m_images[static_cast<size_t>(sourceImageIndex)].maskPaths;
            if (std::find(maskPaths.begin(), maskPaths.end(), key) == maskPaths.end())
                maskPaths.push_back(key);
            if (sourceImageIndex == m_currentImageIndex)
                refreshCurrentLists = true;
        }
        else
        {
            if (std::find(m_unassignedMaskPaths.begin(), m_unassignedMaskPaths.end(), key) == m_unassignedMaskPaths.end())
                m_unassignedMaskPaths.push_back(key);
            if (m_currentImageIndex < 0)
                refreshCurrentLists = true;
        }
    }

    if (sourceImageIndex >= 0 && sourceImageIndex < static_cast<int>(m_images.size()))
    {
        autoDetectAssociatedFilesForImage(sourceImageIndex, false);
        if (sourceImageIndex == m_currentImageIndex)
            refreshCurrentLists = true;
    }

    if (refreshCurrentLists)
        updateMaskSeedLists();

    if (!summary.trimmed().isEmpty())
    {
        appendSegmentationLog(summary);
        if (m_statusLabel)
            m_statusLabel->setText(summary);
    }
    else if (m_statusLabel)
    {
        m_statusLabel->setText(success ? "Background segmentation finished." : "Background segmentation failed.");
    }

    if (!m_pendingSegmentationTasks.empty())
    {
        PendingSegmentationTask nextTask = std::move(m_pendingSegmentationTasks.front());
        m_pendingSegmentationTasks.pop_front();
        appendSegmentationLog(
            QString("Starting queued segmentation. Remaining queue: %1")
                .arg(m_pendingSegmentationTasks.size()));
        launchSegmentationTask(std::move(nextTask));
        return;
    }

    m_segmentationProgressLabel.clear();
    m_segmentationProgressDone = -1;
    m_segmentationProgressTotal = -1;
    if (m_segmentationProgressBar)
    {
        m_segmentationProgressBar->setVisible(false);
        m_segmentationProgressBar->setRange(0, 100);
        m_segmentationProgressBar->setValue(0);
        m_segmentationProgressBar->setFormat("Segmentation");
    }
}

void ManualSeedSelector::stopSegmentationWorker(bool waitForJoin)
{
    if (waitForJoin && m_segmentationWorker.joinable())
        m_segmentationWorker.join();

    m_pendingSegmentationTasks.clear();
    m_segmentationWorkerActive.store(false);
    m_segmentationProgressLabel.clear();
    m_segmentationProgressDone = -1;
    m_segmentationProgressTotal = -1;
    if (m_segmentationProgressBar)
        m_segmentationProgressBar->setVisible(false);
}

void ManualSeedSelector::clampWindowToCurrentScreen()
{
    if (m_clampingWindowGeometry || isFullScreen() || isMaximized())
        return;

    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen && windowHandle())
        screen = windowHandle()->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    const QRect avail = screen->availableGeometry();
    const QRect frame = frameGeometry();
    const QRect client = geometry();

    // Convert between frame/client geometries to preserve WM decorations.
    const int leftMargin = client.x() - frame.x();
    const int topMargin = client.y() - frame.y();
    const int rightMargin = frame.right() - client.right();
    const int bottomMargin = frame.bottom() - client.bottom();

    QRect clampedFrame = frame;
    if (clampedFrame.width() > avail.width())
        clampedFrame.setWidth(avail.width());
    if (clampedFrame.height() > avail.height())
        clampedFrame.setHeight(avail.height());
    if (clampedFrame.left() < avail.left())
        clampedFrame.moveLeft(avail.left());
    if (clampedFrame.top() < avail.top())
        clampedFrame.moveTop(avail.top());
    if (clampedFrame.right() > avail.right())
        clampedFrame.moveRight(avail.right());
    if (clampedFrame.bottom() > avail.bottom())
        clampedFrame.moveBottom(avail.bottom());

    if (clampedFrame == frame)
        return;

    QRect newClient(
        clampedFrame.x() + leftMargin,
        clampedFrame.y() + topMargin,
        std::max(1, clampedFrame.width() - leftMargin - rightMargin),
        std::max(1, clampedFrame.height() - topMargin - bottomMargin));

    m_clampingWindowGeometry = true;
    setGeometry(newClient);
    m_clampingWindowGeometry = false;
}

void ManualSeedSelector::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    clampWindowToCurrentScreen();
}

void ManualSeedSelector::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    clampWindowToCurrentScreen();
}

bool ManualSeedSelector::applyMaskFromPath(const std::string &path)
{
    const bool ok = loadMaskFromFile(path);
    if (!ok)
        return false;

    if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
    {
        const std::string absolutePath =
            QDir::cleanPath(QFileInfo(QString::fromStdString(path)).absoluteFilePath()).toStdString();
        auto &maskPaths = m_images[m_currentImageIndex].maskPaths;
        if (std::find(maskPaths.begin(), maskPaths.end(), absolutePath) == maskPaths.end())
            maskPaths.push_back(absolutePath);
    }
    else
    {
        const std::string absolutePath =
            QDir::cleanPath(QFileInfo(QString::fromStdString(path)).absoluteFilePath()).toStdString();
        if (std::find(m_unassignedMaskPaths.begin(), m_unassignedMaskPaths.end(), absolutePath) == m_unassignedMaskPaths.end())
            m_unassignedMaskPaths.push_back(absolutePath);
    }

    refreshAssociatedFilesForCurrentImage();
    updateViews();
    return true;
}

void ManualSeedSelector::refreshAssociatedFilesForCurrentImage(bool forceDetect)
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size()))
    {
        updateMaskSeedLists();
        return;
    }

    autoDetectAssociatedFilesForImage(m_currentImageIndex, forceDetect);
    updateMaskSeedLists();
}

int ManualSeedSelector::addImagesFromPaths(const QStringList &paths)
{
    int duplicateCount = 0;
    int missingCount = 0;
    const int added = addImagesToList(paths, &duplicateCount, &missingCount);

    if (m_statusLabel)
    {
        QString status = QString("Added %1 image(s)").arg(added);
        if (duplicateCount > 0)
            status += QString(", %1 duplicate(s) skipped").arg(duplicateCount);
        if (missingCount > 0)
            status += QString(", %1 missing path(s) skipped").arg(missingCount);
        m_statusLabel->setText(status);
    }

    return added;
}

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
            min-height: 24px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 3px;
            border: 2px solid #666666;
            background-color: #2d2d2d;
        }
        QCheckBox::indicator:hover {
            border-color: #888888;
        }
        QCheckBox::indicator:checked {
            background-color: #0078d4;
            border-color: #0078d4;
            image: none;
        }
        QCheckBox::indicator:disabled {
            border-color: #444444;
            background-color: #252525;
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

    QGroupBox *srGroup = new QGroupBox("Super Resolution");
    QHBoxLayout *srLayout = new QHBoxLayout(srGroup);
    srLayout->setSpacing(8);
    QPushButton *btnRunSuperResolution = new QPushButton("Run SR");
    btnRunSuperResolution->setToolTip("Run super resolution on the current image using super_resolve_nifti.py");
    connect(btnRunSuperResolution, &QPushButton::clicked, this, &ManualSeedSelector::runSuperResolution);
    srLayout->addWidget(btnRunSuperResolution);

    QPushButton *btnPostprocessMask = new QPushButton("Postprocess Mask");
    btnPostprocessMask->setToolTip("Post-process the selected mask to remove isolated artifacts");
    connect(btnPostprocessMask, &QPushButton::clicked, this, &ManualSeedSelector::runMaskPostProcessing);
    srLayout->addWidget(btnPostprocessMask);

    filesLayout->addWidget(srGroup);

    QGroupBox *toolsGroup = new QGroupBox("Tools");
    QHBoxLayout *toolsLayout = new QHBoxLayout(toolsGroup);
    toolsLayout->setSpacing(8);

    m_btnRuler = new QPushButton("Ruler");
    m_btnRuler->setCheckable(true);
    m_btnRuler->setIcon(makeMonochromeIcon(kRulerIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Ruler, QSize(16, 16))));
    m_btnRuler->setIconSize(QSize(16, 16));
    m_btnRuler->setToolTip("Enable the ruler tool in axial, sagittal and coronal views using physical spacing. Drag with left mouse button. Esc clears.");
    connect(m_btnRuler, &QPushButton::toggled, this, [this](bool enabled)
            { setRulerEnabled(enabled); });
    toolsLayout->addWidget(m_btnRuler);

    filesLayout->addWidget(toolsGroup);

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
    m_windowLevelSpin->setDecimals(3);
    m_windowLevelSpin->setSingleStep(1.0);
    m_windowLevelSpin->setToolTip("Window Level");
    windowGrid->addWidget(m_windowLevelSpin, 1, 1);

    windowGrid->addWidget(new QLabel("WW:"), 1, 2);
    m_windowWidthSpin = new QDoubleSpinBox();
    m_windowWidthSpin->setDecimals(3);
    m_windowWidthSpin->setSingleStep(1.0);
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

    seedBrushLayout->addWidget(new QLabel("Display spacing:"), 1, 0);
    m_seedDisplaySpacingSpin = new QSpinBox();
    m_seedDisplaySpacingSpin->setRange(1, 20);
    m_seedDisplaySpacingSpin->setValue(m_seedDisplayMinPixelSpacing);
    m_seedDisplaySpacingSpin->setSuffix(" px");
    m_seedDisplaySpacingSpin->setToolTip("Visual declutter only. 1 px shows all seeds.");
    connect(m_seedDisplaySpacingSpin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int spacing)
            {
        m_seedDisplayMinPixelSpacing = std::max(1, spacing);
        updateViews(); });
    seedBrushLayout->addWidget(m_seedDisplaySpacingSpin, 1, 1);

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

    QGroupBox *seedGenerationGroup = new QGroupBox("Generate Seeds");
    QHBoxLayout *seedGenerationLayout = new QHBoxLayout(seedGenerationGroup);
    seedGenerationLayout->setSpacing(8);
    QPushButton *btnRunLunasSeeds = new QPushButton("LUNAS");
    btnRunLunasSeeds->setToolTip("Run src/lunas.py with --only-seeds");
    connect(btnRunLunasSeeds, &QPushButton::clicked, this, &ManualSeedSelector::runLunasSeedGeneration);
    seedGenerationLayout->addWidget(btnRunLunasSeeds);

    QPushButton *btnRunRibsSeeds = new QPushButton("Ribs");
    btnRunRibsSeeds->setToolTip("Run src/segment_ribs.py with --only-seeds");
    connect(btnRunRibsSeeds, &QPushButton::clicked, this, &ManualSeedSelector::runRibsSeedGeneration);
    seedGenerationLayout->addWidget(btnRunRibsSeeds);
    seedsLayout->addWidget(seedGenerationGroup);

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

    QPushButton *btnMaskThreshold = new QPushButton("Threshold");
    btnMaskThreshold->setToolTip("Remove mask voxels using the current image intensity threshold");
    connect(btnMaskThreshold, &QPushButton::clicked, this, &ManualSeedSelector::filterActiveMaskByThreshold);
    maskFileLayout->addWidget(btnMaskThreshold);

    maskLayout->addWidget(maskFileGroup);

    // (Heatmap feature removed)
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

    paramsGrid->addWidget(new QLabel("Mode:"), 0, 0);
    m_segmentationModeCombo = new QComboBox();
    m_segmentationModeCombo->addItem("Multi-label");
    m_segmentationModeCombo->addItem("Legacy binary");
    m_segmentationModeCombo->setToolTip("Multi-label runs all labels in one execution. Legacy binary restores the original internal-versus-external workflow.");
    paramsGrid->addWidget(m_segmentationModeCombo, 0, 1, 1, 2);

    paramsGrid->addWidget(new QLabel("Polarity:"), 1, 0);
    m_polSlider = new QSlider(Qt::Horizontal);
    m_polSlider->setRange(-100, 100);
    m_polSlider->setValue(100);
    m_polSlider->setToolTip("+1.0=bright inside, -1.0=dark inside");
    paramsGrid->addWidget(m_polSlider, 1, 1);
    m_polValue = new QLabel("1.00");
    m_polValue->setMinimumWidth(40);
    paramsGrid->addWidget(m_polValue, 1, 2);

    paramsGrid->addWidget(new QLabel("Relax iters:"), 2, 0);
    m_niterSlider = new QSlider(Qt::Horizontal);
    m_niterSlider->setRange(0, 100);
    m_niterSlider->setValue(0);
    m_niterSlider->setToolTip("Relaxation iterations");
    paramsGrid->addWidget(m_niterSlider, 2, 1);
    m_niterValue = new QLabel("0");
    m_niterValue->setMinimumWidth(40);
    paramsGrid->addWidget(m_niterValue, 2, 2);

    paramsGrid->addWidget(new QLabel("Percentile:"), 3, 0);
    m_percSlider = new QSlider(Qt::Horizontal);
    m_percSlider->setRange(0, 100);
    m_percSlider->setValue(0);
    m_percSlider->setToolTip("Arc-weight percentile threshold");
    paramsGrid->addWidget(m_percSlider, 3, 1);
    m_percValue = new QLabel("0");
    m_percValue->setMinimumWidth(40);
    paramsGrid->addWidget(m_percValue, 3, 2);

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

    m_segmentAllBox = new QCheckBox("Batch per label");
    m_segmentAllBox->setToolTip("Run one binary segmentation per label and merge the outputs into a multilabel mask.");
    optionsLayout->addWidget(m_segmentAllBox);

    m_polSweepBox = new QCheckBox("Polarity sweep");
    m_polSweepBox->setToolTip("Test polarity range -1.0 to +1.0");
    optionsLayout->addWidget(m_polSweepBox);

    m_useGPUBox = new QCheckBox("Use GPU");
    m_useGPUBox->setToolTip("Use GPU acceleration");
    optionsLayout->addWidget(m_useGPUBox);

    // GPU always uses additive (shortest path) cost — no combo needed

    connect(m_segmentAllBox, &QCheckBox::toggled, [this](bool on)
            {
        m_polSweepBox->setChecked(false);
        m_polSweepBox->setEnabled(!on);
        if (m_segmentationModeCombo)
            m_segmentationModeCombo->setEnabled(!on); });

    segLayout->addWidget(optionsGroup);

    // Run button
    QGroupBox *runGroup = new QGroupBox("Execute");
    QVBoxLayout *runLayout = new QVBoxLayout(runGroup);

    m_btnRunSegment = new QPushButton("Run");
    m_btnRunSegment->setToolTip("Start ROIFT segmentation (Ctrl+Shift+S)");
    m_btnRunSegment->setShortcut(QKeySequence("Ctrl+Shift+S"));
    m_btnRunSegment->setStyleSheet(R"(
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
    connect(m_btnRunSegment, &QPushButton::clicked, [this]()
            {
        SegmentationRunner::runSegmentation(this); });
    runLayout->addWidget(m_btnRunSegment);

    segLayout->addWidget(runGroup);
    segLayout->addStretch();

    m_ribbonTabs->addTab(segTab, "Segmentation");

    mainLayout->addWidget(m_ribbonTabs);

    // =====================================================
    // MAIN CONTENT: View Grid + Right Sidebar (resizable)
    // =====================================================
    QSplitter *contentSplitter = new QSplitter(Qt::Horizontal);
    contentSplitter->setChildrenCollapsible(true);
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

    // Keep minimums modest so OS tiling/snap (Win+arrow) can fit on half-screen
    // without spilling into adjacent monitors.
    m_axialView->setMinimumSize(120, 120);
    m_sagittalView->setMinimumSize(120, 120);
    m_coronalView->setMinimumSize(120, 120);
    m_mask3DView->setMinimumSize(120, 120);

    const QString toggleCheckStyle = "QCheckBox { background-color: rgba(0, 0, 0, 150); padding: 2px 6px; border-radius: 4px; }";
    auto createToggleCheck = [&toggleCheckStyle](const QString &text, const QString &tooltip, bool checked) -> QCheckBox *
    {
        QCheckBox *check = new QCheckBox(text);
        check->setToolTip(tooltip);
        check->setChecked(checked);
        check->setStyleSheet(toggleCheckStyle);
        return check;
    };

    QCheckBox *axialMaskCheck = nullptr;
    QCheckBox *axialSeedsCheck = nullptr;
    QCheckBox *sagittalMaskCheck = nullptr;
    QCheckBox *sagittalSeedsCheck = nullptr;
    QCheckBox *coronalMaskCheck = nullptr;
    QCheckBox *coronalSeedsCheck = nullptr;

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

    auto addSliceToggleRow = [&](QWidget *panel, const QString &viewName,
                                 bool maskEnabled, bool seedsEnabled,
                                 QCheckBox **maskOut, QCheckBox **seedsOut)
    {
        auto *panelLayout = qobject_cast<QVBoxLayout *>(panel->layout());
        if (!panelLayout)
            return;

        QWidget *toggleRow = new QWidget(panel);
        QHBoxLayout *toggleRowLayout = new QHBoxLayout(toggleRow);
        toggleRowLayout->setContentsMargins(0, 0, 0, 0);
        toggleRowLayout->setSpacing(6);
        toggleRowLayout->addStretch();

        QCheckBox *showMask = createToggleCheck("Show Mask", QString("Show mask overlay in %1 view").arg(viewName), maskEnabled);
        QCheckBox *showSeeds = createToggleCheck("Show Seeds", QString("Show seeds in %1 view").arg(viewName), seedsEnabled);

        toggleRowLayout->addWidget(showMask);
        toggleRowLayout->addWidget(showSeeds);

        panelLayout->addWidget(toggleRow);
        if (maskOut)
            *maskOut = showMask;
        if (seedsOut)
            *seedsOut = showSeeds;
    };

    QWidget *axialPanel = createSlicePanel("Axial", m_axialView, m_axialLabel, m_axialSlider);
    QWidget *sagittalPanel = createSlicePanel("Sagittal", m_sagittalView, m_sagittalLabel, m_sagittalSlider);
    QWidget *coronalPanel = createSlicePanel("Coronal", m_coronalView, m_coronalLabel, m_coronalSlider);
    addSliceToggleRow(axialPanel, "axial", m_enableAxialMask, m_enableAxialSeeds,
                      &axialMaskCheck, &axialSeedsCheck);
    addSliceToggleRow(sagittalPanel, "sagittal", m_enableSagittalMask, m_enableSagittalSeeds,
                      &sagittalMaskCheck, &sagittalSeedsCheck);
    addSliceToggleRow(coronalPanel, "coronal", m_enableCoronalMask, m_enableCoronalSeeds,
                      &coronalMaskCheck, &coronalSeedsCheck);

    m_showMaskCheck = axialMaskCheck;

    QWidget *renderPanel = new QWidget();
    QGridLayout *renderPanelLayout = new QGridLayout(renderPanel);
    renderPanelLayout->setContentsMargins(0, 0, 0, 0);
    renderPanelLayout->setSpacing(0);
    renderPanelLayout->addWidget(m_mask3DView, 0, 0);
    QWidget *renderTogglePanel = new QWidget(renderPanel);
    QHBoxLayout *renderToggleLayout = new QHBoxLayout(renderTogglePanel);
    renderToggleLayout->setContentsMargins(0, 0, 0, 0);
    renderToggleLayout->setSpacing(6);

    m_show3DCheck = new QCheckBox("Show 3D");
    m_show3DCheck->setToolTip("Enable 3D mask visualization");
    m_show3DCheck->setChecked(false);
    m_show3DCheck->setStyleSheet(toggleCheckStyle);
    renderToggleLayout->addWidget(m_show3DCheck);

    m_showSeedsCheck = createToggleCheck("Show Seeds", "Show seed points in 3D and slice views", true);
    renderToggleLayout->addWidget(m_showSeedsCheck);

    renderPanelLayout->addWidget(renderTogglePanel, 0, 0, Qt::AlignRight | Qt::AlignBottom);

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
    viewContainer->setMinimumWidth(0);
    contentSplitter->addWidget(viewContainer);

    // =====================================================
    // RIGHT SIDEBAR: File Management
    // =====================================================
    QWidget *sidebar = new QWidget();
    sidebar->setMinimumWidth(100);
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
    m_niftiList->viewport()->installEventFilter(this);
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
            renumberNiftiListItems();
            if (currentRow == m_currentImageIndex) {
                m_currentImageIndex = -1;
                m_image = NiftiImage();
                m_path.clear();
                m_maskSpacingX = 1.0;
                m_maskSpacingY = 1.0;
                m_maskSpacingZ = 1.0;
                clearRulerMeasurements();
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
        m_loadedMaskPath.clear();
        m_maskData.clear();
        m_maskDimX = 0;
        m_maskDimY = 0;
        m_maskDimZ = 0;
        m_seeds.clear();
        m_maskSpacingX = 1.0;
        m_maskSpacingY = 1.0;
        m_maskSpacingZ = 1.0;
        m_mask3DDirty = true;
        clearRulerMeasurements();

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

    m_autoDetectAssociationsCheck = new QCheckBox("Auto-detect masks/seeds");
    m_autoDetectAssociationsCheck->setChecked(m_autoDetectAssociatedFiles);
    m_autoDetectAssociationsCheck->setToolTip("Automatically scan image folder for associated masks (.nii/.nii.gz) and seeds (.txt)");
    connect(m_autoDetectAssociationsCheck, &QCheckBox::toggled, this, [this](bool enabled)
            {
        m_autoDetectAssociatedFiles = enabled;
        if (m_statusLabel)
        {
            m_statusLabel->setText(enabled
                                       ? "Auto-detection of masks/seeds enabled."
                                       : "Auto-detection of masks/seeds disabled.");
        } });
    niftiListLayout->addWidget(m_autoDetectAssociationsCheck);

    // Connect item selection to load the image
    connect(m_niftiList, &QListWidget::currentRowChanged, [this](int row)
            {
        if (row >= 0 && row < static_cast<int>(m_images.size())) {
            const Mask3DView::CameraState preservedCamera = (m_mask3DView != nullptr)
                                                                ? m_mask3DView->captureCameraState()
                                                                : Mask3DView::CameraState{};
            // Persist current slice positions before switching images.
            if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size())) {
                m_images[m_currentImageIndex].lastAxialSlice = m_axialSlider->value();
                m_images[m_currentImageIndex].lastSagittalSlice = m_sagittalSlider->value();
                m_images[m_currentImageIndex].lastCoronalSlice = m_coronalSlider->value();
            }

            const std::string &path = m_images[row].imagePath;
            if (m_image.load(path)) {
                m_currentImageIndex = row;
                m_path = path;
                autoDetectAssociatedFilesForImage(row, false);
                
                // Clear mask and seed data when switching images
                m_loadedMaskPath.clear();
                m_maskData.clear();
                m_maskDimX = 0;
                m_maskDimY = 0;
                m_maskDimZ = 0;
                m_seeds.clear();
                m_maskSpacingX = m_image.getSpacingX();
                m_maskSpacingY = m_image.getSpacingY();
                m_maskSpacingZ = m_image.getSpacingZ();
                m_mask3DDirty = true;
                clearRulerMeasurements();
                
                // Update slider ranges and restore last saved position for this image.
                const int axialMax = std::max(0, static_cast<int>(m_image.getSizeZ()) - 1);
                const int sagittalMax = std::max(0, static_cast<int>(m_image.getSizeX()) - 1);
                const int coronalMax = std::max(0, static_cast<int>(m_image.getSizeY()) - 1);

                m_axialSlider->setRange(0, axialMax);
                m_sagittalSlider->setRange(0, sagittalMax);
                m_coronalSlider->setRange(0, coronalMax);

                int axialValue = m_images[row].lastAxialSlice;
                int sagittalValue = m_images[row].lastSagittalSlice;
                int coronalValue = m_images[row].lastCoronalSlice;

                if (axialValue < 0)
                    axialValue = axialMax / 2;
                if (sagittalValue < 0)
                    sagittalValue = sagittalMax / 2;
                if (coronalValue < 0)
                    coronalValue = coronalMax / 2;

                m_axialSlider->setValue(std::min(std::max(axialValue, 0), axialMax));
                m_sagittalSlider->setValue(std::min(std::max(sagittalValue, 0), sagittalMax));
                m_coronalSlider->setValue(std::min(std::max(coronalValue, 0), coronalMax));
                
                // Window/level setup
                float gmin = m_image.getGlobalMin();
                float gmax = m_image.getGlobalMax();
                configureWindowControls(gmin, gmax,
                                        m_windowSlider,
                                        m_windowLevelSpin,
                                        m_windowWidthSpin,
                                        &m_windowGlobalMin,
                                        &m_windowGlobalMax,
                                        &m_windowLow,
                                        &m_windowHigh);
                
                // Update mask and seed lists for this image
                updateMaskSeedLists();
                updateViews();
                if (m_mask3DView && preservedCamera.valid)
                    m_mask3DView->restoreCameraState(preservedCamera, true);
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

    QHBoxLayout *maskButtonsLayout = new QHBoxLayout();
    maskButtonsLayout->setContentsMargins(0, 3, 0, 0);
    maskButtonsLayout->setSpacing(10);

    QToolButton *btnLoadMask = new QToolButton();
    btnLoadMask->setIcon(makeMonochromeIcon(kLoadIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Load, QSize(16, 16))));
    btnLoadMask->setToolTip("Add");
    configureNiftiIconButton(btnLoadMask);
    connect(btnLoadMask, &QToolButton::clicked, [this]()
            {
        const bool hadImage = hasImage();
        QStringList files = QFileDialog::getOpenFileNames(this, "Open Masks", "", "NIfTI files (*.nii *.nii.gz)");
        if (files.isEmpty())
            return;

        int duplicateCount = 0;
        int missingCount = 0;
        const int added = addMaskPathsToCurrentContext(files, &duplicateCount, &missingCount);
        const QString targetLabel = (resolveMaskTargetImageIndex() >= 0) ? "current image" : "global mask list";
        if (m_statusLabel)
            m_statusLabel->setText(QString("Added %1 mask(s) to %2, %3 duplicate(s), %4 missing")
                                       .arg(added)
                                       .arg(targetLabel)
                                       .arg(duplicateCount)
                                       .arg(missingCount));

        if (!hadImage && added > 0)
        {
            for (const QString &candidate : files)
            {
                const QString abs = QFileInfo(candidate).absoluteFilePath();
                if (QFileInfo::exists(abs))
                {
                    loadMaskFromFile(abs.toStdString());
                    updateViews();
                    break;
                }
            }
        }
    });

    QToolButton *btnLoadMaskCsv = new QToolButton();
    btnLoadMaskCsv->setIcon(makeMonochromeIcon(kAddCsvIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::AddCsv, QSize(16, 16))));
    btnLoadMaskCsv->setToolTip("Add CSV");
    configureNiftiIconButton(btnLoadMaskCsv);
    connect(btnLoadMaskCsv, &QToolButton::clicked, this, &ManualSeedSelector::openMasksFromCsv);

    QToolButton *btnRefreshMasks = new QToolButton();
    btnRefreshMasks->setIcon(makeMonochromeIcon(kRefreshIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Refresh, QSize(16, 16))));
    btnRefreshMasks->setToolTip("Refresh masks and seeds for current image");
    configureNiftiIconButton(btnRefreshMasks);
    connect(btnRefreshMasks, &QToolButton::clicked, [this]()
            {
        refreshAssociatedFilesForCurrentImage(true);
        if (m_statusLabel)
            m_statusLabel->setText("Refreshed masks and seeds for current image.");
    });

    QToolButton *btnRemoveMask = new QToolButton();
    btnRemoveMask->setIcon(makeMonochromeIcon(kRemoveIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Remove, QSize(16, 16))));
    btnRemoveMask->setToolTip("Remove selected mask");
    configureNiftiIconButton(btnRemoveMask);
    connect(btnRemoveMask, &QToolButton::clicked, [this]()
            {
        if (!m_maskList || !m_maskList->currentItem()) {
            QMessageBox::information(this, "Remove Mask", "Please select a mask in the list.");
            return;
        }

        QListWidgetItem *item = m_maskList->currentItem();
        const QString path = QDir::cleanPath(QFileInfo(item->data(kPathRole).toString()).absoluteFilePath());
        const std::string key = path.toStdString();
        const QString activeMaskPath = QDir::cleanPath(QFileInfo(QString::fromStdString(m_loadedMaskPath)).absoluteFilePath());
        const bool removingActiveMask = (!activeMaskPath.isEmpty() && path == activeMaskPath);
        const int sourceImageIndex = item->data(kMaskSourceImageRole).toInt();
        bool removed = false;
        if (sourceImageIndex >= 0 && sourceImageIndex < static_cast<int>(m_images.size())) {
            auto &maskPaths = m_images[static_cast<size_t>(sourceImageIndex)].maskPaths;
            auto it = std::find(maskPaths.begin(), maskPaths.end(), key);
            if (it != maskPaths.end()) {
                maskPaths.erase(it);
                removed = true;
            }
        } else {
            auto it = std::find(m_unassignedMaskPaths.begin(), m_unassignedMaskPaths.end(), key);
            if (it != m_unassignedMaskPaths.end()) {
                m_unassignedMaskPaths.erase(it);
                removed = true;
            }
        }

        if (!removed) {
            QMessageBox::warning(this, "Remove Mask", "Selected mask is not present in the internal list.");
            return;
        }

        if (removingActiveMask)
        {
            m_loadedMaskPath.clear();
            m_maskData.clear();
            m_maskDimX = 0;
            m_maskDimY = 0;
            m_maskDimZ = 0;
            m_mask3DDirty = true;
            updateViews();
        }

        updateMaskSeedLists();
        if (m_statusLabel)
            m_statusLabel->setText(removingActiveMask
                                       ? QString("Removed active mask: %1 (overlay cleared)").arg(path)
                                       : QString("Removed mask: %1").arg(path));
    });

    QToolButton *btnRemoveAllMasks = new QToolButton();
    btnRemoveAllMasks->setIcon(makeMonochromeIcon(kRemoveAllIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::RemoveAll, QSize(16, 16))));
    btnRemoveAllMasks->setToolTip("Remove all masks from current scope");
    configureNiftiIconButton(btnRemoveAllMasks);
    connect(btnRemoveAllMasks, &QToolButton::clicked, [this]()
            {
        if (!m_maskList || m_maskList->count() == 0)
            return;

        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            "Confirm Remove All Masks",
            "Are you sure you want to remove all masks shown in the current list?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;

        std::vector<std::unordered_set<std::string>> perImageToRemove(m_images.size());
        std::unordered_set<std::string> globalToRemove;
        globalToRemove.reserve(static_cast<size_t>(m_maskList->count()));
        const QString activeMaskPath = QDir::cleanPath(QFileInfo(QString::fromStdString(m_loadedMaskPath)).absoluteFilePath());

        for (int i = 0; i < m_maskList->count(); ++i)
        {
            QListWidgetItem *item = m_maskList->item(i);
            if (!item)
                continue;

            const QString path = QDir::cleanPath(QFileInfo(item->data(kPathRole).toString()).absoluteFilePath());
            if (path.isEmpty())
                continue;

            const std::string key = path.toStdString();
            const int sourceImageIndex = item->data(kMaskSourceImageRole).toInt();
            if (sourceImageIndex >= 0 && sourceImageIndex < static_cast<int>(m_images.size()))
                perImageToRemove[static_cast<size_t>(sourceImageIndex)].insert(key);
            else
                globalToRemove.insert(key);
        }

        bool removedActiveMask = false;
        if (!activeMaskPath.isEmpty())
        {
            const std::string activeKey = activeMaskPath.toStdString();
            if (globalToRemove.find(activeKey) != globalToRemove.end())
                removedActiveMask = true;
            if (!removedActiveMask)
            {
                for (const auto &toRemove : perImageToRemove)
                {
                    if (toRemove.find(activeKey) != toRemove.end())
                    {
                        removedActiveMask = true;
                        break;
                    }
                }
            }
        }

        int removedCount = 0;
        for (size_t imageIdx = 0; imageIdx < m_images.size(); ++imageIdx)
        {
            const auto &toRemove = perImageToRemove[imageIdx];
            if (toRemove.empty())
                continue;

            auto &paths = m_images[imageIdx].maskPaths;
            const auto oldSize = paths.size();
            paths.erase(std::remove_if(paths.begin(), paths.end(), [&toRemove](const std::string &p)
                                       { return toRemove.find(p) != toRemove.end(); }),
                        paths.end());
            removedCount += static_cast<int>(oldSize - paths.size());
        }

        if (!globalToRemove.empty())
        {
            const auto oldSize = m_unassignedMaskPaths.size();
            m_unassignedMaskPaths.erase(
                std::remove_if(m_unassignedMaskPaths.begin(), m_unassignedMaskPaths.end(), [&globalToRemove](const std::string &p)
                               { return globalToRemove.find(p) != globalToRemove.end(); }),
                m_unassignedMaskPaths.end());
            removedCount += static_cast<int>(oldSize - m_unassignedMaskPaths.size());
        }

        if (removedActiveMask)
        {
            m_loadedMaskPath.clear();
            m_maskData.clear();
            m_maskDimX = 0;
            m_maskDimY = 0;
            m_maskDimZ = 0;
            m_mask3DDirty = true;
            updateViews();
        }

        updateMaskSeedLists();
        if (m_statusLabel)
            m_statusLabel->setText(removedActiveMask
                                       ? QString("Removed %1 mask(s) from current list scope (active overlay cleared).").arg(removedCount)
                                       : QString("Removed %1 mask(s) from current list scope.").arg(removedCount));
    });

    maskButtonsLayout->addStretch(1);
    maskButtonsLayout->addWidget(btnLoadMask);
    maskButtonsLayout->addWidget(btnLoadMaskCsv);
    maskButtonsLayout->addWidget(btnRefreshMasks);
    maskButtonsLayout->addWidget(btnRemoveMask);
    maskButtonsLayout->addWidget(btnRemoveAllMasks);
    maskButtonsLayout->addStretch(1);
    maskListLayout->addLayout(maskButtonsLayout);

    // Connect item selection to load the mask
    connect(m_maskList, &QListWidget::itemClicked, [this](QListWidgetItem *item)
            {
        if (!item)
            return;

        const QString maskPath = QFileInfo(item->data(kPathRole).toString()).absoluteFilePath();
        if (maskPath.isEmpty()) {
            QMessageBox::warning(this, "Load Mask", "Selected mask item has no valid path.");
            return;
        }

        if (loadMaskFromFile(maskPath.toStdString())) {
            updateViews();
            if (m_statusLabel)
                m_statusLabel->setText(QString("Loaded mask: %1").arg(item->text()));
        }
    });

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

    QHBoxLayout *seedButtonsLayout = new QHBoxLayout();
    seedButtonsLayout->setContentsMargins(0, 3, 0, 0);
    seedButtonsLayout->setSpacing(10);

    QToolButton *btnLoadSeeds = new QToolButton();
    btnLoadSeeds->setIcon(makeMonochromeIcon(kLoadIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Load, QSize(16, 16))));
    btnLoadSeeds->setToolTip("Load seeds for current image");
    configureNiftiIconButton(btnLoadSeeds);
    connect(btnLoadSeeds, &QToolButton::clicked, [this]()
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
        updateMaskSeedLists();
        if (m_statusLabel)
            m_statusLabel->setText(QString("Added %1 seed group(s) to current image.").arg(files.size())); });

    QToolButton *btnRefreshSeeds = new QToolButton();
    btnRefreshSeeds->setIcon(makeMonochromeIcon(kRefreshIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Refresh, QSize(16, 16))));
    btnRefreshSeeds->setToolTip("Refresh masks and seeds for current image");
    configureNiftiIconButton(btnRefreshSeeds);
    connect(btnRefreshSeeds, &QToolButton::clicked, [this]()
            {
        refreshAssociatedFilesForCurrentImage(true);
        if (m_statusLabel)
            m_statusLabel->setText("Refreshed masks and seeds for current image.");
    });

    QToolButton *btnRemoveSeed = new QToolButton();
    btnRemoveSeed->setIcon(makeMonochromeIcon(kRemoveIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::Remove, QSize(16, 16))));
    btnRemoveSeed->setToolTip("Remove selected seed group");
    configureNiftiIconButton(btnRemoveSeed);
    connect(btnRemoveSeed, &QToolButton::clicked, [this]()
            {
        if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size())) {
            QMessageBox::warning(this, "Remove Seed Group", "Please select an image first.");
            return;
        }

        const int row = m_seedList ? m_seedList->currentRow() : -1;
        auto &seedPaths = m_images[m_currentImageIndex].seedPaths;
        if (row < 0 || row >= static_cast<int>(seedPaths.size())) {
            QMessageBox::information(this, "Remove Seed Group", "Please select a seed group in the list.");
            return;
        }

        const QString removedPath = QFileInfo(QString::fromStdString(seedPaths[static_cast<size_t>(row)])).absoluteFilePath();
        seedPaths.erase(seedPaths.begin() + row);
        updateMaskSeedLists();
        if (m_seedList && m_seedList->count() > 0)
            m_seedList->setCurrentRow(std::min(row, m_seedList->count() - 1));
        if (m_statusLabel)
            m_statusLabel->setText(QString("Removed seed group: %1").arg(removedPath));
    });

    QToolButton *btnRemoveAllSeeds = new QToolButton();
    btnRemoveAllSeeds->setIcon(makeMonochromeIcon(kRemoveAllIconSvg, QSize(16, 16), makeFallbackButtonIcon(NiftiButtonIcon::RemoveAll, QSize(16, 16))));
    btnRemoveAllSeeds->setToolTip("Remove all seed groups for current image");
    configureNiftiIconButton(btnRemoveAllSeeds);
    connect(btnRemoveAllSeeds, &QToolButton::clicked, [this]()
            {
        if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size()))
            return;

        auto &seedPaths = m_images[m_currentImageIndex].seedPaths;
        if (seedPaths.empty())
            return;

        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            "Confirm Remove All Seed Groups",
            "Are you sure you want to remove all seed groups from the current image?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;

        const int removedCount = static_cast<int>(seedPaths.size());
        seedPaths.clear();
        updateMaskSeedLists();
        if (m_statusLabel)
            m_statusLabel->setText(QString("Removed %1 seed group(s) from current image.").arg(removedCount));
    });

    seedButtonsLayout->addStretch(1);
    seedButtonsLayout->addWidget(btnLoadSeeds);
    seedButtonsLayout->addWidget(btnRefreshSeeds);
    seedButtonsLayout->addWidget(btnRemoveSeed);
    seedButtonsLayout->addWidget(btnRemoveAllSeeds);
    seedButtonsLayout->addStretch(1);
    seedListLayout->addLayout(seedButtonsLayout);

    // Connect item selection to load the seeds
    connect(m_seedList, &QListWidget::itemClicked, [this](QListWidgetItem *item)
            {
        if (!item || m_currentImageIndex < 0)
            return;

        const QString seedPath = QFileInfo(item->data(Qt::UserRole).toString()).absoluteFilePath();
        if (seedPath.isEmpty())
            return;

        if (loadSeedsFromFile(seedPath.toStdString()))
        {
            updateViews();
            if (m_statusLabel)
                m_statusLabel->setText(QString("Loaded seeds: %1").arg(QFileInfo(seedPath).fileName()));
        } });

    auto handlePathContextAction = [this](const QString &resolvedPath, const QPoint &globalPos)
    {
        const QString cleanPath = QDir::cleanPath(QFileInfo(resolvedPath.trimmed()).absoluteFilePath());
        if (cleanPath.isEmpty())
            return;

        QMenu menu(this);
        QAction *copyPathAction = menu.addAction("Copy Path");
        QAction *revealPathAction = menu.addAction("Reveal File in Explorer");
        QAction *selectedAction = menu.exec(globalPos);
        if (!selectedAction)
            return;

        if (selectedAction == copyPathAction)
        {
            QApplication::clipboard()->setText(cleanPath);
            if (m_statusLabel)
                m_statusLabel->setText(QString("Copied path: %1").arg(cleanPath));
            return;
        }

        if (selectedAction == revealPathAction)
        {
            QString openedPath;
            QString errorMessage;
            if (revealPathInFileManager(cleanPath, &openedPath, &errorMessage))
            {
                if (m_statusLabel)
                {
                    const QString shownPath = openedPath.isEmpty() ? cleanPath : openedPath;
                    m_statusLabel->setText(QString("Opened in file explorer: %1").arg(shownPath));
                }
            }
            else
            {
                QMessageBox::warning(this,
                                     "Reveal File in Explorer",
                                     errorMessage.isEmpty()
                                         ? QString("Failed to open the file explorer for:\n%1").arg(cleanPath)
                                         : QString("%1\n\nPath:\n%2").arg(errorMessage, cleanPath));
            }
        }
    };

    auto installPathContextMenu = [this, handlePathContextAction](QListWidget *listWidget, auto resolvePath)
    {
        if (!listWidget)
            return;
        listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(listWidget, &QListWidget::customContextMenuRequested, this, [listWidget, resolvePath, handlePathContextAction](const QPoint &pos)
                {
            QListWidgetItem *item = listWidget->itemAt(pos);
            if (!item)
                return;

            const QString resolvedPath = resolvePath(item).trimmed();
            if (resolvedPath.isEmpty())
                return;

            handlePathContextAction(resolvedPath, listWidget->viewport()->mapToGlobal(pos));
        });
    };

    installPathContextMenu(m_maskList, [this](QListWidgetItem *item) -> QString
                           {
        if (!item)
            return {};
        QString path = item->data(Qt::UserRole).toString().trimmed();
        if (!path.isEmpty())
            return QFileInfo(path).absoluteFilePath();

        if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size()) || !m_maskList)
            return {};
        const int row = m_maskList->row(item);
        if (row < 0 || row >= static_cast<int>(m_images[m_currentImageIndex].maskPaths.size()))
            return {};
        return QFileInfo(QString::fromStdString(m_images[m_currentImageIndex].maskPaths[static_cast<size_t>(row)])).absoluteFilePath(); });

    installPathContextMenu(m_seedList, [this](QListWidgetItem *item) -> QString
                           {
        if (!item)
            return {};
        QString path = item->data(Qt::UserRole).toString().trimmed();
        if (!path.isEmpty())
            return QFileInfo(path).absoluteFilePath();

        if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size()) || !m_seedList)
            return {};
        const int row = m_seedList->row(item);
        if (row < 0 || row >= static_cast<int>(m_images[m_currentImageIndex].seedPaths.size()))
            return {};
        return QFileInfo(QString::fromStdString(m_images[m_currentImageIndex].seedPaths[static_cast<size_t>(row)])).absoluteFilePath(); });

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
    // BOTTOM: Logs + status bar
    // =====================================================
    QVBoxLayout *bottomSectionLayout = new QVBoxLayout();
    bottomSectionLayout->setContentsMargins(0, 0, 0, 0);
    bottomSectionLayout->setSpacing(6);

    QLabel *logHeader = new QLabel("Logs");
    logHeader->setStyleSheet("QLabel { color: #007acc; font-weight: 600; padding-left: 2px; }");
    bottomSectionLayout->addWidget(logHeader, 0);

    m_logConsole = new QPlainTextEdit();
    m_logConsole->setReadOnly(true);
    m_logConsole->setMinimumHeight(84);
    m_logConsole->setMaximumHeight(120);
    m_logConsole->setFrameShape(QFrame::NoFrame);
    m_logConsole->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_logConsole->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_logConsole->setPlaceholderText("Background segmentation logs will appear here.");
    m_logConsole->document()->setMaximumBlockCount(500);
    m_logConsole->setStyleSheet(R"(
        QPlainTextEdit {
            background-color: #1e1e1e;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            padding: 6px 8px;
            font-family: 'Consolas', 'Courier New', monospace;
            color: #d8d8d8;
        }
    )");
    bottomSectionLayout->addWidget(m_logConsole, 0);

    QHBoxLayout *bottomStatusLayout = new QHBoxLayout();
    bottomStatusLayout->setContentsMargins(0, 0, 0, 0);
    bottomStatusLayout->setSpacing(8);

    m_statusLabel = new QLabel("Ready - Load an image to begin");
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_statusLabel->setMinimumWidth(0);
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setStyleSheet(R"(
        QLabel {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            padding: 6px 12px;
            font-family: 'Consolas', 'Courier New', monospace;
        }
    )");
    bottomStatusLayout->addWidget(m_statusLabel, 1);

    m_segmentationProgressBar = new QProgressBar();
    m_segmentationProgressBar->setRange(0, 0);
    m_segmentationProgressBar->setValue(0);
    m_segmentationProgressBar->setFormat("Segmentation running...");
    m_segmentationProgressBar->setTextVisible(true);
    m_segmentationProgressBar->setVisible(false);
    m_segmentationProgressBar->setMinimumWidth(240);
    m_segmentationProgressBar->setStyleSheet(R"(
        QProgressBar {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            color: #d8d8d8;
            padding: 2px;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #2d7d46;
            border-radius: 3px;
        }
    )");
    bottomStatusLayout->addWidget(m_segmentationProgressBar, 0);

    bottomSectionLayout->addLayout(bottomStatusLayout);
    mainLayout->addLayout(bottomSectionLayout);

    m_viewUpdateTimer = new QTimer(this);
    m_viewUpdateTimer->setSingleShot(true);
    m_viewUpdateTimer->setInterval(33); // ~30 FPS for interactive drawing
    connect(m_viewUpdateTimer, &QTimer::timeout, this, [this]()
            {
        if (!m_viewUpdatePending)
            return;
        m_viewUpdatePending = false;
        updateViews(); });

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
        const float lo = sliderTickToWindowValue(low, m_windowGlobalMin, m_windowGlobalMax);
        const float hi = sliderTickToWindowValue(high, m_windowGlobalMin, m_windowGlobalMax);
        applyWindowFromValues(lo, hi, true); });

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
        if (m_mask3DView)
            m_mask3DView->setMaskVisible(m_enable3DView);
        if (checked && m_mask3DDirty)
        {
            update3DMaskView();
            m_mask3DDirty = false;
        }
        updateViews(); });

    if (axialMaskCheck)
    {
        connect(axialMaskCheck, &QCheckBox::toggled, this, [this](bool checked)
                {
            m_enableAxialMask = checked;
            updateViews(); });
    }
    if (sagittalMaskCheck)
    {
        connect(sagittalMaskCheck, &QCheckBox::toggled, this, [this](bool checked)
                {
            m_enableSagittalMask = checked;
            updateViews(); });
    }
    if (coronalMaskCheck)
    {
        connect(coronalMaskCheck, &QCheckBox::toggled, this, [this](bool checked)
                {
            m_enableCoronalMask = checked;
            updateViews(); });
    }

    if (axialSeedsCheck)
    {
        connect(axialSeedsCheck, &QCheckBox::toggled, this, [this](bool checked)
                {
            m_enableAxialSeeds = checked;
            updateViews(); });
    }
    if (sagittalSeedsCheck)
    {
        connect(sagittalSeedsCheck, &QCheckBox::toggled, this, [this](bool checked)
                {
            m_enableSagittalSeeds = checked;
            updateViews(); });
    }
    if (coronalSeedsCheck)
    {
        connect(coronalSeedsCheck, &QCheckBox::toggled, this, [this](bool checked)
                {
            m_enableCoronalSeeds = checked;
            updateViews(); });
    }

    connect(m_showSeedsCheck, &QCheckBox::toggled, this, [this](bool checked)
            {
        m_enable3DSeeds = checked;
        if (m_mask3DView)
            m_mask3DView->setSeedsVisible(m_enable3DSeeds);
        updateViews(); });

    connect(m_axialView, &OrthogonalView::contextMenuRequested, this, [this](int x, int y, const QPoint &globalPos)
            { showViewContextMenu(SlicePlane::Axial, x, y, globalPos); });
    connect(m_sagittalView, &OrthogonalView::contextMenuRequested, this, [this](int x, int y, const QPoint &globalPos)
            { showViewContextMenu(SlicePlane::Sagittal, x, y, globalPos); });
    connect(m_coronalView, &OrthogonalView::contextMenuRequested, this, [this](int x, int y, const QPoint &globalPos)
            { showViewContextMenu(SlicePlane::Coronal, x, y, globalPos); });

    // Mouse events for views
    connect(m_axialView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (handleRulerMousePress(SlicePlane::Axial, x, y, b))
            return;
        if (isMaskTabActive() && m_maskMode != 0 && b == Qt::LeftButton)
            paintAxialMask(x, y);
        else if (!isSeedsTabActive() && !isMaskTabActive() && b == Qt::LeftButton)
            beginSliceDrag(m_axialSliceDrag, y, m_axialSlider);
        else
            onAxialClicked(x, y, b); });
    connect(m_axialView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        updateHoverStatus(SlicePlane::Axial, x, y, m_axialSlider->value());
        if (handleRulerMouseMove(SlicePlane::Axial, x, y, buttons))
            return;
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
    connect(m_axialView, &OrthogonalView::mouseReleased, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (handleRulerMouseRelease(SlicePlane::Axial, x, y, b))
        {
            requestViewUpdate(true);
            return;
        }
        endSliceDrag(m_axialSliceDrag);
        requestViewUpdate(true); });

    connect(m_sagittalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (handleRulerMousePress(SlicePlane::Sagittal, x, y, b))
            return;
        if (isMaskTabActive() && m_maskMode != 0 && b == Qt::LeftButton)
            paintSagittalMask(x, y);
        else if (!isSeedsTabActive() && !isMaskTabActive() && b == Qt::LeftButton)
            beginSliceDrag(m_sagittalSliceDrag, y, m_sagittalSlider);
        else
            onSagittalClicked(x, y, b); });
    connect(m_sagittalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        updateHoverStatus(SlicePlane::Sagittal, m_sagittalSlider->value(), x, y);
        if (handleRulerMouseMove(SlicePlane::Sagittal, x, y, buttons))
            return;
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
    connect(m_sagittalView, &OrthogonalView::mouseReleased, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (handleRulerMouseRelease(SlicePlane::Sagittal, x, y, b))
        {
            requestViewUpdate(true);
            return;
        }
        endSliceDrag(m_sagittalSliceDrag);
        requestViewUpdate(true); });

    connect(m_coronalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (handleRulerMousePress(SlicePlane::Coronal, x, y, b))
            return;
        if (isMaskTabActive() && m_maskMode != 0 && b == Qt::LeftButton)
            paintCoronalMask(x, y);
        else if (!isSeedsTabActive() && !isMaskTabActive() && b == Qt::LeftButton)
            beginSliceDrag(m_coronalSliceDrag, y, m_coronalSlider);
        else
            onCoronalClicked(x, y, b); });
    connect(m_coronalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            {
        updateHoverStatus(SlicePlane::Coronal, x, m_coronalSlider->value(), y);
        if (handleRulerMouseMove(SlicePlane::Coronal, x, y, buttons))
            return;
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
    connect(m_coronalView, &OrthogonalView::mouseReleased, this, [this](int x, int y, Qt::MouseButton b)
            {
        if (handleRulerMouseRelease(SlicePlane::Coronal, x, y, b))
        {
            requestViewUpdate(true);
            return;
        }
        endSliceDrag(m_coronalSliceDrag);
        requestViewUpdate(true); });

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

void ManualSeedSelector::openMasksFromCsv()
{
    const bool hadImage = hasImage();
    const QString csvPath = QFileDialog::getOpenFileName(this, "Open CSV with mask paths", "", "CSV files (*.csv);;All files (*)");
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
    const int added = addMaskPathsToCurrentContext(paths, &duplicateCount, &missingCount);
    const int targetImageIndex = resolveMaskTargetImageIndex();
    const QString targetLabel = (targetImageIndex >= 0) ? "current image" : "global mask list";

    QString summary = QString("CSV processed successfully.\nDetected paths: %1\nAdded masks to %2: %3")
                          .arg(paths.size())
                          .arg(targetLabel)
                          .arg(added);
    if (duplicateCount > 0)
        summary += QString("\nDuplicates skipped: %1").arg(duplicateCount);
    if (missingCount > 0)
        summary += QString("\nMissing files skipped: %1").arg(missingCount);

    if (m_statusLabel)
        m_statusLabel->setText(QString("Mask CSV imported: %1 added, %2 duplicate(s), %3 missing")
                                   .arg(added)
                                   .arg(duplicateCount)
                                   .arg(missingCount));

    if (added == 0)
        QMessageBox::warning(this, "Open CSV", summary);
    else
        QMessageBox::information(this, "Open CSV", summary);

    if (!hadImage && added > 0)
    {
        for (const QString &candidate : paths)
        {
            const QString abs = QFileInfo(candidate).absoluteFilePath();
            if (QFileInfo::exists(abs))
            {
                loadMaskFromFile(abs.toStdString());
                updateViews();
                break;
            }
        }
    }
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
        QListWidgetItem *item = new QListWidgetItem(filename.isEmpty() ? normalized : filename);
        item->setData(Qt::UserRole, QFileInfo(normalized).absoluteFilePath());
        m_niftiList->addItem(item);
        if (firstAddedIndex < 0)
            firstAddedIndex = m_niftiList->count() - 1;

        existing.insert(key);
        ++added;
    }

    if (added > 0)
        renumberNiftiListItems();

    if (added > 0 && firstAddedIndex >= 0)
        m_niftiList->setCurrentRow(firstAddedIndex);

    return added;
}

bool ManualSeedSelector::appendNiftiImagePath(const QString &path, bool *isDuplicate)
{
    if (isDuplicate)
        *isDuplicate = false;

    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!QFileInfo::exists(normalized))
        return false;

    const std::string key = normalized.toStdString();
    for (const ImageData &imageData : m_images)
    {
        const QString existing = QDir::cleanPath(QFileInfo(QString::fromStdString(imageData.imagePath)).absoluteFilePath());
        if (existing == normalized)
        {
            if (isDuplicate)
                *isDuplicate = true;
            return false;
        }
    }

    ImageData imageData;
    imageData.imagePath = key;
    imageData.color = getColorForImageIndex(static_cast<int>(m_images.size()));
    m_images.push_back(std::move(imageData));

    const QString fileName = QFileInfo(normalized).fileName();
    QListWidgetItem *item = new QListWidgetItem(fileName.isEmpty() ? normalized : fileName);
    item->setData(Qt::UserRole, normalized);
    m_niftiList->addItem(item);
    renumberNiftiListItems();
    return true;
}

void ManualSeedSelector::renumberNiftiListItems()
{
    if (!m_niftiList)
        return;

    for (int i = 0; i < m_niftiList->count(); ++i)
    {
        QListWidgetItem *item = m_niftiList->item(i);
        if (!item)
            continue;
        const QString path = QFileInfo(item->data(Qt::UserRole).toString()).absoluteFilePath();
        const QString fileName = QFileInfo(path).fileName();
        const QString baseText = fileName.isEmpty() ? path : fileName;
        item->setText(QString("%1. %2").arg(i + 1).arg(baseText));
    }
}

int ManualSeedSelector::resolveMaskTargetImageIndex() const
{
    if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
        return m_currentImageIndex;

    if (m_niftiList)
    {
        const int row = m_niftiList->currentRow();
        if (row >= 0 && row < static_cast<int>(m_images.size()))
            return row;
    }

    return -1;
}

int ManualSeedSelector::addMaskPathsToCurrentContext(const QStringList &paths, int *duplicateCount, int *missingCount)
{
    if (duplicateCount)
        *duplicateCount = 0;
    if (missingCount)
        *missingCount = 0;
    if (paths.isEmpty())
        return 0;

    const int targetImageIndex = resolveMaskTargetImageIndex();
    std::vector<std::string> &targetPaths = (targetImageIndex >= 0)
                                                ? m_images[static_cast<size_t>(targetImageIndex)].maskPaths
                                                : m_unassignedMaskPaths;

    std::unordered_set<std::string> existing;
    existing.reserve(targetPaths.size() + paths.size());
    for (const std::string &path : targetPaths)
    {
        const QString normalized = QDir::cleanPath(QFileInfo(QString::fromStdString(path)).absoluteFilePath());
        existing.insert(normalized.toStdString());
    }

    int added = 0;
    for (const QString &entry : paths)
    {
        const QString cleaned = normalizeCsvCell(entry);
        if (cleaned.isEmpty())
            continue;

        const QString normalized = QDir::cleanPath(QFileInfo(cleaned).absoluteFilePath());
        if (!QFileInfo::exists(normalized))
        {
            if (missingCount)
                ++(*missingCount);
            continue;
        }

        const std::string key = normalized.toStdString();
        if (existing.find(key) != existing.end())
        {
            if (duplicateCount)
                ++(*duplicateCount);
            continue;
        }

        targetPaths.push_back(key);
        existing.insert(key);
        ++added;
    }

    updateMaskSeedLists();
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

void ManualSeedSelector::autoDetectAssociatedFilesForImage(int imageIndex, bool force)
{
    if (!m_autoDetectAssociatedFiles && !force)
        return;

    if (imageIndex < 0 || imageIndex >= static_cast<int>(m_images.size()))
        return;

    ImageData &imageData = m_images[imageIndex];
    const QString imagePath = QDir::cleanPath(QFileInfo(QString::fromStdString(imageData.imagePath)).absoluteFilePath());
    const QDir imageDir = QFileInfo(imagePath).dir();
    if (!imageDir.exists())
        return;
    const QString currentImageBaseName = stripNiftiSuffix(QFileInfo(imagePath).fileName()).trimmed().toLower();

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
            const QString candidateBaseName = stripNiftiSuffix(fileName).trimmed().toLower();
            if (candidateBaseName != currentImageBaseName)
            {
                if (maskKeys.insert(absolutePath).second)
                    detectedMaskPaths.push_back(absolutePath);
            }
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

bool ManualSeedSelector::autoLoadAnatomyMasksForCurrentImage(QString *summary)
{
    if (summary)
        summary->clear();

    if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size()))
        return false;
    if (!hasImage())
        return false;

    const unsigned int imageSX = m_image.getSizeX();
    const unsigned int imageSY = m_image.getSizeY();
    const unsigned int imageSZ = m_image.getSizeZ();
    if (imageSX == 0 || imageSY == 0 || imageSZ == 0)
        return false;

    const QString imageBaseName = stripNiftiSuffix(QFileInfo(QString::fromStdString(m_images[static_cast<size_t>(m_currentImageIndex)].imagePath)).fileName()).trimmed().toLower();
    if (imageBaseName.isEmpty())
        return false;

    std::vector<std::string> candidatePaths;
    std::unordered_set<std::string> seen;
    auto appendPath = [&candidatePaths, &seen](const std::string &rawPath)
    {
        const QString absPath = QFileInfo(QString::fromStdString(rawPath)).absoluteFilePath();
        if (absPath.isEmpty() || !QFileInfo::exists(absPath))
            return;
        const std::string key = QDir::cleanPath(absPath).toStdString();
        if (seen.insert(key).second)
            candidatePaths.push_back(key);
    };

    for (const std::string &p : m_images[static_cast<size_t>(m_currentImageIndex)].maskPaths)
        appendPath(p);
    for (const std::string &p : m_unassignedMaskPaths)
        appendPath(p);

    if (candidatePaths.empty())
        return false;

    struct AnatomyMatch
    {
        std::string path;
        bool found = false;
    };

    AnatomyMatch leftMatch, rightMatch, tracheaMatch;
    for (const std::string &path : candidatePaths)
    {
        const QString fileName = QFileInfo(QString::fromStdString(path)).fileName();
        if (!isNiftiMaskFilenameCandidate(fileName))
            continue;
        const QString base = stripNiftiSuffix(fileName).trimmed().toLower();
        if (!base.contains(imageBaseName))
            continue;

        if (!leftMatch.found && base.contains("left_lung"))
        {
            leftMatch.path = path;
            leftMatch.found = true;
        }
        else if (!rightMatch.found && base.contains("right_lung"))
        {
            rightMatch.path = path;
            rightMatch.found = true;
        }
        else if (!tracheaMatch.found && base.contains("trachea"))
        {
            tracheaMatch.path = path;
            tracheaMatch.found = true;
        }
    }

    if (!leftMatch.found && !rightMatch.found && !tracheaMatch.found)
        return false;

    const size_t imagePlane = static_cast<size_t>(imageSX) * static_cast<size_t>(imageSY);
    const size_t imageTotal = imagePlane * static_cast<size_t>(imageSZ);
    m_maskData.assign(imageTotal, 0);
    m_maskDimX = imageSX;
    m_maskDimY = imageSY;
    m_maskDimZ = imageSZ;
    m_maskSpacingX = m_image.getSpacingX();
    m_maskSpacingY = m_image.getSpacingY();
    m_maskSpacingZ = m_image.getSpacingZ();

    using MaskImageType = itk::Image<int32_t, 3>;
    using MaskReaderType = itk::ImageFileReader<MaskImageType>;

    auto applyMask = [&](const std::string &maskPath, int labelValue, const QString &anatomyName) -> bool
    {
        try
        {
            MaskReaderType::Pointer reader = MaskReaderType::New();
            reader->SetFileName(maskPath);
            reader->Update();
            MaskImageType::Pointer img = reader->GetOutput();
            if (!img)
                return false;

            const auto region = img->GetLargestPossibleRegion();
            const auto size = region.GetSize();
            const unsigned int sx = static_cast<unsigned int>(size[0]);
            const unsigned int sy = static_cast<unsigned int>(size[1]);
            const unsigned int sz = static_cast<unsigned int>(size[2]);
            if (sx != imageSX || sy != imageSY || sz == 0)
            {
                if (m_statusLabel)
                {
                    m_statusLabel->setText(QString("Skipping %1 auto-mask: incompatible dimensions (%2x%3x%4 vs %5x%6x%7).")
                                               .arg(anatomyName)
                                               .arg(sx)
                                               .arg(sy)
                                               .arg(sz)
                                               .arg(imageSX)
                                               .arg(imageSY)
                                               .arg(imageSZ));
                }
                return false;
            }

            const size_t srcPlane = static_cast<size_t>(sx) * static_cast<size_t>(sy);
            const size_t srcTotal = srcPlane * static_cast<size_t>(sz);
            std::vector<int> srcMask(srcTotal, 0);
            itk::ImageRegionConstIterator<MaskImageType> it(img, region);
            size_t idx = 0;
            for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++idx)
                srcMask[idx] = static_cast<int>(it.Get());

            if (sz == imageSZ)
            {
                for (size_t i = 0; i < imageTotal; ++i)
                {
                    if (srcMask[i] != 0)
                        m_maskData[i] = labelValue;
                }
            }
            else
            {
                for (unsigned int z = 0; z < imageSZ; ++z)
                {
                    const unsigned int srcZ = mapDepthIndex(z, imageSZ, sz);
                    const size_t srcOffset = static_cast<size_t>(srcZ) * srcPlane;
                    const size_t dstOffset = static_cast<size_t>(z) * imagePlane;
                    for (size_t i = 0; i < imagePlane; ++i)
                    {
                        if (srcMask[srcOffset + i] != 0)
                            m_maskData[dstOffset + i] = labelValue;
                    }
                }
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    };

    int loadedCount = 0;
    QStringList loadedNames;
    // Deterministic order; trachea last so it wins in overlaps.
    if (leftMatch.found && applyMask(leftMatch.path, 1, "left_lung"))
    {
        ++loadedCount;
        loadedNames.push_back("left_lung");
    }
    if (rightMatch.found && applyMask(rightMatch.path, 2, "right_lung"))
    {
        ++loadedCount;
        loadedNames.push_back("right_lung");
    }
    if (tracheaMatch.found && applyMask(tracheaMatch.path, 3, "trachea"))
    {
        ++loadedCount;
        loadedNames.push_back("trachea");
    }

    if (loadedCount == 0)
    {
        m_maskData.clear();
        m_maskDimX = 0;
        m_maskDimY = 0;
        m_maskDimZ = 0;
        return false;
    }

    if (m_show3DCheck && !m_show3DCheck->isChecked())
    {
        QSignalBlocker blocker(m_show3DCheck);
        m_show3DCheck->setChecked(true);
        m_enable3DView = true;
        if (m_mask3DView)
            m_mask3DView->setMaskVisible(true);
    }

    m_mask3DDirty = true;
    if (summary)
        *summary = QString("Auto-loaded anatomy masks: %1").arg(loadedNames.join(", "));
    return true;
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

void ManualSeedSelector::setRulerEnabled(bool enabled)
{
    m_rulerEnabled = enabled;
    if (!enabled)
        clearRulerMeasurements();
    updateRulerCursor();
    requestViewUpdate(true);

    if (m_statusLabel)
    {
        m_statusLabel->setText(enabled
                                   ? "Ruler enabled. Drag with the left mouse button on any slice view to measure real distance."
                                   : "Ruler disabled.");
    }
}

void ManualSeedSelector::clearRulerMeasurements()
{
    m_axialRuler = RulerMeasurement{};
    m_sagittalRuler = RulerMeasurement{};
    m_coronalRuler = RulerMeasurement{};
}

void ManualSeedSelector::updateRulerCursor()
{
    const Qt::CursorShape cursorShape = m_rulerEnabled ? Qt::CrossCursor : Qt::ArrowCursor;
    for (OrthogonalView *view : {m_axialView, m_sagittalView, m_coronalView})
    {
        if (!view)
            continue;
        view->setCursor(cursorShape);
    }
}

ManualSeedSelector::RulerMeasurement &ManualSeedSelector::rulerForPlane(SlicePlane plane)
{
    switch (plane)
    {
    case SlicePlane::Axial:
        return m_axialRuler;
    case SlicePlane::Sagittal:
        return m_sagittalRuler;
    case SlicePlane::Coronal:
        return m_coronalRuler;
    }

    return m_axialRuler;
}

const ManualSeedSelector::RulerMeasurement &ManualSeedSelector::rulerForPlane(SlicePlane plane) const
{
    switch (plane)
    {
    case SlicePlane::Axial:
        return m_axialRuler;
    case SlicePlane::Sagittal:
        return m_sagittalRuler;
    case SlicePlane::Coronal:
        return m_coronalRuler;
    }

    return m_axialRuler;
}

void ManualSeedSelector::beginRulerMeasurement(SlicePlane plane, int planeX, int planeY)
{
    if (planeX < 0 || planeY < 0)
        return;

    RulerMeasurement &ruler = rulerForPlane(plane);
    ruler.visible = true;
    ruler.dragging = true;
    ruler.start = QPoint(planeX, planeY);
    ruler.end = QPoint(planeX, planeY);

    switch (plane)
    {
    case SlicePlane::Axial:
        ruler.sliceIndex = m_axialSlider ? m_axialSlider->value() : -1;
        break;
    case SlicePlane::Sagittal:
        ruler.sliceIndex = m_sagittalSlider ? m_sagittalSlider->value() : -1;
        break;
    case SlicePlane::Coronal:
        ruler.sliceIndex = m_coronalSlider ? m_coronalSlider->value() : -1;
        break;
    }
}

void ManualSeedSelector::updateRulerMeasurement(SlicePlane plane, int planeX, int planeY, bool finalize)
{
    RulerMeasurement &ruler = rulerForPlane(plane);
    if (!ruler.dragging && !finalize)
        return;

    if (planeX >= 0 && planeY >= 0)
        ruler.end = QPoint(planeX, planeY);

    if (finalize)
        ruler.dragging = false;

    ruler.visible = true;
}

QString ManualSeedSelector::formatRulerDistance(double millimeters) const
{
    if (!std::isfinite(millimeters))
        return "n/a";

    if (millimeters >= 10.0)
        return QString("%1 mm").arg(millimeters, 0, 'f', 1);
    return QString("%1 mm").arg(millimeters, 0, 'f', 2);
}

void ManualSeedSelector::endRulerMeasurement(SlicePlane plane, int planeX, int planeY)
{
    updateRulerMeasurement(plane, planeX, planeY, true);

    const RulerMeasurement &ruler = rulerForPlane(plane);
    if (!ruler.visible || !m_statusLabel)
        return;

    double spacingU = 1.0;
    double spacingV = 1.0;
    QString viewName;
    switch (plane)
    {
    case SlicePlane::Axial:
        spacingU = m_image.getSpacingX();
        spacingV = m_image.getSpacingY();
        viewName = "Axial";
        break;
    case SlicePlane::Sagittal:
        spacingU = m_image.getSpacingY();
        spacingV = m_image.getSpacingZ();
        viewName = "Sagittal";
        break;
    case SlicePlane::Coronal:
        spacingU = m_image.getSpacingX();
        spacingV = m_image.getSpacingZ();
        viewName = "Coronal";
        break;
    }

    const double du = std::abs(static_cast<double>(ruler.end.x() - ruler.start.x())) * spacingU;
    const double dv = std::abs(static_cast<double>(ruler.end.y() - ruler.start.y())) * spacingV;
    const double total = std::hypot(du, dv);
    m_statusLabel->setText(QString("%1 ruler | %2 | Δu %3 | Δv %4")
                               .arg(viewName)
                               .arg(formatRulerDistance(total))
                               .arg(formatRulerDistance(du))
                               .arg(formatRulerDistance(dv)));
}

bool ManualSeedSelector::handleRulerMousePress(SlicePlane plane, int planeX, int planeY, Qt::MouseButton button)
{
    if (!m_rulerEnabled || button != Qt::LeftButton)
        return false;

    beginRulerMeasurement(plane, planeX, planeY);
    requestViewUpdate(true);
    return true;
}

bool ManualSeedSelector::handleRulerMouseMove(SlicePlane plane, int planeX, int planeY, Qt::MouseButtons buttons)
{
    if (!m_rulerEnabled)
        return false;

    RulerMeasurement &ruler = rulerForPlane(plane);
    if (!ruler.dragging)
        return false;

    if (!(buttons & Qt::LeftButton))
    {
        ruler.dragging = false;
        return true;
    }

    if (planeX < 0 || planeY < 0)
    {
        ruler.dragging = false;
        requestViewUpdate(true);
        return true;
    }

    updateRulerMeasurement(plane, planeX, planeY, false);
    requestViewUpdate();
    return true;
}

bool ManualSeedSelector::handleRulerMouseRelease(SlicePlane plane, int planeX, int planeY, Qt::MouseButton button)
{
    if (!m_rulerEnabled || button != Qt::LeftButton)
        return false;

    endRulerMeasurement(plane, planeX, planeY);
    return true;
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

void ManualSeedSelector::showViewContextMenu(SlicePlane plane, int planeX, int planeY, const QPoint &globalPos)
{
    if (!hasImage())
        return;

    int vx = -1;
    int vy = -1;
    int vz = -1;
    switch (plane)
    {
    case SlicePlane::Axial:
        vx = planeX;
        vy = planeY;
        vz = m_axialSlider->value();
        break;
    case SlicePlane::Sagittal:
        vx = m_sagittalSlider->value();
        vy = planeX;
        vz = planeY;
        break;
    case SlicePlane::Coronal:
        vx = planeX;
        vy = m_coronalSlider->value();
        vz = planeY;
        break;
    }

    if (vx < 0 || vy < 0 || vz < 0 ||
        vx >= static_cast<int>(m_image.getSizeX()) ||
        vy >= static_cast<int>(m_image.getSizeY()) ||
        vz >= static_cast<int>(m_image.getSizeZ()))
    {
        return;
    }

    QMenu menu(this);
    QAction *eraseSeedsAction = nullptr;
    if (isSeedsTabActive())
    {
        eraseSeedsAction = menu.addAction("Erase seeds near this point");
    }

    QAction *selected = menu.exec(globalPos);
    if (!selected)
        return;

    if (eraseSeedsAction && selected == eraseSeedsAction)
    {
        eraseNear(vx, vy, vz, m_seedBrushRadius);
        if (m_statusLabel)
            m_statusLabel->setText(QString("Erased seeds near x:%1 y:%2 z:%3").arg(vx).arg(vy).arg(vz));
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
    requestViewUpdate(false);
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
    requestViewUpdate(false);
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
        m_windowGlobalMax = m_windowGlobalMin + 1e-3f;

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
        m_windowSlider->setLowerValue(windowValueToSliderTick(clampedLow, m_windowGlobalMin, m_windowGlobalMax));
        m_windowSlider->setUpperValue(windowValueToSliderTick(clampedHigh, m_windowGlobalMin, m_windowGlobalMax));
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

void ManualSeedSelector::requestViewUpdate(bool immediate)
{
    if (immediate || !m_viewUpdateTimer)
    {
        m_viewUpdatePending = false;
        if (m_viewUpdateTimer && m_viewUpdateTimer->isActive())
            m_viewUpdateTimer->stop();
        updateViews();
        return;
    }

    m_viewUpdatePending = true;
    if (!m_viewUpdateTimer->isActive())
        m_viewUpdateTimer->start();
}

void ManualSeedSelector::drawRulerOverlay(QPainter &p,
                                          float scale,
                                          const RulerMeasurement &ruler,
                                          int activeSliceIndex,
                                          double spacingU,
                                          double spacingV) const
{
    if (!m_rulerEnabled || !ruler.visible || ruler.sliceIndex != activeSliceIndex || scale <= 0.0f)
        return;

    const QPointF startPoint(static_cast<qreal>(ruler.start.x()) * scale,
                             static_cast<qreal>(ruler.start.y()) * scale);
    const QPointF endPoint(static_cast<qreal>(ruler.end.x()) * scale,
                           static_cast<qreal>(ruler.end.y()) * scale);

    const double du = std::abs(static_cast<double>(ruler.end.x() - ruler.start.x())) * spacingU;
    const double dv = std::abs(static_cast<double>(ruler.end.y() - ruler.start.y())) * spacingV;
    const QString label = QString("%1 | %2 x %3")
                              .arg(formatRulerDistance(std::hypot(du, dv)))
                              .arg(formatRulerDistance(du))
                              .arg(formatRulerDistance(dv));

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor rulerColor(255, 214, 10);
    p.setPen(QPen(rulerColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(startPoint, endPoint);
    p.setBrush(rulerColor);
    p.drawEllipse(startPoint, 3.0, 3.0);
    p.drawEllipse(endPoint, 3.0, 3.0);

    const QPointF labelAnchor = (startPoint + endPoint) * 0.5 + QPointF(10.0, -10.0);
    const QFontMetrics metrics(p.font());
    const QRect textRect = metrics.boundingRect(label);
    QRectF bubble(labelAnchor.x(),
                  labelAnchor.y() - static_cast<qreal>(textRect.height()),
                  static_cast<qreal>(textRect.width() + 14),
                  static_cast<qreal>(textRect.height() + 8));

    if (bubble.left() < 0.0)
        bubble.moveLeft(0.0);
    if (bubble.top() < 0.0)
        bubble.moveTop(0.0);
    if (bubble.right() > p.viewport().width())
        bubble.moveRight(static_cast<qreal>(p.viewport().width()));
    if (bubble.bottom() > p.viewport().height())
        bubble.moveBottom(static_cast<qreal>(p.viewport().height()));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 175));
    p.drawRoundedRect(bubble, 5.0, 5.0);

    p.setPen(Qt::white);
    p.drawText(bubble.adjusted(7.0, 4.0, -7.0, -4.0), Qt::AlignLeft | Qt::AlignVCenter, label);
    p.restore();
}

void ManualSeedSelector::updateViews()
{
    unsigned int sizeX = m_image.getSizeX();
    unsigned int sizeY = m_image.getSizeY();
    unsigned int sizeZ = m_image.getSizeZ();

    if (m_mask3DView)
    {
        m_mask3DView->setVoxelSpacing(m_maskSpacingX, m_maskSpacingY, m_maskSpacingZ);
        m_mask3DView->setMaskVisible(m_enable3DView);
        m_mask3DView->setSeedsVisible(m_enable3DSeeds);
    }

    // 3D mask rendering is controlled by "Show 3D", seeds are controlled separately.
    if (m_enable3DView && m_mask3DDirty)
    {
        update3DMaskView();
        m_mask3DDirty = false;
    }
    else if (m_mask3DView)
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
        // Mask-only mode: keep 3D renderer active, but clear 2D orthogonal views.
        m_axialView->setImage(QImage());
        m_sagittalView->setImage(QImage());
        m_coronalView->setImage(QImage());
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

    const size_t expectedTotal = size_t(sizeX) * size_t(sizeY) * size_t(sizeZ);
    const bool maskDimsKnown = (m_maskDimX > 0 && m_maskDimY > 0 && m_maskDimZ > 0);
    const size_t expectedMaskTotal = maskDimsKnown ? (size_t(m_maskDimX) * size_t(m_maskDimY) * size_t(m_maskDimZ)) : 0;
    const bool maskBufferShapeValid = (!m_maskData.empty() && maskDimsKnown && m_maskData.size() == expectedMaskTotal);
    const bool maskXYMatchImage = (m_maskDimX == sizeX && m_maskDimY == sizeY);
    bool maskOverlayReady = (maskBufferShapeValid && maskXYMatchImage);
    if (!m_maskData.empty() && !maskOverlayReady)
    {
        std::cerr << "updateViews: mask/image mismatch in X/Y or invalid mask buffer, skipping overlay and clearing mask buffer\n";
        m_maskData.clear();
        m_maskDimX = 0;
        m_maskDimY = 0;
        m_maskDimZ = 0;
        m_maskSpacingX = m_image.getSpacingX();
        m_maskSpacingY = m_image.getSpacingY();
        m_maskSpacingZ = m_image.getSpacingZ();
        m_mask3DDirty = true;
        maskOverlayReady = false;
    }

    // Axial view
    auto axial_rgb = m_image.getAxialSliceAsRGB(z, lo, hi);
    if (m_enableAxialMask && maskOverlayReady)
    {
        const unsigned int mappedZ = mapDepthIndex(static_cast<unsigned int>(z), sizeZ, m_maskDimZ);
        for (unsigned int yy = 0; yy < sizeY; ++yy)
        {
            for (unsigned int xx = 0; xx < sizeX; ++xx)
            {
                const size_t idx3 = size_t(xx) + size_t(yy) * m_maskDimX + size_t(mappedZ) * m_maskDimX * m_maskDimY;
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
    if (m_enableSagittalMask && maskOverlayReady)
    {
        for (unsigned int zz = 0; zz < sizeZ; ++zz)
        {
            const unsigned int mappedZ = mapDepthIndex(zz, sizeZ, m_maskDimZ);
            for (unsigned int yy = 0; yy < sizeY; ++yy)
            {
                const size_t idx3 = size_t(sagX) + size_t(yy) * m_maskDimX + size_t(mappedZ) * m_maskDimX * m_maskDimY;
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
    if (m_enableCoronalMask && maskOverlayReady)
    {
        for (unsigned int zz = 0; zz < sizeZ; ++zz)
        {
            const unsigned int mappedZ = mapDepthIndex(zz, sizeZ, m_maskDimZ);
            for (unsigned int xx = 0; xx < sizeX; ++xx)
            {
                const size_t idx3 = size_t(xx) + size_t(corY) * m_maskDimX + size_t(mappedZ) * m_maskDimX * m_maskDimY;
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

    // Seed overlays (visual declutter only; does not modify m_seeds)
    const int minPixelSpacing = std::max(1, m_seedDisplayMinPixelSpacing);
    const auto makeCellKey = [minPixelSpacing](int px, int py) -> std::uint64_t
    {
        const int cellX = (minPixelSpacing > 0) ? (px / minPixelSpacing) : px;
        const int cellY = (minPixelSpacing > 0) ? (py / minPixelSpacing) : py;
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) << 32) |
               static_cast<std::uint32_t>(cellY);
    };

    m_axialView->setOverlayDraw([this, z, minPixelSpacing, makeCellKey](QPainter &p, float scale)
                                {
        if (m_enableAxialSeeds)
        {
            std::unordered_set<std::uint64_t> occupiedCells;
            if (minPixelSpacing > 1)
                occupiedCells.reserve(m_seeds.size());
            const int markerRadius = (minPixelSpacing >= 5) ? 1 : 2;
            for (const auto &s : m_seeds)
            {
                if (s.z != z)
                    continue;
                const int px = static_cast<int>(std::lround(s.x * scale));
                const int py = static_cast<int>(std::lround(s.y * scale));
                if (minPixelSpacing > 1)
                {
                    if (!occupiedCells.insert(makeCellKey(px, py)).second)
                        continue;
                }
                int lbl = std::max(0, std::min(255, s.label));
                const QColor fillColor = colorForLabel(lbl);
                const QColor outlineColor = s.fromFile ? ((s.internal != 0) ? QColor(Qt::white) : QColor(Qt::black)) : fillColor;
                p.setPen(QPen(outlineColor, 1.0));
                p.setBrush(fillColor);
                p.drawEllipse(QPoint(px, py), markerRadius, markerRadius);
            }
        }
        drawRulerOverlay(p, scale, m_axialRuler, z, m_image.getSpacingX(), m_image.getSpacingY()); });

    m_sagittalView->setOverlayDraw([this, sagX, minPixelSpacing, makeCellKey](QPainter &p, float scale)
                                   {
        if (m_enableSagittalSeeds)
        {
            std::unordered_set<std::uint64_t> occupiedCells;
            if (minPixelSpacing > 1)
                occupiedCells.reserve(m_seeds.size());
            const int markerRadius = (minPixelSpacing >= 5) ? 1 : 2;
            for (const auto &s : m_seeds)
            {
                if (s.x != sagX)
                    continue;
                const int px = static_cast<int>(std::lround(s.y * scale));
                const int py = static_cast<int>(std::lround(s.z * scale));
                if (minPixelSpacing > 1)
                {
                    if (!occupiedCells.insert(makeCellKey(px, py)).second)
                        continue;
                }
                int lbl = std::max(0, std::min(255, s.label));
                const QColor fillColor = colorForLabel(lbl);
                const QColor outlineColor = s.fromFile ? ((s.internal != 0) ? QColor(Qt::white) : QColor(Qt::black)) : fillColor;
                p.setPen(QPen(outlineColor, 1.0));
                p.setBrush(fillColor);
                p.drawEllipse(QPoint(px, py), markerRadius, markerRadius);
            }
        }
        drawRulerOverlay(p, scale, m_sagittalRuler, sagX, m_image.getSpacingY(), m_image.getSpacingZ()); });

    m_coronalView->setOverlayDraw([this, corY, minPixelSpacing, makeCellKey](QPainter &p, float scale)
                                  {
        if (m_enableCoronalSeeds)
        {
            std::unordered_set<std::uint64_t> occupiedCells;
            if (minPixelSpacing > 1)
                occupiedCells.reserve(m_seeds.size());
            const int markerRadius = (minPixelSpacing >= 5) ? 1 : 2;
            for (const auto &s : m_seeds)
            {
                if (s.y != corY)
                    continue;
                const int px = static_cast<int>(std::lround(s.x * scale));
                const int py = static_cast<int>(std::lround(s.z * scale));
                if (minPixelSpacing > 1)
                {
                    if (!occupiedCells.insert(makeCellKey(px, py)).second)
                        continue;
                }
                int lbl = std::max(0, std::min(255, s.label));
                const QColor fillColor = colorForLabel(lbl);
                const QColor outlineColor = s.fromFile ? ((s.internal != 0) ? QColor(Qt::white) : QColor(Qt::black)) : fillColor;
                p.setPen(QPen(outlineColor, 1.0));
                p.setBrush(fillColor);
                p.drawEllipse(QPoint(px, py), markerRadius, markerRadius);
            }
        }
        drawRulerOverlay(p, scale, m_coronalRuler, corY, m_image.getSpacingX(), m_image.getSpacingZ()); });
}

void ManualSeedSelector::update3DMaskView()
{
    if (!m_mask3DView)
        return;
    m_mask3DView->setVoxelSpacing(m_maskSpacingX, m_maskSpacingY, m_maskSpacingZ);
    const unsigned int sx = m_image.getSizeX();
    const unsigned int sy = m_image.getSizeY();
    const unsigned int sz = m_image.getSizeZ();

    const bool maskDimsValid = (m_maskDimX > 0 && m_maskDimY > 0 && m_maskDimZ > 0);
    const size_t expectedMaskTotal = size_t(m_maskDimX) * size_t(m_maskDimY) * size_t(m_maskDimZ);
    const bool maskBufferValid = (!m_maskData.empty() && maskDimsValid && m_maskData.size() == expectedMaskTotal);
    const bool hasImageVolume = (sx > 0 && sy > 0 && sz > 0);

    if (!maskBufferValid)
    {
        m_mask3DView->clearMask();
    }
    else if (!hasImageVolume)
    {
        // No reference CT loaded: render the mask volume directly in 3D.
        m_mask3DView->setMaskData(m_maskData, m_maskDimX, m_maskDimY, m_maskDimZ,
                                  m_maskSpacingX, m_maskSpacingY, m_maskSpacingZ);
    }
    else
    {
        const bool maskXYMatchImage = (m_maskDimX == sx && m_maskDimY == sy);
        if (!maskXYMatchImage)
        {
            m_mask3DView->clearMask();
        }
        else if (m_maskDimZ == sz)
        {
            m_mask3DView->setMaskData(m_maskData, sx, sy, sz, m_image.getSpacingX(), m_image.getSpacingY(), m_image.getSpacingZ());
        }
        else
        {
            std::vector<int> remappedMask(size_t(sx) * size_t(sy) * size_t(sz), 0);
            const size_t planeStride = size_t(sx) * size_t(sy);
            const size_t sourcePlaneStride = size_t(m_maskDimX) * size_t(m_maskDimY);
            for (unsigned int z = 0; z < sz; ++z)
            {
                const unsigned int mappedZ = mapDepthIndex(z, sz, m_maskDimZ);
                const size_t srcOffset = size_t(mappedZ) * sourcePlaneStride;
                const size_t dstOffset = size_t(z) * planeStride;
                std::copy_n(m_maskData.begin() + static_cast<std::ptrdiff_t>(srcOffset),
                            static_cast<std::ptrdiff_t>(planeStride),
                            remappedMask.begin() + static_cast<std::ptrdiff_t>(dstOffset));
            }
            m_mask3DView->setMaskData(remappedMask, sx, sy, sz, m_image.getSpacingX(), m_image.getSpacingY(), m_image.getSpacingZ());
        }
    }

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
    m_maskDimX = m_image.getSizeX();
    m_maskDimY = m_image.getSizeY();
    m_maskDimZ = m_image.getSizeZ();
    m_maskSpacingX = m_image.getSpacingX();
    m_maskSpacingY = m_image.getSpacingY();
    m_maskSpacingZ = m_image.getSpacingZ();
    m_mask3DDirty = true;
}

void ManualSeedSelector::filterActiveMaskByThreshold()
{
    if (!hasImage())
    {
        QMessageBox::information(this, "Mask Threshold", "Load an image before filtering a mask by threshold.");
        return;
    }

    QString selectedMaskPath;
    if (m_maskList && m_maskList->currentItem())
        selectedMaskPath = QDir::cleanPath(QFileInfo(m_maskList->currentItem()->data(kPathRole).toString()).absoluteFilePath());

    QString activeMaskPath = QDir::cleanPath(QFileInfo(QString::fromStdString(m_loadedMaskPath)).absoluteFilePath());
    if (!selectedMaskPath.isEmpty() && selectedMaskPath != activeMaskPath)
    {
        if (!loadMaskFromFile(selectedMaskPath.toStdString()))
            return;
        activeMaskPath = selectedMaskPath;
        updateViews();
    }

    if (m_maskData.empty() || m_maskDimX == 0 || m_maskDimY == 0 || m_maskDimZ == 0)
    {
        QMessageBox::information(this, "Mask Threshold", "Load a mask before applying threshold filtering.");
        return;
    }

    if (m_maskDimX != m_image.getSizeX() || m_maskDimY != m_image.getSizeY())
    {
        QMessageBox::warning(this, "Mask Threshold", "Threshold filtering requires matching X/Y dimensions between image and mask.");
        return;
    }

    bool ok = false;
    const double threshold = QInputDialog::getDouble(
        this,
        "Mask Threshold",
        "Remove mask voxels where image intensity is >= threshold (HU):",
        -200.0,
        -4096.0,
        4096.0,
        1,
        &ok);
    if (!ok)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);

    const unsigned int imageSX = m_image.getSizeX();
    const unsigned int imageSY = m_image.getSizeY();
    const unsigned int imageSZ = m_image.getSizeZ();
    const size_t maskPlaneStride = size_t(m_maskDimX) * size_t(m_maskDimY);

    size_t removedCount = 0;
    for (unsigned int z = 0; z < imageSZ; ++z)
    {
        const unsigned int mappedZ = mapDepthIndex(z, imageSZ, m_maskDimZ);
        const size_t maskZOffset = size_t(mappedZ) * maskPlaneStride;
        for (unsigned int y = 0; y < imageSY; ++y)
        {
            const size_t rowOffset = size_t(y) * m_maskDimX;
            for (unsigned int x = 0; x < imageSX; ++x)
            {
                const size_t maskIdx = size_t(x) + rowOffset + maskZOffset;
                if (m_maskData[maskIdx] == 0)
                    continue;
                if (m_image.getVoxelValue(x, y, z) >= static_cast<float>(threshold))
                {
                    m_maskData[maskIdx] = 0;
                    ++removedCount;
                }
            }
        }
    }

    QApplication::restoreOverrideCursor();

    m_mask3DDirty = true;
    updateViews();

    const QString maskName = !activeMaskPath.isEmpty()
                                 ? QFileInfo(activeMaskPath).fileName()
                                 : QString("current mask");
    if (m_statusLabel)
    {
        m_statusLabel->setText(
            QString("Mask threshold applied to %1: removed %2 voxel(s) with image intensity >= %3 HU.")
                .arg(maskName)
                .arg(removedCount)
                .arg(threshold, 0, 'f', 1));
    }
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
        {
            m_maskData.assign(size_t(sx) * size_t(sy) * size_t(sz), 0);
            m_maskDimX = sx;
            m_maskDimY = sy;
            m_maskDimZ = sz;
        }

        const bool maskDimsKnown = (m_maskDimX > 0 && m_maskDimY > 0 && m_maskDimZ > 0);
        const size_t expectedMaskTotal = maskDimsKnown ? (size_t(m_maskDimX) * size_t(m_maskDimY) * size_t(m_maskDimZ)) : 0;
        const bool canSampleMask = (!m_maskData.empty() &&
                                    maskDimsKnown &&
                                    m_maskData.size() == expectedMaskTotal &&
                                    m_maskDimX == sx &&
                                    m_maskDimY == sy);

        itk::ImageRegionIterator<ImageType> it(out, region);
        size_t idx = 0;
        const size_t imagePlaneStride = size_t(sx) * size_t(sy);
        const size_t maskPlaneStride = size_t(m_maskDimX) * size_t(m_maskDimY);
        for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++idx)
        {
            int v = 0;
            if (canSampleMask)
            {
                const unsigned int x = static_cast<unsigned int>(idx % sx);
                const unsigned int y = static_cast<unsigned int>((idx / sx) % sy);
                const unsigned int z = static_cast<unsigned int>(idx / imagePlaneStride);
                const unsigned int mappedZ = mapDepthIndex(z, sz, m_maskDimZ);
                const size_t maskIdx = size_t(x) + size_t(y) * m_maskDimX + size_t(mappedZ) * maskPlaneStride;
                v = m_maskData[maskIdx];
            }
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
        const QString absoluteMaskPath = QDir::cleanPath(QFileInfo(QString::fromStdString(path)).absoluteFilePath());
        const bool hasImage = (m_image.getSizeX() > 0 && m_image.getSizeY() > 0 && m_image.getSizeZ() > 0);

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

        if (hasImage &&
            (sx != m_image.getSizeX() || sy != m_image.getSizeY()))
        {
            m_maskData.clear();
            m_maskDimX = 0;
            m_maskDimY = 0;
            m_maskDimZ = 0;
            m_maskSpacingX = m_image.getSpacingX();
            m_maskSpacingY = m_image.getSpacingY();
            m_maskSpacingZ = m_image.getSpacingZ();
            m_mask3DDirty = true;

            const QString msg = QString("Mask dimensions (%1 x %2 x %3) do not match image dimensions (%4 x %5 x %6). "
                                        "Overlay requires matching X/Y dimensions.")
                                    .arg(sx)
                                    .arg(sy)
                                    .arg(sz)
                                    .arg(m_image.getSizeX())
                                    .arg(m_image.getSizeY())
                                    .arg(m_image.getSizeZ());
            QMessageBox::warning(this, "Load Mask", msg);
            if (m_statusLabel)
                m_statusLabel->setText("Mask not overlaid: X/Y dimensions do not match current image.");
            m_loadedMaskPath.clear();
            return false;
        }

        const auto spacing = img->GetSpacing();
        if (hasImage)
        {
            m_maskSpacingX = m_image.getSpacingX();
            m_maskSpacingY = m_image.getSpacingY();
            m_maskSpacingZ = m_image.getSpacingZ();
        }
        else
        {
            m_maskSpacingX = std::abs(static_cast<double>(spacing[0]));
            m_maskSpacingY = std::abs(static_cast<double>(spacing[1]));
            m_maskSpacingZ = std::abs(static_cast<double>(spacing[2]));
            if (!std::isfinite(m_maskSpacingX) || m_maskSpacingX <= 0.0)
                m_maskSpacingX = 1.0;
            if (!std::isfinite(m_maskSpacingY) || m_maskSpacingY <= 0.0)
                m_maskSpacingY = 1.0;
            if (!std::isfinite(m_maskSpacingZ) || m_maskSpacingZ <= 0.0)
                m_maskSpacingZ = 1.0;
        }
        size_t tot = size_t(sx) * size_t(sy) * size_t(sz);
        m_maskData.clear();
        m_maskData.resize(tot);
        m_maskDimX = sx;
        m_maskDimY = sy;
        m_maskDimZ = sz;
        itk::ImageRegionConstIterator<ImageType> it(img, region);
        size_t idx = 0;
        for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++idx)
        {
            m_maskData[idx] = static_cast<int>(it.Get());
        }

        if (hasImage && sz != m_image.getSizeZ() && m_statusLabel)
        {
            m_statusLabel->setText(QString("Loaded mask with depth mismatch (%1 vs %2): overlay adapted to image height.")
                                       .arg(sz)
                                       .arg(m_image.getSizeZ()));
        }

        if (!hasImage && m_show3DCheck && !m_show3DCheck->isChecked())
        {
            // In mask-only mode, ensure render window actually shows the loaded mask.
            QSignalBlocker blocker(m_show3DCheck);
            m_show3DCheck->setChecked(true);
            m_enable3DView = true;
            if (m_mask3DView)
                m_mask3DView->setMaskVisible(true);
        }

        m_mask3DDirty = true;
        m_loadedMaskPath = absoluteMaskPath.toStdString();
        return true;
    }
    catch (const std::exception &e)
    {
        m_loadedMaskPath.clear();
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
    requestViewUpdate(false);
}

void ManualSeedSelector::paintSagittalMask(int x, int y)
{
    int sx = m_sagittalSlider->value();
    bool erase = (m_maskMode == 2);
    applyBrushToMask({sx, x, y}, {1, 2}, m_maskBrushRadius, m_labelSelector->value(), erase);
    requestViewUpdate(false);
}

void ManualSeedSelector::paintCoronalMask(int x, int y)
{
    int cy = m_coronalSlider->value();
    bool erase = (m_maskMode == 2);
    applyBrushToMask({x, cy, y}, {0, 2}, m_maskBrushRadius, m_labelSelector->value(), erase);
    requestViewUpdate(false);
}

void ManualSeedSelector::applyBrushToMask(const std::array<int, 3> &center, const std::pair<int, int> &axes, int radius, int labelValue, bool erase)
{
    const unsigned int imageSX = m_image.getSizeX();
    const unsigned int imageSY = m_image.getSizeY();
    const unsigned int imageSZ = m_image.getSizeZ();
    if (imageSX == 0)
        return;
    if (m_maskData.empty())
    {
        m_maskData.assign(size_t(imageSX) * size_t(imageSY) * size_t(imageSZ), 0);
        m_maskDimX = imageSX;
        m_maskDimY = imageSY;
        m_maskDimZ = imageSZ;
    }

    if (m_maskDimX != imageSX || m_maskDimY != imageSY || m_maskDimZ == 0)
        return;

    const unsigned int maskSX = m_maskDimX;
    const unsigned int maskSY = m_maskDimY;
    const unsigned int maskSZ = m_maskDimZ;

    std::array<int, 3> mappedCenter = center;
    mappedCenter[2] = static_cast<int>(mapDepthIndex(static_cast<unsigned int>(std::max(0, center[2])), imageSZ, maskSZ));

    int a0 = axes.first;
    int a1 = axes.second;
    int min0 = std::max(0, mappedCenter[a0] - radius);
    int max0 = std::min(int((a0 == 0 ? maskSX : (a0 == 1 ? maskSY : maskSZ))) - 1, mappedCenter[a0] + radius);
    int min1 = std::max(0, mappedCenter[a1] - radius);
    int max1 = std::min(int((a1 == 0 ? maskSX : (a1 == 1 ? maskSY : maskSZ))) - 1, mappedCenter[a1] + radius);

    for (int i = min0; i <= max0; ++i)
    {
        for (int j = min1; j <= max1; ++j)
        {
            int di = i - mappedCenter[a0];
            int dj = j - mappedCenter[a1];
            if (di * di + dj * dj <= radius * radius)
            {
                int xi = (a0 == 0) ? i : ((a1 == 0) ? j : mappedCenter[0]);
                int yi = (a0 == 1) ? i : ((a1 == 1) ? j : mappedCenter[1]);
                int zi = (a0 == 2) ? i : ((a1 == 2) ? j : mappedCenter[2]);
                if (xi < 0 || yi < 0 || zi < 0 || xi >= int(maskSX) || yi >= int(maskSY) || zi >= int(maskSZ))
                    continue;
                const size_t idx = size_t(xi) + size_t(yi) * maskSX + size_t(zi) * maskSX * maskSY;
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

    if (event->key() == Qt::Key_Escape && m_rulerEnabled)
    {
        clearRulerMeasurements();
        requestViewUpdate(true);
        if (m_statusLabel)
            m_statusLabel->setText("Ruler measurements cleared.");
        return;
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
    if (event->key() == Qt::Key_Escape && m_rulerEnabled)
    {
        clearRulerMeasurements();
        requestViewUpdate(true);
        if (m_statusLabel)
            m_statusLabel->setText("Ruler measurements cleared.");
        return true;
    }

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
    if (obj == (m_niftiList ? m_niftiList->viewport() : nullptr) && event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        if (me && me->button() == Qt::RightButton)
        {
            QListWidgetItem *item = m_niftiList ? m_niftiList->itemAt(me->pos()) : nullptr;
            if (!item)
                return true; // consume right-click and keep current viewer image

            QString path = item->data(Qt::UserRole).toString().trimmed();
            if (path.isEmpty() && m_niftiList)
            {
                const int row = m_niftiList->row(item);
                if (row >= 0 && row < static_cast<int>(m_images.size()))
                    path = QString::fromStdString(m_images[static_cast<size_t>(row)].imagePath);
            }
            path = QFileInfo(path).absoluteFilePath();
            if (path.isEmpty())
                return true;

            QMenu menu(this);
            QAction *copyPathAction = menu.addAction("Copy Path");
            QAction *revealPathAction = menu.addAction("Reveal File in Explorer");
            QAction *selectedAction = menu.exec(m_niftiList->viewport()->mapToGlobal(me->pos()));
            if (selectedAction == copyPathAction)
            {
                QApplication::clipboard()->setText(path);
                if (m_statusLabel)
                    m_statusLabel->setText(QString("Copied path: %1").arg(path));
            }
            else if (selectedAction == revealPathAction)
            {
                QString openedPath;
                QString errorMessage;
                if (revealPathInFileManager(path, &openedPath, &errorMessage))
                {
                    if (m_statusLabel)
                    {
                        const QString shownPath = openedPath.isEmpty() ? path : openedPath;
                        m_statusLabel->setText(QString("Opened in file explorer: %1").arg(shownPath));
                    }
                }
                else
                {
                    QMessageBox::warning(this,
                                         "Reveal File in Explorer",
                                         errorMessage.isEmpty()
                                             ? QString("Failed to open the file explorer for:\n%1").arg(path)
                                             : QString("%1\n\nPath:\n%2").arg(errorMessage, path));
                }
            }
            return true; // prevent selection change on right-click
        }
    }

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
    const QString activeMaskPath = QDir::cleanPath(QFileInfo(QString::fromStdString(m_loadedMaskPath)).absoluteFilePath());
    int activeMaskRow = -1;

    auto appendMaskItem = [this, &activeMaskPath, &activeMaskRow](const std::string &maskPath, const QColor &color, int sourceImageIndex, bool markAsGlobal)
    {
        const QString absolutePath = QFileInfo(QString::fromStdString(maskPath)).absoluteFilePath();
        const QString fileName = QFileInfo(absolutePath).fileName();
        const int rowNumber = m_maskList->count() + 1;
        const QString baseLabel = markAsGlobal ? QString("%1 [global]").arg(fileName) : fileName;
        const QString label = QString("%1. %2").arg(rowNumber).arg(baseLabel);
        QListWidgetItem *item = new QListWidgetItem(label);
        item->setForeground(QBrush(color));
        item->setData(kPathRole, absolutePath);
        item->setData(kMaskSourceImageRole, sourceImageIndex);
        m_maskList->addItem(item);
        const int row = m_maskList->count() - 1;
        if (!activeMaskPath.isEmpty() && activeMaskRow < 0 && QDir::cleanPath(absolutePath) == activeMaskPath)
            activeMaskRow = row;
    };

    if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
    {
        const ImageData &currentImage = m_images[m_currentImageIndex];
        const QColor color = currentImage.color;

        for (const std::string &maskPath : currentImage.maskPaths)
            appendMaskItem(maskPath, color, m_currentImageIndex, false);

        for (const auto &seedPath : currentImage.seedPaths)
        {
            const int rowNumber = m_seedList->count() + 1;
            const QString absoluteSeedPath = QFileInfo(QString::fromStdString(seedPath)).absoluteFilePath();
            const QString filename = QFileInfo(absoluteSeedPath).fileName();
            QListWidgetItem *item = new QListWidgetItem(QString("%1. %2").arg(rowNumber).arg(filename.isEmpty() ? absoluteSeedPath : filename));
            item->setForeground(QBrush(color));
            item->setData(Qt::UserRole, absoluteSeedPath);
            m_seedList->addItem(item);
        }
    }

    for (const std::string &maskPath : m_unassignedMaskPaths)
        appendMaskItem(maskPath, QColor(200, 200, 200), -1, m_currentImageIndex >= 0);

    if (activeMaskRow >= 0)
        m_maskList->setCurrentRow(activeMaskRow);

    if (m_maskData.empty() && hasImage())
    {
        QString autoLoadSummary;
        if (autoLoadAnatomyMasksForCurrentImage(&autoLoadSummary) && m_statusLabel && !autoLoadSummary.isEmpty())
            m_statusLabel->setText(autoLoadSummary);
    }

}
