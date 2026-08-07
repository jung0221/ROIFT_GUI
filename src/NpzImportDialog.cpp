#include "NpzImportDialog.h"
#include "SectionGroup.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{

constexpr int kPreviewSize = 240;      // on-screen edge of the preview, in pixels
constexpr size_t kPreviewMaxExtent = 128; // per-axis cap of the cached subsample
// Above this the array is not subsampled for preview: reading it whole would
// cost more memory than the import itself.
constexpr size_t kPreviewMaxElements = 150ull * 1000ull * 1000ull;

// Arrays that can stand for a volume: three spatial axes, optionally channelled.
bool isImportable(const npz::ArrayInfo &info)
{
    return (info.shape.size() == 3 || info.shape.size() == 4) && info.elementCount() > 0;
}

int importableCount(const std::vector<npz::ArrayInfo> &arrays)
{
    int count = 0;
    for (const npz::ArrayInfo &info : arrays)
        if (isImportable(info))
            ++count;
    return count;
}

QString arrayLabel(const npz::ArrayInfo &info)
{
    return info.name.empty() ? QStringLiteral("(single array)") : QString::fromStdString(info.name);
}

struct AxisOrderChoice
{
    NpzImportOptions::AxisOrder order;
    const char *label;
};

// Named after the producers that actually emit each layout, so the choice can
// be made from how the file was written rather than by trial and error.
const AxisOrderChoice kAxisOrderChoices[] = {
    {NpzImportOptions::AxisOrder::Auto, "Automatic"},
    {NpzImportOptions::AxisOrder::ZYX, "Z, Y, X  -  SimpleITK / nnUNet"},
    {NpzImportOptions::AxisOrder::XYZ, "X, Y, Z  -  nibabel"},
    {NpzImportOptions::AxisOrder::YXZ, "Y, X, Z  -  rows, columns, slices (stacked DICOM)"},
    {NpzImportOptions::AxisOrder::XZY, "X, Z, Y"},
    {NpzImportOptions::AxisOrder::YZX, "Y, Z, X"},
    {NpzImportOptions::AxisOrder::ZXY, "Z, X, Y"},
};

} // namespace

NpzImportDialog::NpzImportDialog(const QString &path, const std::vector<npz::ArrayInfo> &arrays,
                                 const QString &autoSelectedArray, QWidget *parent)
    : QDialog(parent), m_path(path), m_arrays(arrays)
{
    setWindowTitle("Import numpy volume - " + QFileInfo(path).fileName());
    setMinimumWidth(720);

    QVBoxLayout *root = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        "A numpy file stores samples only: no spacing, no orientation and no axis convention. "
        "Check the preview - if the anatomy looks transposed or mirrored, change the axis order.");
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({"Array", "Shape", "Type"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMaximumHeight(130);

    int selectRow = -1;
    for (const npz::ArrayInfo &info : m_arrays)
    {
        if (!isImportable(info))
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(arrayLabel(info)));
        m_table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(info.shapeString())));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::fromLatin1(npz::dtypeName(info.dtype))));
        m_table->item(row, 0)->setData(Qt::UserRole, QString::fromStdString(info.name));
        if (selectRow < 0 && QString::fromStdString(info.name) == autoSelectedArray)
            selectRow = row;
    }
    m_table->resizeColumnsToContents();
    root->addWidget(m_table);

    // Left: how to read the array. Right: what that produces.
    QHBoxLayout *body = new QHBoxLayout();

    QVBoxLayout *controls = new QVBoxLayout();
    QGridLayout *grid = new QGridLayout();
    grid->addWidget(new QLabel("Axis order:"), 0, 0);
    m_axisOrder = new QComboBox();
    for (const AxisOrderChoice &choice : kAxisOrderChoices)
        m_axisOrder->addItem(QString::fromLatin1(choice.label), static_cast<int>(choice.order));
    m_axisOrder->setToolTip(
        "What the array axes are, in order. ZYX means the array is indexed [z][y][x].\n"
        "Getting this wrong transposes the volume: the axial view shows a coronal slice, or\n"
        "the sagittal and coronal panels swap.");
    grid->addWidget(m_axisOrder, 0, 1);

    grid->addWidget(new QLabel("Channel:"), 1, 0);
    m_channel = new QSpinBox();
    m_channel->setRange(0, 0);
    m_channel->setToolTip("Which channel of a 4D array to show, e.g. one class of a softmax volume.");
    grid->addWidget(m_channel, 1, 1);
    controls->addLayout(grid);

    SectionGroup *flipBox = new SectionGroup("Mirror");
    QHBoxLayout *flipLayout = new QHBoxLayout(flipBox);
    const char *const flipLabels[3] = {"X (left-right)", "Y (front-back)", "Z (head-foot)"};
    for (int i = 0; i < 3; ++i)
    {
        m_flip[i] = new QCheckBox(QString::fromLatin1(flipLabels[i]));
        flipLayout->addWidget(m_flip[i]);
    }
    flipBox->setToolTip("A numpy file records no handedness. Mirror an axis if the preview is flipped.");
    controls->addWidget(flipBox);

    SectionGroup *geometryBox = new SectionGroup("Geometry");
    QVBoxLayout *geometryLayout = new QVBoxLayout(geometryBox);
    m_summary = new QLabel();
    m_summary->setWordWrap(true);
    geometryLayout->addWidget(m_summary);
    m_warning = new QLabel();
    m_warning->setWordWrap(true);
    // Flag ink: this is a rejection of the file's own geometry, not a decoration.
    m_warning->setStyleSheet(QString("color: %1; font-weight: 600;").arg(Theme::kFlag));
    m_warning->setVisible(false);
    geometryLayout->addWidget(m_warning);

    m_overrideSpacing = new QCheckBox("Set voxel spacing manually (mm)");
    geometryLayout->addWidget(m_overrideSpacing);
    QHBoxLayout *spacingRow = new QHBoxLayout();
    const char *const axisNames[3] = {"X:", "Y:", "Z:"};
    for (int i = 0; i < 3; ++i)
    {
        spacingRow->addWidget(new QLabel(axisNames[i]));
        m_spacing[i] = new QDoubleSpinBox();
        m_spacing[i]->setRange(0.0001, 1000.0);
        m_spacing[i]->setDecimals(4);
        m_spacing[i]->setValue(1.0);
        m_spacing[i]->setEnabled(false);
        spacingRow->addWidget(m_spacing[i]);
    }
    spacingRow->addStretch(1);
    geometryLayout->addLayout(spacingRow);
    controls->addWidget(geometryBox);
    controls->addStretch(1);
    body->addLayout(controls, 1);

    QVBoxLayout *previewColumn = new QVBoxLayout();
    m_preview = new QLabel();
    m_preview->setFixedSize(kPreviewSize, kPreviewSize);
    m_preview->setAlignment(Qt::AlignCenter);
    // The preview is a recessed slot the slice drops into, so it takes the well.
    m_preview->setStyleSheet(QString("background: %1; border: none; border-radius: %2px;")
                                 .arg(Theme::kWell)
                                 .arg(Theme::kRadiusRow));
    previewColumn->addWidget(m_preview);
    m_previewCaption = new QLabel();
    m_previewCaption->setAlignment(Qt::AlignCenter);
    m_previewCaption->setWordWrap(true);
    m_previewCaption->setFixedWidth(kPreviewSize);
    previewColumn->addWidget(m_previewCaption);
    previewColumn->addStretch(1);
    body->addLayout(previewColumn, 0);

    root->addLayout(body);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &NpzImportDialog::onArraySelectionChanged);
    connect(m_axisOrder, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
            { refreshPreview(); });
    connect(m_channel, qOverload<int>(&QSpinBox::valueChanged), this, [this](int)
            { refreshPreview(); });
    for (QCheckBox *flip : m_flip)
        connect(flip, &QCheckBox::toggled, this, [this](bool)
                { refreshPreview(); });
    connect(m_overrideSpacing, &QCheckBox::toggled, this, [this](bool on)
            {
        for (QDoubleSpinBox *box : m_spacing)
            box->setEnabled(on);
        refreshPreview(); });

    Theme::guardWheel(this);

    if (m_table->rowCount() > 0)
        m_table->selectRow(selectRow >= 0 ? selectRow : 0); // triggers the first load
}

const npz::ArrayInfo *NpzImportDialog::selectedArray() const
{
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    if (selected.isEmpty())
        return nullptr;
    const std::string name =
        m_table->item(selected.first()->row(), 0)->data(Qt::UserRole).toString().toStdString();
    for (const npz::ArrayInfo &info : m_arrays)
        if (info.name == name)
            return &info;
    return nullptr;
}

NpzImportOptions NpzImportDialog::options() const
{
    NpzImportOptions options;
    if (const npz::ArrayInfo *info = selectedArray())
        options.arrayName = info->name;
    options.channel = m_channel->value();
    options.axisOrder = static_cast<NpzImportOptions::AxisOrder>(m_axisOrder->currentData().toInt());
    for (int i = 0; i < 3; ++i)
        options.flip[i] = m_flip[i]->isChecked();
    if (m_overrideSpacing->isChecked())
        for (int i = 0; i < 3; ++i)
            options.spacing[i] = m_spacing[i]->value();
    return options;
}

// A newly picked array may not have the previous one's channel, which would
// otherwise leave the preview stuck reporting an out-of-range channel.
void NpzImportDialog::onArraySelectionChanged()
{
    {
        QSignalBlocker blocker(m_channel);
        m_channel->setRange(0, 0);
        m_channel->setValue(0);
    }
    loadPreviewData();
    refreshPreview();
}

// Cache a coarse copy of the array once, so switching axis order is instant.
void NpzImportDialog::loadPreviewData()
{
    m_previewData.clear();
    m_previewShape.clear();
    m_previewError.clear();

    const npz::ArrayInfo *info = selectedArray();
    if (!info)
        return;
    m_previewArray = info->name;

    if (info->elementCount() > kPreviewMaxElements)
    {
        m_previewError = "Array too large to preview.";
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    npz::ArrayInfo readInfo;
    std::vector<float> full;
    std::string error;
    const bool ok = npz::readAllAsFloat(m_path.toStdString(), info->name, &readInfo, full, &error);
    QApplication::restoreOverrideCursor();
    if (!ok)
    {
        m_previewError = QString::fromStdString(error);
        return;
    }

    // Stride each spatial axis down to at most kPreviewMaxExtent samples; keep
    // any channel axis whole so channel selection still works in the preview.
    const size_t nd = readInfo.shape.size();
    NpzAxisMapping probe;
    const bool channelled = npzBuildAxisMapping(readInfo.shape, NpzImportOptions::AxisOrder::XYZ, probe) &&
                            probe.hasChannel;

    std::vector<size_t> step(nd, 1);
    m_previewShape.assign(nd, 0);
    for (size_t axis = 0; axis < nd; ++axis)
    {
        const bool isChannel = channelled && axis == probe.channelAxis;
        step[axis] = isChannel ? 1 : std::max<size_t>(1, (readInfo.shape[axis] + kPreviewMaxExtent - 1) / kPreviewMaxExtent);
        m_previewShape[axis] = (readInfo.shape[axis] + step[axis] - 1) / step[axis];
    }

    std::vector<size_t> fullStride(nd, 1);
    for (size_t axis = nd - 1; axis-- > 0;)
        fullStride[axis] = fullStride[axis + 1] * readInfo.shape[axis + 1];

    size_t total = 1;
    for (size_t extent : m_previewShape)
        total *= extent;
    m_previewData.resize(total);

    std::vector<size_t> index(nd, 0);
    for (size_t i = 0; i < total; ++i)
    {
        size_t source = 0;
        for (size_t axis = 0; axis < nd; ++axis)
            source += index[axis] * step[axis] * fullStride[axis];
        m_previewData[i] = full[source];
        for (size_t axis = nd; axis-- > 0;)
        {
            if (++index[axis] < m_previewShape[axis])
                break;
            index[axis] = 0;
        }
    }

    // Window on percentiles: CT background sits at -1024 and would otherwise
    // flatten the whole range into near-black.
    std::vector<float> sorted = m_previewData;
    std::sort(sorted.begin(), sorted.end());
    if (!sorted.empty())
    {
        m_previewLow = sorted[static_cast<size_t>(sorted.size() * 0.02)];
        m_previewHigh = sorted[static_cast<size_t>(sorted.size() * 0.98)];
        if (!(m_previewHigh > m_previewLow))
        {
            m_previewLow = sorted.front();
            m_previewHigh = sorted.back() > sorted.front() ? sorted.back() : sorted.front() + 1.0f;
        }
    }
}

// Draw the middle axial slice under the current mapping, from the cached copy.
void NpzImportDialog::renderPreview(const NpzImportReport &report)
{
    const npz::ArrayInfo *info = selectedArray();
    if (!info || m_previewData.empty() || m_previewShape.empty())
    {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(m_previewError.isEmpty() ? "No preview" : m_previewError);
        m_previewCaption->clear();
        return;
    }

    NpzAxisMapping mapping;
    if (!npzBuildAxisMapping(info->shape, report.axisOrder, mapping))
    {
        m_preview->setPixmap(QPixmap());
        m_preview->setText("No preview");
        m_previewCaption->clear();
        return;
    }

    const size_t nd = m_previewShape.size();
    std::vector<size_t> stride(nd, 1);
    for (size_t axis = nd - 1; axis-- > 0;)
        stride[axis] = stride[axis + 1] * m_previewShape[axis + 1];

    const size_t sizeX = m_previewShape[mapping.axisForX];
    const size_t sizeY = m_previewShape[mapping.axisForY];
    const size_t sizeZ = m_previewShape[mapping.axisForZ];
    if (sizeX == 0 || sizeY == 0 || sizeZ == 0)
        return;

    const size_t channel = mapping.hasChannel
                               ? std::min<size_t>(static_cast<size_t>(report.channel),
                                                  m_previewShape[mapping.channelAxis] - 1)
                               : 0;
    const size_t channelBase = mapping.hasChannel ? channel * stride[mapping.channelAxis] : 0;
    const size_t sliceZ = sizeZ / 2;

    auto sourceIndex = [](size_t i, size_t extent, bool flipped)
    { return flipped ? (extent - 1 - i) : i; };

    QImage image(static_cast<int>(sizeX), static_cast<int>(sizeY), QImage::Format_Grayscale8);
    const float span = (m_previewHigh > m_previewLow) ? (m_previewHigh - m_previewLow) : 1.0f;
    const size_t baseZ = channelBase + sourceIndex(sliceZ, sizeZ, report.flip[2]) * stride[mapping.axisForZ];
    for (size_t y = 0; y < sizeY; ++y)
    {
        uchar *row = image.scanLine(static_cast<int>(y));
        const size_t baseY = baseZ + sourceIndex(y, sizeY, report.flip[1]) * stride[mapping.axisForY];
        for (size_t x = 0; x < sizeX; ++x)
        {
            const float value = m_previewData[baseY + sourceIndex(x, sizeX, report.flip[0]) * stride[mapping.axisForX]];
            const float normalised = std::clamp((value - m_previewLow) / span, 0.0f, 1.0f);
            row[x] = static_cast<uchar>(normalised * 255.0f);
        }
    }

    m_preview->setText(QString());
    m_preview->setPixmap(QPixmap::fromImage(image).scaled(kPreviewSize, kPreviewSize,
                                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_previewCaption->setText(QString("Axial slice %1 of %2 - read as %3")
                                  .arg(sliceZ + 1)
                                  .arg(sizeZ)
                                  .arg(QString::fromLatin1(npzAxisOrderName(report.axisOrder))));
}

// Ask the importer what the current settings would produce, so the dialog can
// never describe a reading the loader would not actually perform.
void NpzImportDialog::refreshPreview()
{
    if (m_refreshing)
        return;
    m_refreshing = true;

    NpzImportReport report;
    std::string error;
    const bool ok = NiftiImage::previewNumpy(m_path.toStdString(), options(), report, &error);

    if (ok)
    {
        m_channel->setEnabled(report.channelCount > 1);
        m_channel->setRange(0, std::max(0, report.channelCount - 1));

        m_summary->setText(QString("Volume %1 x %2 x %3 voxels, spacing %4 x %5 x %6 mm\nGeometry from: %7")
                               .arg(report.size[0])
                               .arg(report.size[1])
                               .arg(report.size[2])
                               .arg(report.spacing[0], 0, 'g', 4)
                               .arg(report.spacing[1], 0, 'g', 4)
                               .arg(report.spacing[2], 0, 'g', 4)
                               .arg(QString::fromStdString(report.geometrySource)));
        QStringList warnings;
        if (!report.geometryResolved)
            warnings << "No voxel spacing could be found for this file. It will be shown as 1 mm "
                        "isotropic, so measured distances, volumes and the 3D view proportions will be "
                        "wrong. Set the spacing below, or put the matching .nii.gz next to it.";
        if (report.axisOrderAmbiguous &&
            options().axisOrder == NpzImportOptions::AxisOrder::Auto)
            warnings << "The axis order could not be determined: several orders fit this shape. Check "
                        "the preview and pick one explicitly.";
        m_warning->setVisible(!warnings.isEmpty());
        m_warning->setText(warnings.join("\n\n"));

        if (!m_overrideSpacing->isChecked())
            for (int i = 0; i < 3; ++i)
                m_spacing[i]->setValue(report.spacing[i]);

        renderPreview(report);
    }
    else
    {
        m_summary->setText("Cannot import this array.");
        m_warning->setVisible(true);
        m_warning->setText(QString::fromStdString(error));
        m_preview->setPixmap(QPixmap());
        m_preview->setText("No preview");
        m_previewCaption->clear();
    }
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);

    m_refreshing = false;
}

bool NpzImportDialog::chooseOptions(QWidget *parent, const QString &path, NpzImportOptions &options)
{
    std::vector<npz::ArrayInfo> arrays;
    std::string error;
    if (!NiftiImage::inspectNumpy(path.toStdString(), arrays, &error))
    {
        QMessageBox::warning(parent, "Cannot read numpy file",
                             QFileInfo(path).fileName() + ":\n" + QString::fromStdString(error));
        return false;
    }
    if (importableCount(arrays) == 0)
    {
        QMessageBox::warning(parent, "Cannot read numpy file",
                             QFileInfo(path).fileName() +
                                 " holds no 3D or 4D array, so it does not contain a volume.");
        return false;
    }

    NpzImportReport report;
    const bool previewed = NiftiImage::previewNumpy(path.toStdString(), NpzImportOptions{}, report, &error);

    // Only skip the dialog when nothing is left to decide: a single array, a
    // single channel, and a reference volume that pinned both geometry and
    // axis order. Without a reference the axis order is a guess, and a wrong
    // guess is not obvious from the numbers alone - so always ask.
    const bool ambiguous = !previewed || importableCount(arrays) > 1 || report.channelCount > 1 ||
                           !report.geometryResolved || report.axisOrderAmbiguous;
    if (!ambiguous)
    {
        options = NpzImportOptions{};
        return true;
    }

    NpzImportDialog dialog(path, arrays, QString::fromStdString(report.arrayName), parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    options = dialog.options();
    return true;
}
