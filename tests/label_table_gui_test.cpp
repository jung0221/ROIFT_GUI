// The label table in the window: rows for the edited mask's labels and the loaded names,
// voxel counts that follow edits and undo without a rescan, finding a label by name or
// number, painting with a row's label, picking the label under a voxel, and the names
// coming back in the next session.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <cstdio>
#include <map>

#include <itkImage.h>
#include <itkImageFileWriter.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-66s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kDim = 24;
constexpr unsigned kDimZ = 6;
using VolumeType = itk::Image<int32_t, 3>;

template <typename ValueAt>
bool writeVolume(const QString &path, ValueAt valueAt)
{
    VolumeType::Pointer image = VolumeType::New();
    VolumeType::SizeType size;
    size[0] = kDim;
    size[1] = kDim;
    size[2] = kDimZ;
    VolumeType::IndexType start;
    start.Fill(0);
    image->SetRegions(VolumeType::RegionType(start, size));
    image->Allocate();
    for (unsigned z = 0; z < kDimZ; ++z)
        for (unsigned y = 0; y < kDim; ++y)
            for (unsigned x = 0; x < kDim; ++x)
            {
                VolumeType::IndexType voxel;
                voxel[0] = static_cast<VolumeType::IndexValueType>(x);
                voxel[1] = static_cast<VolumeType::IndexValueType>(y);
                voxel[2] = static_cast<VolumeType::IndexValueType>(z);
                image->SetPixel(voxel, valueAt(x, y, z));
            }
    try
    {
        auto writer = itk::ImageFileWriter<VolumeType>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->Update();
    }
    catch (...)
    {
        return false;
    }
    return true;
}

// 1 in the left half, 2 in the top right quarter, 18 in a 2 x 2 column.
int maskAt(unsigned x, unsigned y, unsigned)
{
    if (x >= 20 && x < 22 && y >= 20 && y < 22)
        return 18;
    if (x < kDim / 2)
        return 1;
    return y < kDim / 2 ? 2 : 0;
}

std::map<int, long long> histogram(const std::vector<int> &labels)
{
    std::map<int, long long> counts;
    for (int v : labels)
        if (v != 0)
            ++counts[v];
    return counts;
}

QTreeWidgetItem *rowFor(QTreeWidget *tree, int label)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(2, Qt::UserRole).toInt() == label)
            return tree->topLevelItem(i);
    return nullptr;
}

int visibleRows(QTreeWidget *tree)
{
    int n = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        n += !tree->topLevelItem(i)->isHidden();
    return n;
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir workDir;
    if (!workDir.isValid())
        return 1;
    const QDir dir(workDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("settings"));
    qputenv("ROIFT_AUTOSAVE_DIR", dir.filePath("autosave").toLocal8Bit());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("roift-test");
    QCoreApplication::setApplicationName("label_table_gui_test");

    const QString ct = dir.filePath("jaw_0000.nii.gz");
    const QString mask = dir.filePath("jaw_mask.nii.gz");
    const QString json = dir.filePath("dataset.json");
    QFile names(json);
    const bool wrote = writeVolume(ct, [](unsigned x, unsigned y, unsigned) { return int(x * 10 + y); }) &&
                       writeVolume(mask, maskAt) && names.open(QIODevice::WriteOnly) &&
                       names.write(R"json({"labels": {"background": 0, "Mandible": 1, "Maxilla": 2,
                                   "Upper Left First Molar (26)": 18, "Pulp": 37, "ignore": 38}})json") > 0;
    names.close();
    check(wrote, "test data written");
    if (!wrote)
        return 1;

    {
        ManualSeedSelector window("");
        window.addImagesFromPaths({ct});
        check(window.applyMaskFromPath(mask.toStdString()), "the mask is edited");
        auto *tree = window.findChild<QTreeWidget *>("labelTable");
        auto *filter = window.findChild<QLineEdit *>("labelFilter");
        auto *activeName = window.findChild<QLabel *>("activeLabelName");
        auto *axial = window.findChild<OrthogonalView *>("axialView");
        auto *slider = window.findChild<QSlider *>("axialSlider");
        auto *draw = window.findChild<QPushButton *>("maskDrawButton");
        const bool found = tree && filter && activeName && axial && slider && draw;
        check(found, "controls found");
        if (!found)
            return 1;

        check(tree->topLevelItemCount() == 3, "without names: one row per label of the mask");
        check(window.maskLabelCounts() == histogram(window.activeMaskLabels()), "voxel counts from the scan");

        QString error;
        check(window.loadLabelTable(json, &error), "the dataset.json loads");
        check(tree->topLevelItemCount() == 5, "with names: the mask's labels and the table's (ignore included)");
        QTreeWidgetItem *molar = rowFor(tree, 18);
        check(molar && molar->text(3) == "Upper Left First Molar (26)", "names in the rows");
        check(molar && molar->text(4) == QLocale().toString(qlonglong(2 * 2 * kDimZ)), "and voxel counts");
        QTreeWidgetItem *pulp = rowFor(tree, 37);
        check(pulp && pulp->text(4).isEmpty(), "a named label absent from the mask has no count");

        filter->setText("molar");
        check(visibleRows(tree) == 1 && !molar->isHidden(), "the filter keeps the rows whose name matches");
        emit filter->returnPressed();
        check(window.activeLabel() == 18 && activeName->text() == "Upper Left First Molar (26)",
              "Enter paints with it, and its name shows beside the selector");
        filter->setText("26");
        emit filter->returnPressed();
        check(window.activeLabel() == 18, "an FDI number finds its tooth by name first");
        filter->clear();
        check(visibleRows(tree) == 5, "clearing the filter shows every row");

        emit tree->itemClicked(rowFor(tree, 2), 3);
        check(window.activeLabel() == 2, "clicking a row paints with its label");

        // Paint with Pulp: the count follows the stroke, and the undo, without a rescan.
        window.setActiveLabel(37);
        draw->click();
        emit axial->mousePressed(14, 16, Qt::LeftButton);
        for (int x = 15; x <= 18; ++x)
            emit axial->mouseMoved(x, 16, Qt::LeftButton);
        emit axial->mouseReleased(18, 16, Qt::LeftButton);
        const auto afterStroke = histogram(window.activeMaskLabels());
        check(afterStroke.count(37) && window.maskLabelCounts() == afterStroke, "a stroke updates the counts");
        check(pulp && pulp->text(4) == QLocale().toString(qlonglong(afterStroke.at(37))), "and its row");
        window.undoMaskEdit();
        check(window.maskLabelCounts() == histogram(window.activeMaskLabels()) && !window.maskLabelCounts().count(37),
              "undo takes them back");

        // The ignore label is drawn hatched: on a run of it, some voxels are tinted and some not.
        window.setActiveLabel(38);
        emit axial->mousePressed(14, 20, Qt::LeftButton);
        for (int x = 15; x <= 20; ++x)
            emit axial->mouseMoved(x, 20, Qt::LeftButton);
        emit axial->mouseReleased(20, 20, Qt::LeftButton);
        auto *show = window.findChild<QPushButton *>("maskOffButton");
        if (show)
            show->click();
        const QImage slice = axial->image();
        int tinted = 0;
        int plain = 0;
        for (int x = 14; x <= 20; ++x)
        {
            const QColor pixel = slice.pixelColor(x, 20);
            const bool grey = pixel.red() == pixel.green() && pixel.green() == pixel.blue();
            (grey ? plain : tinted) += 1;
        }
        check(slice.width() == int(kDim) && tinted > 0 && plain > 0, "the ignore label is drawn hatched");
        window.undoMaskEdit();

        const int z = slider->value();
        check(window.pickActiveLabelAt(2, 2, z) && window.activeLabel() == 1, "Ctrl+click picks the label under a voxel");
        check(!window.pickActiveLabelAt(18, 18, z) && window.activeLabel() == 1, "and nothing on background");
    }

    {
        ManualSeedSelector next("");
        check(next.labelTable().nameFor(2) == "Maxilla", "the names come back in the next session");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
