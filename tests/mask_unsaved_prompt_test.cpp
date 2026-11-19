// Unsaved mask edits are never lost without a question. Each action that would throw
// edits away asks first, Cancel leaves everything as it was, Discard goes on, and Save
// writes the edits before going on. An action that loses nothing asks nothing: editing
// another mask while the edited one is drawn keeps its edits, and its undo history, in
// its layer.
#include "ManualSeedSelector.h"
#include "MaskListDelegate.h"
#include "OrthogonalView.h"
#include "UiUtils.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>

#include <cstdio>
#include <vector>

#include <itkImage.h>
#include <itkImageFileReader.h>
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

int voxelOnDisk(const QString &path, unsigned x, unsigned y, unsigned z)
{
    try
    {
        auto reader = itk::ImageFileReader<VolumeType>::New();
        reader->SetFileName(path.toStdString());
        reader->Update();
        VolumeType::IndexType voxel;
        voxel[0] = static_cast<VolumeType::IndexValueType>(x);
        voxel[1] = static_cast<VolumeType::IndexValueType>(y);
        voxel[2] = static_cast<VolumeType::IndexValueType>(z);
        return reader->GetOutput()->GetPixel(voxel);
    }
    catch (...)
    {
        return -1;
    }
}

int rowForFile(QListWidget *list, const QString &fileName)
{
    for (int row = 0; row < list->count(); ++row)
        if (QFileInfo(list->item(row)->data(UiUtils::kPathRole).toString()).fileName() == fileName)
            return row;
    return -1;
}

void clickAt(QListWidget *list, const QPointF &pos)
{
    const QPointF global = list->viewport()->mapToGlobal(pos);
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &press);
    QApplication::sendEvent(list->viewport(), &release);
}

// Selects the mask for editing, and nothing else.
void clickName(QListWidget *list, const QString &fileName)
{
    const int row = rowForFile(list, fileName);
    if (row < 0)
        return;
    const QRect itemRect = list->visualItemRect(list->item(row));
    clickAt(list, QPointF(itemRect.left() + MaskListDelegate::kTextOffset + 2, itemRect.center().y()));
}

void clickEye(QListWidget *list, const QString &fileName)
{
    const int row = rowForFile(list, fileName);
    if (row >= 0)
        clickAt(list, QRectF(MaskListDelegate::eyeRect(list->visualItemRect(list->item(row)))).center());
}

void stroke(OrthogonalView *view, int x0, int x1, int y)
{
    emit view->mousePressed(x0, y, Qt::LeftButton);
    for (int x = x0 + 1; x <= x1; ++x)
        emit view->mouseMoved(x, y, Qt::LeftButton);
    emit view->mouseReleased(x1, y, Qt::LeftButton);
}

struct Prompts
{
    ManualSeedSelector::UnsavedEditsChoice next = ManualSeedSelector::UnsavedEditsChoice::Cancel;
    std::vector<QStringList> asked;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir workDir;
    if (!workDir.isValid())
        return 1;
    const QDir dir(workDir.path());
    const QString ctA = dir.filePath("case_0000.nii.gz");
    const QString ctB = dir.filePath("other_0000.nii.gz");
    const QString maskX = dir.filePath("case_x.nii.gz");
    const QString maskY = dir.filePath("case_y.nii.gz");
    const bool wrote =
        writeVolume(ctA, [](unsigned x, unsigned y, unsigned) { return int(x * 10 + y); }) &&
        writeVolume(ctB, [](unsigned x, unsigned, unsigned) { return int(x * 5); }) &&
        writeVolume(maskX, [](unsigned x, unsigned y, unsigned) { return (x < kDim / 2 && y < kDim / 2) ? 1 : 0; }) &&
        writeVolume(maskY, [](unsigned x, unsigned y, unsigned) { return (x >= kDim / 2 && y >= kDim / 2) ? 1 : 0; });
    check(wrote, "test data written");
    if (!wrote)
        return 1;

    ManualSeedSelector window("");
    Prompts prompts;
    window.setUnsavedEditsPrompt([&prompts](const QString &, const QStringList &masks)
                                 {
        prompts.asked.push_back(masks);
        return prompts.next; });
    window.addImagesFromPaths({ctA, ctB});
    window.refreshAssociatedFilesForCurrentImage(true);

    auto *images = window.findChild<QListWidget *>("imageList");
    auto *masks = window.findChild<QListWidget *>("maskList");
    auto *axial = window.findChild<OrthogonalView *>("axialView");
    auto *axialSlider = window.findChild<QSlider *>("axialSlider");
    auto *draw = window.findChild<QPushButton *>("maskDrawButton");
    auto *label = window.findChild<QSpinBox *>("labelSelector");
    const bool found = images && masks && axial && axialSlider && draw && label;
    check(found, "controls found");
    if (!found)
        return 1;
    masks->resize(240, 160);
    check(QFileInfo(QString::fromStdString(window.getImagePath())) == QFileInfo(ctA), "first image open");
    check(rowForFile(masks, "case_x.nii.gz") >= 0 && rowForFile(masks, "case_y.nii.gz") >= 0, "both masks listed");

    label->setValue(2);
    draw->click(); // before any mask is selected, so selecting one leaves its eye closed
    const unsigned z = static_cast<unsigned>(axialSlider->value());
    const std::size_t painted = std::size_t(8) + std::size_t(6) * kDim + std::size_t(z) * kDim * kDim;
    auto paintedValue = [&]() -> int
    {
        const std::vector<int> &labels = window.activeMaskLabels();
        return painted < labels.size() ? labels[painted] : -1;
    };

    // A hidden mask is dropped when another one is edited, so that asks.
    clickName(masks, "case_x.nii.gz");
    stroke(axial, 6, 10, 6);
    check(paintedValue() == 2 && window.hasUnsavedMaskEdits(), "hidden case_x painted: unsaved edits");
    prompts.next = ManualSeedSelector::UnsavedEditsChoice::Cancel;
    clickName(masks, "case_y.nii.gz");
    check(prompts.asked.size() == 1 && prompts.asked.back() == QStringList{"case_x.nii.gz"},
          "editing case_y asks about case_x");
    check(QFileInfo(window.activeMaskPath()).fileName() == "case_x.nii.gz" && paintedValue() == 2,
          "Cancel: case_x still edited, its edits intact");
    prompts.next = ManualSeedSelector::UnsavedEditsChoice::Discard;
    clickName(masks, "case_y.nii.gz");
    check(QFileInfo(window.activeMaskPath()).fileName() == "case_y.nii.gz" && !window.hasUnsavedMaskEdits(),
          "Discard: case_y edited, nothing unsaved");

    // A drawn mask keeps its edits and its history in its layer; nothing to ask.
    clickName(masks, "case_x.nii.gz");
    clickEye(masks, "case_x.nii.gz");
    check(paintedValue() == 1, "case_x read again from its file");
    stroke(axial, 6, 10, 6);
    const std::size_t askedBefore = prompts.asked.size();
    clickName(masks, "case_y.nii.gz");
    check(prompts.asked.size() == askedBefore, "editing case_y while case_x is drawn asks nothing");
    check(window.hasUnsavedMaskEdits(), "case_x's edits are still unsaved, in its layer");
    clickName(masks, "case_x.nii.gz");
    check(paintedValue() == 2, "case_x comes back with its edits");
    check(window.undoMaskEdit() && paintedValue() == 1, "and with its undo history");
    window.redoMaskEdit();

    // Reading case_x from its file again would replace the edits in its layer.
    clickName(masks, "case_y.nii.gz");
    prompts.next = ManualSeedSelector::UnsavedEditsChoice::Cancel;
    check(!window.applyMaskFromPath(maskX.toStdString()), "Cancel: reloading case_x does not happen");
    check(prompts.asked.size() == askedBefore + 1 && prompts.asked.back() == QStringList{"case_x.nii.gz"},
          "reloading case_x asks about case_x");
    check(window.hasUnsavedMaskEdits(), "case_x's edits survive the refused reload");

    // Opening another image takes every mask with it; Save writes case_x first.
    prompts.next = ManualSeedSelector::UnsavedEditsChoice::Save;
    images->setCurrentRow(1);
    check(QFileInfo(QString::fromStdString(window.getImagePath())) == QFileInfo(ctB), "Save: the other image opens");
    check(voxelOnDisk(maskX, 8, 6, z) == 2, "and case_x was written with its edits");

    // A mask painted from scratch has no file; Cancel keeps the image and the paint.
    stroke(axial, 6, 10, 6);
    check(window.activeMaskPath().isEmpty() && paintedValue() == 2, "a scratch mask painted on the other image");
    prompts.next = ManualSeedSelector::UnsavedEditsChoice::Cancel;
    images->setCurrentRow(0);
    check(QFileInfo(QString::fromStdString(window.getImagePath())) == QFileInfo(ctB) && images->currentRow() == 1,
          "Cancel: the image and the list row stay");
    check(paintedValue() == 2, "and the scratch mask is intact");

    // Closing asks too; Cancel keeps the window open.
    const std::size_t askedBeforeClose = prompts.asked.size();
    QCloseEvent close;
    QApplication::sendEvent(&window, &close);
    check(!close.isAccepted() && prompts.asked.size() == askedBeforeClose + 1, "Cancel: closing is refused");
    check(paintedValue() == 2, "and the edits are intact");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
