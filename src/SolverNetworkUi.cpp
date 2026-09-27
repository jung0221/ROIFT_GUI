// The Solver Network section: a 1D haemodynamic network read from its YAML,
// listed segment by segment and placed on the image through the segment map
// `vessels.cli.analyze_vessels --solver-yaml` writes beside it.
#include "ManualSeedSelector.h"
#include "Mask3DView.h"
#include "Theme.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QString cleanPath(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
} // namespace

QVBoxLayout *ManualSeedSelector::buildSolverNetworkSection()
{
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLabel *hint = new QLabel("A 1D solver network (YAML). Its segment map, when beside it, "
                              "places each segment on the image: pick a row to go there, "
                              "right-click a vessel or Shift+click it in 3D to find its row.");
    hint->setWordWrap(true);
    Theme::applyHintStyle(hint);
    layout->addWidget(hint);

    QPushButton *load = new QPushButton("Load Network (YAML)...");
    load->setToolTip("Read <case>_artery_solver.yaml and, if present, <case>_artery_solver_segments.nii.gz");
    connect(load, &QPushButton::clicked, this, &ManualSeedSelector::loadSolverNetwork);
    layout->addWidget(load);

    m_solverSummary = new QLabel("No network loaded.");
    m_solverSummary->setWordWrap(true);
    Theme::applyHintStyle(m_solverSummary);
    layout->addWidget(m_solverSummary);

    m_solverTable = new QTableWidget(0, 4);
    // Units live in the tooltips: the sidebar is 250 px and a fourth wide header would clip.
    m_solverTable->setHorizontalHeaderLabels({"Segment", "L", "Ø", "BC"});
    m_solverTable->horizontalHeaderItem(1)->setToolTip("Length, mm");
    m_solverTable->horizontalHeaderItem(2)->setToolTip("Diameter (2 x R0), mm");
    m_solverTable->horizontalHeaderItem(3)->setToolTip("Outlet: carries a Windkessel boundary condition");
    m_solverTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_solverTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_solverTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_solverTable->verticalHeader()->setVisible(false);
    m_solverTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_solverTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_solverTable->setMinimumHeight(180);
    // Take the sidebar's width rather than set it: a table's width hint would push the column wider.
    m_solverTable->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_solverTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_solverTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int)
            {
                if (row >= 0 && row != m_selectedSolverSegment)
                    selectSolverSegment(row, true);
            });
    layout->addWidget(m_solverTable);

    m_solverDetails = new QLabel();
    m_solverDetails->setWordWrap(true);
    m_solverDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_solverDetails->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_solverDetails);

    m_solverAllNamesBox = new QCheckBox("Name every segment in 3D");
    m_solverAllNamesBox->setToolTip("Off: only the selected segment is named in the 3D view.");
    connect(m_solverAllNamesBox, &QCheckBox::toggled, this, [this](bool) { updateSolverAnnotations(); });
    layout->addWidget(m_solverAllNamesBox);
    return layout;
}

void ManualSeedSelector::loadSolverNetwork()
{
    const QString startDir = m_path.empty() ? QString()
                                            : QFileInfo(QString::fromStdString(m_path)).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Load Solver Network", startDir,
                                                      "Solver network (*.yaml *.yml);;All files (*)");
    if (path.isEmpty())
        return;
    QString error;
    if (!loadSolverNetworkFromPath(path, &error))
        QMessageBox::warning(this, "Solver Network", error);
}

bool ManualSeedSelector::loadSolverNetworkFromPath(const QString &yamlPath, QString *error)
{
    SolverNetwork network;
    if (!readSolverNetwork(yamlPath, network, error))
        return false;

    m_solverNetwork = network;
    m_selectedSolverSegment = -1;
    m_solverSegmentIndex.clear();
    m_solverSegmentMapPath.clear();
    m_locatedPoint.text.clear();

    const int outlets = static_cast<int>(std::count_if(network.segments.begin(), network.segments.end(),
                                                       [](const SolverSegment &s) { return s.outlet; }));
    QString mapNote;
    const QString mapPath = cleanPath(solverSegmentMapPath(yamlPath));
    if (!QFileInfo::exists(mapPath))
    {
        mapNote = "no segment map beside it, so the segments cannot be located";
    }
    else
    {
        MaskVolume volume;
        QString readError;
        if (!readMaskVolume(mapPath.toStdString(), numpyOptionsForMask(), volume, &readError))
        {
            mapNote = QString("segment map unreadable: %1").arg(readError);
        }
        else if (hasImage() && (volume.dimX != m_image.getSizeX() || volume.dimY != m_image.getSizeY() ||
                                volume.dimZ != m_image.getSizeZ()))
        {
            mapNote = QString("segment map is %1x%2x%3, the image %4x%5x%6: not placed")
                          .arg(volume.dimX).arg(volume.dimY).arg(volume.dimZ)
                          .arg(m_image.getSizeX()).arg(m_image.getSizeY()).arg(m_image.getSizeZ());
        }
        else
        {
            m_solverSegmentIndex.build(volume, static_cast<int>(network.segments.size()));
            m_solverSegmentMapPath = mapPath;
            m_solverImagePath = m_path;
            const std::vector<int> labels = volume.distinctLabels();
            const int placed = static_cast<int>(std::count_if(
                m_solverSegmentIndex.anchors().begin(), m_solverSegmentIndex.anchors().end(),
                [](const SegmentAnchor &a) { return a.valid; }));
            mapNote = QString("%1 of %2 segments placed by the segment map").arg(placed).arg(network.segments.size());
            if (!labels.empty() && labels.back() > static_cast<int>(network.segments.size()))
                mapNote += QString("; labels above %1 match no segment, so the map is not this YAML's")
                               .arg(network.segments.size());
        }
    }

    if (!m_solverSegmentMapPath.isEmpty())
    {
        if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
        {
            auto &maskPaths = m_images[static_cast<size_t>(m_currentImageIndex)].maskPaths;
            const std::string p = m_solverSegmentMapPath.toStdString();
            if (std::find(maskPaths.begin(), maskPaths.end(), p) == maskPaths.end())
                maskPaths.push_back(p);
            updateMaskSeedLists();
            const int row = static_cast<int>(std::find(maskPaths.begin(), maskPaths.end(), p) - maskPaths.begin());
            if (m_maskList && row >= 0 && row < m_maskList->count())
                m_maskList->setCurrentRow(row);
        }
        if (loadMaskFromFile(m_solverSegmentMapPath.toStdString()))
            setActiveMaskVisible();
    }

    // File names go in the tooltip: a wrapped label is never narrower than its longest word.
    if (m_solverSummary)
    {
        m_solverSummary->setText(QString("%1 segments, %2 outlets; %3.")
                                     .arg(network.segments.size())
                                     .arg(outlets)
                                     .arg(mapNote));
        m_solverSummary->setToolTip(QString("%1\n%2").arg(network.path,
                                                          m_solverSegmentMapPath.isEmpty() ? QStringLiteral("(no segment map)")
                                                                                           : m_solverSegmentMapPath));
    }
    appendSegmentationLog(QString("Solver network: %1 (%2 segments, %3 outlets); %4%5")
                              .arg(network.path)
                              .arg(network.segments.size())
                              .arg(outlets)
                              .arg(mapNote)
                              .arg(m_solverSegmentMapPath.isEmpty() ? QString() : ", " + m_solverSegmentMapPath));
    refreshSolverNetworkTable();
    if (m_solverDetails)
        m_solverDetails->clear();
    updateSolverAnnotations();
    update3DMaskView();
    updateViews();
    return true;
}

void ManualSeedSelector::refreshSolverNetworkTable()
{
    if (!m_solverTable)
        return;
    const QSignalBlocker block(m_solverTable);
    m_solverTable->setRowCount(static_cast<int>(m_solverNetwork.segments.size()));
    for (int i = 0; i < static_cast<int>(m_solverNetwork.segments.size()); ++i)
    {
        const SolverSegment &s = m_solverNetwork.segments[static_cast<size_t>(i)];
        auto cell = [](const QString &text, bool number)
        {
            auto *item = new QTableWidgetItem(text);
            if (number)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        QTableWidgetItem *name = cell(s.label, false);
        const bool placed = i < static_cast<int>(m_solverSegmentIndex.anchors().size()) &&
                            m_solverSegmentIndex.anchors()[static_cast<size_t>(i)].valid;
        if (!m_solverSegmentMapPath.isEmpty() && !placed)
            name->setToolTip("No voxel of the map carries this segment: nothing to go to.");
        m_solverTable->setItem(i, 0, name);
        m_solverTable->setItem(i, 1, cell(QString::number(s.lengthM * 1000.0, 'f', 1), true));
        m_solverTable->setItem(i, 2, cell(QString::number(s.radiusM * 2000.0, 'f', 1), true));
        m_solverTable->setItem(i, 3, cell(s.outlet ? QStringLiteral("●") : QString(), false));
    }
}

void ManualSeedSelector::selectSolverSegment(int index, bool jump)
{
    if (index < 0 || index >= static_cast<int>(m_solverNetwork.segments.size()))
        return;
    m_selectedSolverSegment = index;
    const SolverSegment &s = m_solverNetwork.segments[static_cast<size_t>(index)];
    if (m_solverTable)
    {
        const QSignalBlocker block(m_solverTable);
        m_solverTable->selectRow(index);
        m_solverTable->scrollToItem(m_solverTable->item(index, 0));
    }

    QString details = m_solverNetwork.describe(index);
    const auto &anchors = m_solverSegmentIndex.anchors();
    const bool placed = index < static_cast<int>(anchors.size()) && anchors[static_cast<size_t>(index)].valid;
    if (placed)
    {
        const SegmentAnchor &a = anchors[static_cast<size_t>(index)];
        details += QString("\nmap: %1 voxels, shown at (%2, %3, %4)").arg(a.voxels).arg(a.x).arg(a.y).arg(a.z);
    }
    else if (!m_solverSegmentMapPath.isEmpty())
    {
        details += "\nmap: no voxels (a connector the export inserted, or cut)";
    }
    if (m_solverDetails)
        m_solverDetails->setText(details);

    if (jump && placed)
    {
        const SegmentAnchor &a = anchors[static_cast<size_t>(index)];
        jumpToVoxel(a.x, a.y, a.z);
    }
    if (m_locatedPoint.valid)
        m_locatedPoint.text = s.label;
    updateSolverAnnotations();
    requestViewUpdate(true);
}

int ManualSeedSelector::solverSegmentAtVoxel(int x, int y, int z, int radius) const
{
    if (m_solverSegmentMapPath.isEmpty() || m_solverSegmentIndex.empty() || m_path != m_solverImagePath)
        return 0;
    if (m_solverSegmentIndex.dimX() != m_image.getSizeX() || m_solverSegmentIndex.dimY() != m_image.getSizeY() ||
        m_solverSegmentIndex.dimZ() != m_image.getSizeZ())
        return 0;
    const int label = m_solverSegmentIndex.segmentNear(x, y, z, radius);
    return label <= static_cast<int>(m_solverNetwork.segments.size()) ? label : 0;
}

QString ManualSeedSelector::solverSegmentName(const QString &path, int label) const
{
    if (m_solverSegmentMapPath.isEmpty() || label <= 0 ||
        label > static_cast<int>(m_solverNetwork.segments.size()) || cleanPath(path) != m_solverSegmentMapPath)
        return QString();
    return m_solverNetwork.segments[static_cast<size_t>(label - 1)].label;
}

void ManualSeedSelector::updateSolverAnnotations()
{
    if (!m_mask3DView)
        return;
    std::vector<Annotation3D> annotations;
    const auto &anchors = m_solverSegmentIndex.anchors();
    const MaskLayer *layer = findMaskLayer(m_solverSegmentMapPath);
    const bool all = m_solverAllNamesBox && m_solverAllNamesBox->isChecked();
    for (int i = 0; i < static_cast<int>(anchors.size()) && i < static_cast<int>(m_solverNetwork.segments.size()); ++i)
    {
        const SegmentAnchor &a = anchors[static_cast<size_t>(i)];
        const bool selected = (i == m_selectedSolverSegment);
        if (!a.valid || (!all && !selected))
            continue;
        Annotation3D note;
        note.x = a.x;
        note.y = a.y;
        note.z = a.z;
        note.text = m_solverNetwork.segments[static_cast<size_t>(i)].label;
        note.color = selected ? QColor(0, 229, 255) : (layer ? layer->colorForLabelValue(i + 1) : QColor(Qt::white));
        note.emphasised = selected;
        annotations.push_back(note);
    }
    m_mask3DView->setAnnotations(annotations);
}
