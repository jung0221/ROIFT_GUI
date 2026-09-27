// The Solver Network section: a 1D haemodynamic network read from its YAML,
// shown as a topology tree, and placed on the image through what
// `vessels.cli.analyze_vessels --solver-yaml` writes beside it: the geometry
// (node positions, centrelines) and the segment maps (territory, lumen).
//
// Nothing here re-contours the 3D surface. Selection moves the slices and swaps
// a highlight actor; hover repaints the slice overlays and, coalesced, the 3D
// highlight. The whole graph is two 3D actors built once per load.
#include "ColorUtils.h"
#include "ManualSeedSelector.h"
#include "Mask3DView.h"
#include "OrthogonalView.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
constexpr int kSegmentRole = Qt::UserRole + 1;
// How far off the current slice the graph is still drawn: a vessel crossing the
// slice obliquely would otherwise flicker in and out a voxel at a time.
constexpr double kSlabMm = 3.0;

const QColor kInletColor(80, 220, 120);
const QColor kJunctionColor(235, 235, 235);
const QColor kOutletColor(255, 155, 74);
const QColor kSelectColor(0, 229, 255);

QString cleanPath(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QColor nodeColor(SolverNodeKind kind)
{
    switch (kind)
    {
    case SolverNodeKind::Inlet:
        return kInletColor;
    case SolverNodeKind::Outlet:
        return kOutletColor;
    default:
        return kJunctionColor;
    }
}

QString mmText(double metres, int decimals = 1)
{
    return QString::number(metres * 1000.0, 'f', decimals);
}

QString link(const QString &target, const QString &text)
{
    return QString("<a href=\"%1\">%2</a>").arg(target, text.toHtmlEscaped());
}

// In-plane (u, v) and depth of a voxel for one plane; matches drawLocatedPointOverlay.
struct PlaneCoords
{
    double u, v, depth;
};
} // namespace

// ---------------------------------------------------------------- section

QVBoxLayout *ManualSeedSelector::buildSolverNetworkSection()
{
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLabel *hint = new QLabel("A 1D solver network (YAML) and, beside it, where it lies. "
                              "Pick a segment or node to go there; hover the image to identify one.");
    hint->setWordWrap(true);
    Theme::applyHintStyle(hint);
    layout->addWidget(hint);

    QPushButton *load = new QPushButton("Load Network (YAML)...");
    load->setToolTip("Read <stem>.yaml and, beside it, <stem>_geometry.json, _segments.nii.gz and _lumen.nii.gz");
    connect(load, &QPushButton::clicked, this, &ManualSeedSelector::loadSolverNetwork);
    layout->addWidget(load);

    m_solverSummary = new QLabel("No network loaded.");
    m_solverSummary->setWordWrap(true);
    Theme::applyHintStyle(m_solverSummary);
    layout->addWidget(m_solverSummary);

    QGridLayout *options = new QGridLayout();
    options->setContentsMargins(0, 0, 0, 0);
    options->addWidget(new QLabel("Colour:"), 0, 0);
    m_solverMapCombo = new QComboBox();
    m_solverMapCombo->addItem("Territory (whole vessel)", "territory");
    m_solverMapCombo->addItem("Modelled lumen", "lumen");
    m_solverMapCombo->setToolTip("Territory: every vessel voxel, a cut branch in the colour of the segment it "
                                 "hangs from. Lumen: only the vessels the network models.");
    connect(m_solverMapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applySolverMapChoice(); });
    options->addWidget(m_solverMapCombo, 0, 1);
    m_solverGraph2DBox = new QCheckBox("Graph on slices");
    m_solverGraph2DBox->setChecked(true);
    m_solverGraph2DBox->setToolTip("Centrelines and nodes within 3 mm of each slice.");
    connect(m_solverGraph2DBox, &QCheckBox::toggled, this, [this](bool) { requestViewUpdate(true); });
    m_solverGraph3DBox = new QCheckBox("Graph in 3D");
    m_solverGraph3DBox->setChecked(true);
    connect(m_solverGraph3DBox, &QCheckBox::toggled, this, [this](bool on)
            {
        if (m_mask3DView)
            m_mask3DView->setNetworkVisible(on); });
    options->addWidget(m_solverGraph2DBox, 1, 0, 1, 2);
    options->addWidget(m_solverGraph3DBox, 2, 0, 1, 2);
    layout->addLayout(options);

    m_solverSearch = new QLineEdit();
    m_solverSearch->setPlaceholderText("Find: LB_03, RPA, n27...");
    m_solverSearch->setClearButtonEnabled(true);
    m_solverSearch->setToolTip("Filters the tree by segment name; n<id> or a bare number and Enter selects a node.");
    connect(m_solverSearch, &QLineEdit::textChanged, this, &ManualSeedSelector::filterSolverTree);
    connect(m_solverSearch, &QLineEdit::returnPressed, this, [this]()
            {
        const QString text = m_solverSearch->text().trimmed();
        if (text.isEmpty())
            return;
        const QRegularExpression nodeRe("^(?:n|node\\s*)?(\\d+)$", QRegularExpression::CaseInsensitiveOption);
        const auto m = nodeRe.match(text);
        if (m.hasMatch())
        {
            const int id = m.captured(1).toInt();
            if (solverNodeKind(m_solverNetwork, id) != SolverNodeKind::Unknown)
            {
                selectSolverNode(id, true);
                return;
            }
        }
        for (QTreeWidgetItem *item : m_solverTreeItems)
            if (item && !item->isHidden() && item->text(0).contains(text, Qt::CaseInsensitive))
            {
                selectSolverSegment(item->data(0, kSegmentRole).toInt(), true);
                return;
            } });
    layout->addWidget(m_solverSearch);

    m_solverTree = new QTreeWidget();
    m_solverTree->setColumnCount(4);
    m_solverTree->setHeaderLabels({"Segment", "L", "Ø", "BC"});
    m_solverTree->headerItem()->setToolTip(1, "Length, mm");
    m_solverTree->headerItem()->setToolTip(2, "Diameter (2 x R0), mm");
    m_solverTree->headerItem()->setToolTip(3, "Outlet: carries a Windkessel boundary condition");
    m_solverTree->setIndentation(12);
    m_solverTree->setUniformRowHeights(true);
    m_solverTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_solverTree->header()->setStretchLastSection(false);
    m_solverTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_solverTree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_solverTree->setMinimumHeight(220);
    // Take the sidebar's width rather than set it: a tree's width hint would push the column wider.
    m_solverTree->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_solverTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_solverTree->setToolTip("A branch's wider daughter continues on the same level; narrower ones nest under it.");
    connect(m_solverTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *)
            {
                if (!item)
                    return;
                const int index = item->data(0, kSegmentRole).toInt();
                if (index != m_selectedSolverSegment || m_selectedSolverNode >= 0)
                    selectSolverSegment(index, true);
            });
    layout->addWidget(m_solverTree);

    m_solverDetails = new QLabel();
    m_solverDetails->setWordWrap(true);
    m_solverDetails->setTextFormat(Qt::RichText);
    m_solverDetails->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse |
                                             Qt::LinksAccessibleByKeyboard);
    m_solverDetails->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    connect(m_solverDetails, &QLabel::linkActivated, this, &ManualSeedSelector::onSolverLinkActivated);
    layout->addWidget(m_solverDetails);

    m_solver3DTimer = new QTimer(this);
    m_solver3DTimer->setSingleShot(true);
    m_solver3DTimer->setInterval(60); // coalesces hover changes into one 3D render
    connect(m_solver3DTimer, &QTimer::timeout, this, &ManualSeedSelector::updateSolverHighlight3D);
    return layout;
}

// ---------------------------------------------------------------- loading

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
    m_solverGeometry = SolverGeometry{};
    m_solverGeometryPlaced = false;
    m_selectedSolverSegment = -1;
    m_selectedSolverNode = -1;
    m_hoverSolverSegment = 0;
    m_hoverSolverNode = -1;
    m_hoverSolverPlane = -1;
    m_solverSegmentIndex.clear();
    m_solverLumenIndex.clear();
    m_solverSegmentMapPath.clear();
    m_solverLumenMapPath.clear();
    m_solverImagePath.clear();
    m_locatedPoint.text.clear();

    const unsigned int imageDims[3] = {m_image.getSizeX(), m_image.getSizeY(), m_image.getSizeZ()};
    const bool haveImage = hasImage();
    const int count = static_cast<int>(network.segments.size());
    QStringList notes;

    // Geometry first: it says which maps belong to this YAML, and refuses a stale set.
    QString segmentMap = cleanPath(solverSegmentMapPath(yamlPath));
    QString lumenMap = cleanPath(solverSiblingPath(yamlPath, "_lumen.nii.gz"));
    const QString geometryPath = cleanPath(solverSiblingPath(yamlPath, "_geometry.json"));
    bool refused = false;
    if (QFileInfo::exists(geometryPath))
    {
        QString geometryError;
        if (!readSolverGeometry(geometryPath, m_solverGeometry, &geometryError))
        {
            notes << QString("geometry unreadable (%1)").arg(geometryError);
        }
        else
        {
            const QStringList problems =
                solverGeometryProblems(network, m_solverGeometry, haveImage ? imageDims : nullptr);
            if (!problems.isEmpty())
            {
                refused = true;
                notes << QString("not placed: the geometry %1").arg(problems.join("; "));
                m_solverGeometry = SolverGeometry{};
            }
            else
            {
                m_solverGeometryPlaced = haveImage;
                const QDir dir = QFileInfo(yamlPath).absoluteDir();
                if (!m_solverGeometry.files.value("segment_map").isEmpty())
                    segmentMap = cleanPath(dir.filePath(m_solverGeometry.files.value("segment_map")));
                if (!m_solverGeometry.files.value("lumen_map").isEmpty())
                    lumenMap = cleanPath(dir.filePath(m_solverGeometry.files.value("lumen_map")));
            }
        }
    }
    else
    {
        notes << "no geometry beside it, so no nodes or centrelines";
    }

    auto readMap = [&](const QString &path, SegmentVoxelIndex &index, const QString &what) -> bool
    {
        if (refused || !haveImage || !QFileInfo::exists(path))
            return false;
        MaskVolume volume;
        QString readError;
        if (!readMaskVolume(path.toStdString(), numpyOptionsForMask(), volume, &readError))
        {
            notes << QString("%1 unreadable (%2)").arg(what, readError);
            return false;
        }
        if (volume.dimX != imageDims[0] || volume.dimY != imageDims[1] || volume.dimZ != imageDims[2])
        {
            notes << QString("%1 is %2x%3x%4, the image %5x%6x%7: not placed")
                         .arg(what).arg(volume.dimX).arg(volume.dimY).arg(volume.dimZ)
                         .arg(imageDims[0]).arg(imageDims[1]).arg(imageDims[2]);
            return false;
        }
        const std::vector<int> labels = volume.distinctLabels();
        if (!labels.empty() && labels.back() > count)
        {
            notes << QString("%1 has labels above %2, so it is not this YAML's: not placed").arg(what).arg(count);
            return false;
        }
        index.build(volume, count);
        return true;
    };
    if (readMap(segmentMap, m_solverSegmentIndex, "segment map"))
        m_solverSegmentMapPath = segmentMap;
    if (readMap(lumenMap, m_solverLumenIndex, "lumen map"))
        m_solverLumenMapPath = lumenMap;
    if (!m_solverSegmentMapPath.isEmpty() || m_solverGeometryPlaced)
        m_solverImagePath = m_path;

    // Both maps join the image's mask list; the combo decides which one is drawn.
    if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
    {
        auto &maskPaths = m_images[static_cast<size_t>(m_currentImageIndex)].maskPaths;
        for (const QString &path : {m_solverSegmentMapPath, m_solverLumenMapPath})
        {
            const std::string p = path.toStdString();
            if (!p.empty() && std::find(maskPaths.begin(), maskPaths.end(), p) == maskPaths.end())
                maskPaths.push_back(p);
        }
        updateMaskSeedLists();
    }
    if (m_solverMapCombo)
    {
        const QSignalBlocker block(m_solverMapCombo);
        m_solverMapCombo->setCurrentIndex(0);
        m_solverMapCombo->setEnabled(!m_solverLumenMapPath.isEmpty());
    }
    applySolverMapChoice();

    const int outlets = static_cast<int>(std::count_if(network.segments.begin(), network.segments.end(),
                                                       [](const SolverSegment &s) { return s.outlet; }));
    QString placed;
    if (m_solverGeometryPlaced)
        placed = QString("%1 nodes placed").arg(m_solverGeometry.nodes.size());
    else if (!m_solverSegmentMapPath.isEmpty())
        placed = "segments placed by their map";
    else
        placed = haveImage ? "nothing placed" : "open the image first to place it";
    if (m_solverSummary)
    {
        m_solverSummary->setText(QString("%1 segments, %2 outlets; %3.%4")
                                     .arg(count)
                                     .arg(outlets)
                                     .arg(placed)
                                     .arg(notes.isEmpty() ? QString() : "\n" + notes.join("\n")));
        // File names go in the tooltip: a wrapped label is never narrower than its longest word.
        QStringList files = {network.path};
        for (const QString &f : {m_solverGeometryPlaced ? geometryPath : QString(), m_solverSegmentMapPath,
                                 m_solverLumenMapPath})
            if (!f.isEmpty())
                files << f;
        m_solverSummary->setToolTip(files.join("\n"));
    }
    appendSegmentationLog(QString("Solver network: %1 (%2 segments, %3 outlets); %4%5")
                              .arg(network.path)
                              .arg(count)
                              .arg(outlets)
                              .arg(placed)
                              .arg(notes.isEmpty() ? QString() : "; " + notes.join("; ")));

    rebuildSolverTree();
    if (m_solverSearch)
        m_solverSearch->clear();
    refreshSolverDetails();
    rebuildSolverGraph3D();
    updateViews();
    return true;
}

bool ManualSeedSelector::solverNetworkPlaced() const
{
    return !m_solverImagePath.empty() && m_path == m_solverImagePath;
}

void ManualSeedSelector::applySolverMapChoice()
{
    const bool lumen = m_solverMapCombo && m_solverMapCombo->currentData().toString() == "lumen" &&
                       !m_solverLumenMapPath.isEmpty();
    const QString show = lumen ? m_solverLumenMapPath : m_solverSegmentMapPath;
    const QString hide = lumen ? m_solverSegmentMapPath : m_solverLumenMapPath;
    if (show.isEmpty())
        return;
    if (m_maskList && m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
    {
        const auto &maskPaths = m_images[static_cast<size_t>(m_currentImageIndex)].maskPaths;
        const auto it = std::find(maskPaths.begin(), maskPaths.end(), show.toStdString());
        const int row = static_cast<int>(it - maskPaths.begin());
        if (it != maskPaths.end() && row < m_maskList->count())
            m_maskList->setCurrentRow(row);
    }
    if (loadMaskFromFile(show.toStdString()))
        setActiveMaskVisible();
    if (!hide.isEmpty() && maskVisibilityForPath(hide) == MaskVisibility::Visible)
        toggleMaskVisible(hide);
    rebuildSolverGraph3D(); // line colours follow the drawn map's palette
    updateViews();
}

// ---------------------------------------------------------------- lookups

int ManualSeedSelector::solverSegmentAtVoxel(int x, int y, int z, int radius) const
{
    if (!solverNetworkPlaced() || m_solverSegmentIndex.empty())
        return 0;
    const int label = m_solverSegmentIndex.segmentNear(x, y, z, radius);
    return label <= static_cast<int>(m_solverNetwork.segments.size()) ? label : 0;
}

int ManualSeedSelector::solverLumenSegmentAtVoxel(int x, int y, int z) const
{
    if (!solverNetworkPlaced() || m_solverLumenIndex.empty())
        return 0;
    const int label = m_solverLumenIndex.segmentAt(x, y, z);
    return label <= static_cast<int>(m_solverNetwork.segments.size()) ? label : 0;
}

int ManualSeedSelector::solverNodeNear(int x, int y, int z, double slackMm, bool fromSurface) const
{
    if (!solverNetworkPlaced() || !m_solverGeometryPlaced)
        return -1;
    const double sx = m_image.getSpacingX(), sy = m_image.getSpacingY(), sz = m_image.getSpacingZ();
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (const auto &entry : m_solverGeometry.nodes)
    {
        const SolverGeometry::Point &p = entry.second;
        const double d = std::sqrt(std::pow((p.x - x) * sx, 2) + std::pow((p.y - y) * sy, 2) +
                                   std::pow((p.z - z) * sz, 2));
        double reach = 0.0;
        for (int i : fromSurface ? m_solverNetwork.segmentsInto(entry.first) : std::vector<int>{})
            reach = std::max(reach, m_solverNetwork.segments[static_cast<size_t>(i)].radiusM * 1000.0);
        for (int i : fromSurface ? m_solverNetwork.segmentsOutOf(entry.first) : std::vector<int>{})
            reach = std::max(reach, m_solverNetwork.segments[static_cast<size_t>(i)].radiusM * 1000.0);
        if (d <= reach + slackMm && d < bestDistance)
        {
            bestDistance = d;
            best = entry.first;
        }
    }
    return best;
}

QString ManualSeedSelector::solverSegmentName(const QString &path, int label) const
{
    if (label <= 0 || label > static_cast<int>(m_solverNetwork.segments.size()))
        return QString();
    const QString clean = cleanPath(path);
    if (clean.isEmpty() || (clean != m_solverSegmentMapPath && clean != m_solverLumenMapPath))
        return QString();
    return m_solverNetwork.segments[static_cast<size_t>(label - 1)].label;
}

QColor ManualSeedSelector::solverSegmentColor(int label) const
{
    const bool lumen = m_solverMapCombo && m_solverMapCombo->currentData().toString() == "lumen";
    const MaskLayer *layer = findMaskLayer(lumen ? m_solverLumenMapPath : m_solverSegmentMapPath);
    return layer ? layer->colorForLabelValue(label) : colorForLabel(label);
}

std::vector<std::array<double, 3>> ManualSeedSelector::solverCenterlineVoxels(int index) const
{
    std::vector<std::array<double, 3>> out;
    if (!m_solverGeometryPlaced || index < 0 || index >= static_cast<int>(m_solverGeometry.segments.size()))
        return out;
    for (const SolverGeometry::Point &p : m_solverGeometry.segments[static_cast<size_t>(index)].centerline)
        out.push_back({p.x, p.y, p.z});
    return out;
}

bool ManualSeedSelector::solverSegmentFocus(int index, int &x, int &y, int &z) const
{
    if (index < 0 || index >= static_cast<int>(m_solverNetwork.segments.size()) || !solverNetworkPlaced())
        return false;
    if (m_solverGeometryPlaced)
    {
        // Half-way along the centreline: on the vessel even when the segment bends.
        const auto &line = m_solverGeometry.segments[static_cast<size_t>(index)].centerline;
        if (!line.empty())
        {
            const double sx = m_image.getSpacingX(), sy = m_image.getSpacingY(), sz = m_image.getSpacingZ();
            std::vector<double> arc(line.size(), 0.0);
            for (size_t i = 1; i < line.size(); ++i)
                arc[i] = arc[i - 1] + std::sqrt(std::pow((line[i].x - line[i - 1].x) * sx, 2) +
                                                std::pow((line[i].y - line[i - 1].y) * sy, 2) +
                                                std::pow((line[i].z - line[i - 1].z) * sz, 2));
            const size_t mid = static_cast<size_t>(std::lower_bound(arc.begin(), arc.end(), arc.back() / 2.0) - arc.begin());
            const SolverGeometry::Point &p = line[std::min(mid, line.size() - 1)];
            x = static_cast<int>(std::lround(p.x));
            y = static_cast<int>(std::lround(p.y));
            z = static_cast<int>(std::lround(p.z));
            return true;
        }
    }
    const auto &anchors = m_solverSegmentIndex.anchors();
    if (index < static_cast<int>(anchors.size()) && anchors[static_cast<size_t>(index)].valid)
    {
        const SegmentAnchor &a = anchors[static_cast<size_t>(index)];
        x = a.x;
        y = a.y;
        z = a.z;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- tree

void ManualSeedSelector::rebuildSolverTree()
{
    if (!m_solverTree)
        return;
    const QSignalBlocker block(m_solverTree);
    m_solverTree->clear();
    m_solverTreeItems.assign(m_solverNetwork.segments.size(), nullptr);
    const int count = static_cast<int>(m_solverNetwork.segments.size());

    auto makeItem = [this](int i)
    {
        const SolverSegment &s = m_solverNetwork.segments[static_cast<size_t>(i)];
        auto *item = new QTreeWidgetItem();
        item->setText(0, s.label);
        item->setText(1, mmText(s.lengthM));
        item->setText(2, mmText(2.0 * s.radiusM));
        item->setText(3, s.outlet ? QStringLiteral("●") : QString());
        for (int c = 1; c < 3; ++c)
            item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, kSegmentRole, i);
        item->setToolTip(0, QString("%1: node %2 → %3").arg(s.label).arg(s.sn).arg(s.tn));
        m_solverTreeItems[static_cast<size_t>(i)] = item;
        return item;
    };
    auto widestFirst = [this](std::vector<int> daughters)
    {
        std::stable_sort(daughters.begin(), daughters.end(), [this](int a, int b)
                         { return m_solverNetwork.segments[static_cast<size_t>(a)].radiusM >
                                  m_solverNetwork.segments[static_cast<size_t>(b)].radiusM; });
        return daughters;
    };
    // A chain: the segment, then its wider daughter as the next sibling, and so on;
    // every narrower daughter starts a chain nested under the segment it leaves.
    // An item already made is never made again, so a cyclic file cannot loop.
    std::function<void(QTreeWidgetItem *, int)> addChain = [&](QTreeWidgetItem *parent, int start)
    {
        int current = start;
        while (current >= 0 && !m_solverTreeItems[static_cast<size_t>(current)])
        {
            QTreeWidgetItem *item = makeItem(current);
            parent->addChild(item);
            const std::vector<int> daughters = widestFirst(m_solverNetwork.daughtersOf(current));
            for (size_t d = 1; d < daughters.size(); ++d)
                addChain(item, daughters[d]);
            current = daughters.empty() ? -1 : daughters.front();
        }
    };
    // Inlets root the tree; the trunk's daughters are both main arteries, so both nest.
    for (int i = 0; i < count; ++i)
    {
        if (m_solverNetwork.parentOf(i) >= 0 || m_solverTreeItems[static_cast<size_t>(i)])
            continue;
        QTreeWidgetItem *root = makeItem(i);
        m_solverTree->addTopLevelItem(root);
        for (int d : widestFirst(m_solverNetwork.daughtersOf(i)))
            addChain(root, d);
        root->setExpanded(true);
    }
    // A malformed file (a cycle, a missing parent) still lists every segment.
    for (int i = 0; i < count; ++i)
        if (!m_solverTreeItems[static_cast<size_t>(i)])
            m_solverTree->addTopLevelItem(makeItem(i));
}

void ManualSeedSelector::filterSolverTree(const QString &text)
{
    if (!m_solverTree)
        return;
    const QString needle = text.trimmed();
    std::function<bool(QTreeWidgetItem *)> apply = [&](QTreeWidgetItem *item) -> bool
    {
        bool childMatches = false;
        for (int c = 0; c < item->childCount(); ++c)
            childMatches |= apply(item->child(c));
        const bool matches = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!(matches || childMatches));
        if (!needle.isEmpty() && childMatches)
            item->setExpanded(true);
        return matches || childMatches;
    };
    for (int i = 0; i < m_solverTree->topLevelItemCount(); ++i)
        apply(m_solverTree->topLevelItem(i));
}

// ---------------------------------------------------------------- selection

void ManualSeedSelector::selectSolverSegment(int index, bool jump)
{
    if (index < 0 || index >= static_cast<int>(m_solverNetwork.segments.size()))
        return;
    m_selectedSolverSegment = index;
    m_selectedSolverNode = -1;
    if (m_solverTree && index < static_cast<int>(m_solverTreeItems.size()) && m_solverTreeItems[static_cast<size_t>(index)])
    {
        const QSignalBlocker block(m_solverTree);
        QTreeWidgetItem *item = m_solverTreeItems[static_cast<size_t>(index)];
        for (QTreeWidgetItem *p = item->parent(); p; p = p->parent())
            p->setExpanded(true);
        m_solverTree->setCurrentItem(item);
        m_solverTree->scrollToItem(item);
    }
    int x = 0, y = 0, z = 0;
    if (jump && solverSegmentFocus(index, x, y, z))
        jumpToVoxel(x, y, z);
    if (m_locatedPoint.valid)
        m_locatedPoint.text = m_solverNetwork.segments[static_cast<size_t>(index)].label;
    refreshSolverDetails();
    updateSolverHighlight3D();
    requestViewUpdate(true);
}

void ManualSeedSelector::selectSolverNode(int id, bool jump)
{
    if (solverNodeKind(m_solverNetwork, id) == SolverNodeKind::Unknown)
        return;
    m_selectedSolverNode = id;
    m_selectedSolverSegment = -1;
    if (m_solverTree)
    {
        const QSignalBlocker block(m_solverTree);
        m_solverTree->setCurrentItem(nullptr);
        m_solverTree->clearSelection();
    }
    const SolverGeometry::Point *p = m_solverGeometryPlaced ? m_solverGeometry.node(id) : nullptr;
    if (jump && p && solverNetworkPlaced())
        jumpToVoxel(static_cast<int>(std::lround(p->x)), static_cast<int>(std::lround(p->y)),
                    static_cast<int>(std::lround(p->z)));
    if (m_locatedPoint.valid)
        m_locatedPoint.text = QString("node %1").arg(id);
    refreshSolverDetails();
    updateSolverHighlight3D();
    requestViewUpdate(true);
}

void ManualSeedSelector::onSolverLinkActivated(const QString &target)
{
    const int colon = target.indexOf(':');
    if (colon < 0)
        return;
    const QString kind = target.left(colon);
    const int value = target.mid(colon + 1).toInt();
    if (kind == "seg")
        selectSolverSegment(value, true);
    else if (kind == "node")
        selectSolverNode(value, true);
}

void ManualSeedSelector::refreshSolverDetails()
{
    if (!m_solverDetails)
        return;
    const SolverNetwork &net = m_solverNetwork;
    const double voxelMl = m_image.getSpacingX() * m_image.getSpacingY() * m_image.getSpacingZ() / 1000.0;
    auto segLink = [&](int i) { return link(QString("seg:%1").arg(i), net.segments[static_cast<size_t>(i)].label); };
    auto nodeLink = [&](int id)
    {
        return link(QString("node:%1").arg(id), QString("node %1").arg(id)) +
               QString(" <span style='color:gray'>(%1)</span>").arg(solverNodeKindName(solverNodeKind(net, id)));
    };
    auto join = [&](const std::vector<int> &segs, const QString &none)
    {
        QStringList parts;
        for (int i : segs)
            parts << segLink(i);
        return parts.isEmpty() ? none : parts.join(", ");
    };

    QStringList html;
    if (m_selectedSolverSegment >= 0 && m_selectedSolverSegment < static_cast<int>(net.segments.size()))
    {
        const int i = m_selectedSolverSegment;
        const SolverSegment &s = net.segments[static_cast<size_t>(i)];
        html << QString("<b>%1</b> <span style='color:gray'>#%2%3</span>")
                    .arg(s.label.toHtmlEscaped())
                    .arg(s.index)
                    .arg(s.outlet ? QStringLiteral(" · outlet") : QString());
        QString sizes = QString("L %1 mm · Ø %2 mm (R0 %3)").arg(mmText(s.lengthM), mmText(2.0 * s.radiusM, 2),
                                                                 mmText(s.radiusM, 2));
        if (s.youngPa > 0.0)
            sizes += QString(" · E %1 kPa").arg(s.youngPa / 1000.0, 0, 'f', 0);
        html << sizes;
        html << QString("From %1 to %2").arg(nodeLink(s.sn), nodeLink(s.tn));
        const int parent = net.parentOf(i);
        html << QString("Parent: %1").arg(parent < 0 ? QStringLiteral("none (inlet)") : segLink(parent));
        html << QString("Daughters: %1").arg(join(net.daughtersOf(i), QStringLiteral("none (outlet)")));
        if (m_solverGeometryPlaced && i < static_cast<int>(m_solverGeometry.segments.size()))
        {
            const SolverGeometry::Segment &g = m_solverGeometry.segments[static_cast<size_t>(i)];
            html << QString("Image: lumen %1 mL · territory %2 mL")
                        .arg(g.lumenVoxels * voxelMl, 0, 'f', 2)
                        .arg(g.territoryVoxels * voxelMl, 0, 'f', 2);
            if (g.lumenVoxels == 0)
                html << "<span style='color:gray'>No lumen of its own: a connector the export inserted "
                        "to split a node into bifurcations.</span>";
        }
        QStringList rest;
        static const QStringList shown = {"label", "sn", "tn", "L", "R0", "E"};
        for (const auto &kv : s.fields)
            if (!shown.contains(kv.first))
                rest << QString("%1 %2").arg(kv.first.toHtmlEscaped(), kv.second.toHtmlEscaped());
        if (!rest.isEmpty())
            html << QString("<span style='color:gray'>%1</span>").arg(rest.join(" · "));
    }
    else if (m_selectedSolverNode >= 0)
    {
        const int id = m_selectedSolverNode;
        html << QString("<b>Node %1</b> <span style='color:gray'>· %2</span>")
                    .arg(id)
                    .arg(solverNodeKindName(solverNodeKind(net, id)));
        if (const SolverGeometry::Point *p = m_solverGeometryPlaced ? m_solverGeometry.node(id) : nullptr)
            html << QString("Voxel (%1, %2, %3)")
                        .arg(std::lround(p->x))
                        .arg(std::lround(p->y))
                        .arg(std::lround(p->z));
        html << QString("In: %1").arg(join(net.segmentsInto(id), QStringLiteral("none (the inlet)")));
        html << QString("Out: %1").arg(join(net.segmentsOutOf(id), QStringLiteral("none (Windkessel)")));
    }
    else if (!net.empty())
    {
        html << "<span style='color:gray'>Pick a segment in the tree, a link here, or right-click a vessel.</span>";
    }
    m_solverDetails->setText(html.join("<br>"));
}

// ---------------------------------------------------------------- hover

void ManualSeedSelector::setSolverHover(int segment, int node, int x, int y, int z, int plane)
{
    const bool changed = segment != m_hoverSolverSegment || node != m_hoverSolverNode || plane != m_hoverSolverPlane;
    const bool moved = x != m_hoverSolverVoxel[0] || y != m_hoverSolverVoxel[1] || z != m_hoverSolverVoxel[2];
    if (!changed && !moved)
        return;
    const bool labelled = m_hoverSolverSegment > 0 || m_hoverSolverNode >= 0 || segment > 0 || node >= 0;
    m_hoverSolverSegment = segment;
    m_hoverSolverNode = node;
    m_hoverSolverPlane = plane;
    m_hoverSolverVoxel[0] = x;
    m_hoverSolverVoxel[1] = y;
    m_hoverSolverVoxel[2] = z;
    // The label follows the pointer on the slices: repaint the overlays, nothing else.
    if (labelled || changed)
        for (OrthogonalView *view : {m_axialView, m_sagittalView, m_coronalView})
            if (view)
                view->update();
    if (changed)
        scheduleSolverHighlight3D();
}

void ManualSeedSelector::scheduleSolverHighlight3D()
{
    if (m_solver3DTimer && !m_solver3DTimer->isActive())
        m_solver3DTimer->start();
}

void ManualSeedSelector::rebuildSolverGraph3D()
{
    if (!m_mask3DView)
        return;
    NetworkGraph3D graph;
    if (m_solverGeometryPlaced && solverNetworkPlaced())
    {
        for (size_t i = 0; i < m_solverGeometry.segments.size(); ++i)
        {
            NetworkGraph3D::Line line;
            for (const auto &p : solverCenterlineVoxels(static_cast<int>(i)))
                line.points.push_back(p);
            line.color = solverSegmentColor(static_cast<int>(i) + 1).lighter(130);
            graph.lines.push_back(std::move(line));
        }
        for (const auto &entry : m_solverGeometry.nodes)
        {
            NetworkGraph3D::Node node;
            node.position = {entry.second.x, entry.second.y, entry.second.z};
            node.color = nodeColor(solverNodeKind(m_solverNetwork, entry.first));
            graph.nodes.push_back(node);
        }
    }
    m_mask3DView->setNetworkVisible(!m_solverGraph3DBox || m_solverGraph3DBox->isChecked());
    m_mask3DView->setNetworkGraph(graph);
    updateSolverHighlight3D();
}

void ManualSeedSelector::updateSolverHighlight3D()
{
    if (!m_mask3DView)
        return;
    std::vector<std::array<double, 3>> selectedLine, hoveredLine;
    std::array<double, 3> selectedNode{}, hoveredNode{};
    bool hasSelectedNode = false, hasHoveredNode = false;
    std::vector<Annotation3D> notes;
    const bool placed = m_solverGeometryPlaced && solverNetworkPlaced();
    auto note = [](double x, double y, double z, const QString &text, const QColor &colour, bool emphasised)
    {
        Annotation3D a;
        a.x = static_cast<int>(std::lround(x));
        a.y = static_cast<int>(std::lround(y));
        a.z = static_cast<int>(std::lround(z));
        a.text = text;
        a.color = colour;
        a.emphasised = emphasised;
        return a;
    };

    if (placed && m_selectedSolverSegment >= 0)
    {
        selectedLine = solverCenterlineVoxels(m_selectedSolverSegment);
        int x = 0, y = 0, z = 0;
        if (solverSegmentFocus(m_selectedSolverSegment, x, y, z))
            notes.push_back(note(x, y, z, m_solverNetwork.segments[static_cast<size_t>(m_selectedSolverSegment)].label,
                                 kSelectColor, true));
    }
    if (placed && m_selectedSolverNode >= 0)
        if (const SolverGeometry::Point *p = m_solverGeometry.node(m_selectedSolverNode))
        {
            selectedNode = {p->x, p->y, p->z};
            hasSelectedNode = true;
            notes.push_back(note(p->x, p->y, p->z, QString("node %1").arg(m_selectedSolverNode), kSelectColor, true));
        }
    // Only a 3D hover gets a 3D label; a slice hover names itself on the slice.
    if (placed && m_hoverSolverNode >= 0)
    {
        if (const SolverGeometry::Point *p = m_solverGeometry.node(m_hoverSolverNode))
        {
            hoveredNode = {p->x, p->y, p->z};
            hasHoveredNode = true;
            if (m_hoverSolverPlane < 0 && m_hoverSolverNode != m_selectedSolverNode)
                notes.push_back(note(p->x, p->y, p->z,
                                     QString("node %1 · %2").arg(m_hoverSolverNode)
                                         .arg(solverNodeKindName(solverNodeKind(m_solverNetwork, m_hoverSolverNode))),
                                     QColor(Qt::white), false));
        }
    }
    else if (placed && m_hoverSolverSegment > 0)
    {
        hoveredLine = solverCenterlineVoxels(m_hoverSolverSegment - 1);
        if (m_hoverSolverPlane < 0 && m_hoverSolverSegment - 1 != m_selectedSolverSegment)
        {
            const SolverSegment &s = m_solverNetwork.segments[static_cast<size_t>(m_hoverSolverSegment - 1)];
            notes.push_back(note(m_hoverSolverVoxel[0], m_hoverSolverVoxel[1], m_hoverSolverVoxel[2],
                                 QString("%1 · Ø %2 mm").arg(s.label, mmText(2.0 * s.radiusM)),
                                 solverSegmentColor(m_hoverSolverSegment), false));
        }
    }
    m_mask3DView->setAnnotations(notes);
    m_mask3DView->setNetworkHighlight(selectedLine, hasSelectedNode ? &selectedNode : nullptr, hoveredLine,
                                      hasHoveredNode ? &hoveredNode : nullptr);
}

// ---------------------------------------------------------------- slices

void ManualSeedSelector::drawSolverNetworkOverlay(QPainter &p, float scaleX, float scaleY, SlicePlane plane) const
{
    if (!solverNetworkPlaced() || scaleX <= 0.0f || scaleY <= 0.0f)
        return;
    const bool graph = m_solverGeometryPlaced && (!m_solverGraph2DBox || m_solverGraph2DBox->isChecked());
    const double spacing[3] = {m_image.getSpacingX(), m_image.getSpacingY(), m_image.getSpacingZ()};
    int depthAxis = 2;
    int slice = m_axialSlider ? m_axialSlider->value() : 0;
    if (plane == SlicePlane::Sagittal)
    {
        depthAxis = 0;
        slice = m_sagittalSlider ? m_sagittalSlider->value() : 0;
    }
    else if (plane == SlicePlane::Coronal)
    {
        depthAxis = 1;
        slice = m_coronalSlider ? m_coronalSlider->value() : 0;
    }
    const double slab = kSlabMm / spacing[depthAxis];
    auto project = [&](double x, double y, double z) -> PlaneCoords
    {
        switch (plane)
        {
        case SlicePlane::Sagittal:
            return {y, z, x};
        case SlicePlane::Coronal:
            return {x, z, y};
        default:
            return {x, y, z};
        }
    };
    auto screen = [&](const PlaneCoords &c) { return QPointF(c.u * scaleX, c.v * scaleY); };
    auto nearSlice = [&](double depth) { return std::fabs(depth - slice) <= slab; };

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    if (graph)
    {
        // Every segment where it crosses this slab, thin, in its own colour.
        for (size_t i = 0; i < m_solverGeometry.segments.size(); ++i)
        {
            const auto &line = m_solverGeometry.segments[i].centerline;
            const int label = static_cast<int>(i) + 1;
            const bool selected = static_cast<int>(i) == m_selectedSolverSegment;
            const bool hovered = label == m_hoverSolverSegment && m_hoverSolverNode < 0;
            QColor colour = selected ? kSelectColor : hovered ? QColor(Qt::white) : solverSegmentColor(label);
            if (!selected && !hovered)
                colour.setAlpha(190);
            const qreal width = selected ? 3.0 : hovered ? 2.5 : 1.2;
            for (size_t k = 1; k < line.size(); ++k)
            {
                const PlaneCoords a = project(line[k - 1].x, line[k - 1].y, line[k - 1].z);
                const PlaneCoords b = project(line[k].x, line[k].y, line[k].z);
                const bool crosses = std::min(a.depth, b.depth) <= slice + slab &&
                                     std::max(a.depth, b.depth) >= slice - slab;
                if (crosses)
                {
                    p.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(screen(a), screen(b));
                }
                else if (selected)
                {
                    // Where the selection goes off this slice: its shadow, dashed.
                    QColor faint = kSelectColor;
                    faint.setAlpha(110);
                    p.setPen(QPen(faint, 1.0, Qt::DashLine));
                    p.drawLine(screen(a), screen(b));
                }
            }
        }
        // Nodes in the slab: a dot coloured by kind; rings for the selection and hover.
        for (const auto &entry : m_solverGeometry.nodes)
        {
            const PlaneCoords c = project(entry.second.x, entry.second.y, entry.second.z);
            const bool selected = entry.first == m_selectedSolverNode;
            const bool hovered = entry.first == m_hoverSolverNode;
            if (!nearSlice(c.depth) && !selected)
                continue;
            const QPointF at = screen(c);
            p.setPen(QPen(QColor(0, 0, 0, 200), 1.0));
            p.setBrush(nodeColor(solverNodeKind(m_solverNetwork, entry.first)));
            p.drawEllipse(at, 3.0, 3.0);
            if (selected || hovered)
            {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(selected ? kSelectColor : QColor(Qt::white), 2.0));
                p.drawEllipse(at, 7.0, 7.0);
            }
        }
    }

    // The hover label, on the view the pointer is in, beside the pointer.
    if (m_hoverSolverPlane == static_cast<int>(plane) && (m_hoverSolverSegment > 0 || m_hoverSolverNode >= 0))
    {
        QString text;
        if (m_hoverSolverNode >= 0)
        {
            text = QString("node %1 · %2").arg(m_hoverSolverNode)
                       .arg(solverNodeKindName(solverNodeKind(m_solverNetwork, m_hoverSolverNode)));
        }
        else
        {
            const SolverSegment &s = m_solverNetwork.segments[static_cast<size_t>(m_hoverSolverSegment - 1)];
            text = QString("%1 · L %2 · Ø %3 mm").arg(s.label, mmText(s.lengthM), mmText(2.0 * s.radiusM));
            const int lumen = solverLumenSegmentAtVoxel(m_hoverSolverVoxel[0], m_hoverSolverVoxel[1], m_hoverSolverVoxel[2]);
            if (!m_solverLumenIndex.empty() && lumen != m_hoverSolverSegment)
                text += QStringLiteral(" · fed, not modelled");
        }
        const QPointF at = screen(project(m_hoverSolverVoxel[0], m_hoverSolverVoxel[1], m_hoverSolverVoxel[2])) +
                           QPointF(12.0, -10.0);
        QFont font = p.font();
        font.setBold(true);
        p.setFont(font);
        const QRectF box = QRectF(p.fontMetrics().boundingRect(text)).adjusted(-4, -2, 4, 2).translated(at);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 15, 20, 215));
        p.drawRoundedRect(box, 3.0, 3.0);
        p.setPen(Qt::white);
        p.drawText(at, text);
    }
    p.restore();
}
