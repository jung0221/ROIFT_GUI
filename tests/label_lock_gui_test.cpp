// Locks and paint targets: no tool changes a voxel of a locked label, a locked label is not
// painted or erased, and a stroke paints only over what "Over:" allows. The slice paste is
// pinned in plane_mode_gui.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QSettings>
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

// 1 in the left half, 2 in the top right quarter, background in the bottom right quarter.
int maskAt(unsigned x, unsigned y, unsigned)
{
    if (x < kDim / 2)
        return 1;
    return y < kDim / 2 ? 2 : 0;
}

// A stroke along row y from x0 to x1.
void stroke(OrthogonalView *view, int x0, int x1, int y)
{
    emit view->mousePressed(x0, y, Qt::LeftButton);
    for (int x = x0 + 1; x <= x1; ++x)
        emit view->mouseMoved(x, y, Qt::LeftButton);
    emit view->mouseReleased(x1, y, Qt::LeftButton);
}

QTreeWidgetItem *rowFor(QTreeWidget *tree, int label)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(2, Qt::UserRole).toInt() == label)
            return tree->topLevelItem(i);
    return nullptr;
}

// True when every voxel holding `label` in before still holds it in after, and no other does.
bool labelUnchanged(const std::vector<int> &before, const std::vector<int> &after, int label)
{
    if (before.size() != after.size())
        return false;
    for (std::size_t i = 0; i < before.size(); ++i)
        if ((before[i] == label) != (after[i] == label))
            return false;
    return true;
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
    QCoreApplication::setApplicationName("label_lock_gui_test");

    const QString ct = dir.filePath("lock_0000.nii.gz");
    const QString mask = dir.filePath("lock_mask.nii.gz");
    check(writeVolume(ct, [](unsigned x, unsigned y, unsigned) { return int(x * 10 + y); }) && writeVolume(mask, maskAt),
          "test data written");

    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    check(window.applyMaskFromPath(mask.toStdString()), "the mask is edited");
    auto *tree = window.findChild<QTreeWidget *>("labelTable");
    auto *axial = window.findChild<OrthogonalView *>("axialView");
    auto *draw = window.findChild<QPushButton *>("maskDrawButton");
    auto *erase = window.findChild<QPushButton *>("maskEraseButton");
    auto *clear = window.findChild<QPushButton *>("maskClearButton");
    const bool found = tree && axial && draw && erase && clear;
    check(found, "controls found");
    if (!found)
        return 1;
    window.setBrushRadiusMm(1.0); // one voxel at the test image's 1 mm spacing
    const MaskEditHistory &history = window.maskEditHistory();

    // Lock label 1 from its row.
    QTreeWidgetItem *one = rowFor(tree, 1);
    check(one != nullptr, "label 1 has a row");
    if (!one)
        return 1;
    one->setCheckState(1, Qt::Checked);
    check(window.labelLocked(1), "its Lock box locks it");

    const std::vector<int> start = window.activeMaskLabels();
    window.setActiveLabel(2);
    draw->click();
    stroke(axial, 6, 18, 16); // across label 1 and background
    std::vector<int> now = window.activeMaskLabels();
    check(labelUnchanged(start, now, 1), "a stroke of label 2 leaves every voxel of locked label 1");
    check(now != start, "and still paints the background it crosses");

    const std::size_t steps = history.undoCount();
    window.setActiveLabel(1);
    stroke(axial, 14, 18, 18);
    check(history.undoCount() == steps && window.activeMaskLabels() == now, "painting with a locked label is refused");
    erase->click();
    stroke(axial, 2, 8, 4);
    check(history.undoCount() == steps && window.activeMaskLabels() == now, "and so is erasing it");

    clear->click();
    const std::vector<int> cleared = window.activeMaskLabels();
    bool onlyOneLeft = labelUnchanged(now, cleared, 1);
    for (int v : cleared)
        onlyOneLeft = onlyOneLeft && (v == 0 || v == 1);
    check(onlyOneLeft, "Clear empties everything but the locked label");
    window.undoMaskEdit();
    check(window.activeMaskLabels() == now, "undo brings the rest back");

    // Paint targets, with nothing locked.
    tree->findItems("1", Qt::MatchExactly, 2).value(0)->setCheckState(1, Qt::Unchecked);
    check(!window.labelLocked(1), "unlocked again");
    window.setPaintOver(ManualSeedSelector::PaintOver::Background);
    window.setActiveLabel(37);
    draw->click();
    const std::vector<int> beforeBackground = window.activeMaskLabels();
    stroke(axial, 6, 18, 20); // across label 1 and background
    const std::vector<int> afterBackground = window.activeMaskLabels();
    bool onlyBackground = afterBackground != beforeBackground;
    for (std::size_t i = 0; i < afterBackground.size(); ++i)
        if (afterBackground[i] != beforeBackground[i])
            onlyBackground = onlyBackground && beforeBackground[i] == 0;
    check(onlyBackground, "Over: Background only paints background voxels only");

    window.setPaintOver(ManualSeedSelector::PaintOver::OneLabel, 2);
    const std::vector<int> beforeOne = window.activeMaskLabels();
    stroke(axial, 6, 18, 6); // across label 1 and label 2
    const std::vector<int> afterOne = window.activeMaskLabels();
    bool onlyTwo = afterOne != beforeOne;
    for (std::size_t i = 0; i < afterOne.size(); ++i)
        if (afterOne[i] != beforeOne[i])
            onlyTwo = onlyTwo && beforeOne[i] == 2;
    check(onlyTwo, "Over: One label 2 paints label 2 voxels only");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
