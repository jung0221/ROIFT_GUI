// Drives the window's mask editing through its own controls and checks that every edit is
// one undo step: a brush stroke from press to release, an erase stroke, and Clear. Also
// pins the dirty flag: an edit makes the mask dirty, a save makes it clean, and undoing
// back to the saved state makes it clean again.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>

#include <cstdio>

#include <itkImage.h>
#include <itkImageFileWriter.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned int kDim = 24;
constexpr unsigned int kDimZ = 6;

bool writeImage(const QString &path)
{
    using ImageType = itk::Image<int32_t, 3>;
    ImageType::Pointer image = ImageType::New();
    ImageType::SizeType size;
    size[0] = kDim;
    size[1] = kDim;
    size[2] = kDimZ;
    ImageType::IndexType start;
    start.Fill(0);
    image->SetRegions(ImageType::RegionType(start, size));
    image->Allocate();
    for (unsigned z = 0; z < kDimZ; ++z)
        for (unsigned y = 0; y < kDim; ++y)
            for (unsigned x = 0; x < kDim; ++x)
            {
                ImageType::IndexType voxel;
                voxel[0] = static_cast<ImageType::IndexValueType>(x);
                voxel[1] = static_cast<ImageType::IndexValueType>(y);
                voxel[2] = static_cast<ImageType::IndexValueType>(z);
                image->SetPixel(voxel, static_cast<int32_t>(x * 10 + y));
            }
    try
    {
        auto writer = itk::ImageFileWriter<ImageType>::New();
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

std::size_t index(unsigned x, unsigned y, unsigned z)
{
    return std::size_t(x) + std::size_t(y) * kDim + std::size_t(z) * kDim * kDim;
}

// A left-button stroke on the axial view, as the view reports it: press, moves, release.
void stroke(OrthogonalView *view, int x0, int x1, int y)
{
    emit view->mousePressed(x0, y, Qt::LeftButton);
    for (int x = x0 + 1; x <= x1; ++x)
        emit view->mouseMoved(x, y, Qt::LeftButton);
    emit view->mouseReleased(x1, y, Qt::LeftButton);
}

std::size_t painted(ManualSeedSelector &window)
{
    std::size_t n = 0;
    for (int v : window.activeMaskLabels())
        n += v != 0;
    return n;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir workDir;
    if (!workDir.isValid())
        return 1;
    const QDir dir(workDir.path());
    const QString ctPath = dir.filePath("edit_0000.nii.gz");
    check(writeImage(ctPath), "test image written");

    ManualSeedSelector window("");
    window.addImagesFromPaths({ctPath});
    check(window.hasImage(), "image loaded");

    auto *axial = window.findChild<OrthogonalView *>("axialView");
    auto *draw = window.findChild<QPushButton *>("maskDrawButton");
    auto *erase = window.findChild<QPushButton *>("maskEraseButton");
    auto *undo = window.findChild<QPushButton *>("maskUndoButton");
    auto *redo = window.findChild<QPushButton *>("maskRedoButton");
    auto *clear = window.findChild<QPushButton *>("maskClearButton");
    check(axial && draw && erase && undo && redo && clear, "controls found");
    if (!(axial && draw && erase && undo && redo && clear))
        return 1;
    check(!undo->isEnabled() && !redo->isEnabled(), "nothing to undo or redo before an edit");

    auto *label = window.findChild<QSpinBox *>("labelSelector");
    check(label != nullptr, "label selector found");
    if (!label)
        return 1;
    label->setValue(1);

    const MaskEditHistory &history = window.maskEditHistory();
    auto *axialSlider = window.findChild<QSlider *>("axialSlider");
    check(axialSlider != nullptr, "axial slider found");
    if (!axialSlider)
        return 1;
    const unsigned z = static_cast<unsigned>(axialSlider->value()); // the slice the strokes land on
    draw->click();
    stroke(axial, 6, 10, 6);
    const std::size_t afterFirst = painted(window);
    check(afterFirst > 0 && window.activeMaskLabels()[index(8, 6, z)] != 0, "a stroke paints");
    check(history.undoCount() == 1 && !history.hasOpenOperation(), "a whole stroke is one closed operation");
    check(history.isDirty() && undo->isEnabled(), "and makes the mask dirty and undoable");

    stroke(axial, 16, 18, 16);
    const std::size_t afterSecond = painted(window);
    check(history.undoCount() == 2 && afterSecond > afterFirst, "a second stroke is a second operation");

    undo->click();
    check(painted(window) == afterFirst && window.activeMaskLabels()[index(17, 16, z)] == 0,
          "undo removes the last stroke only");
    check(redo->isEnabled(), "and enables redo");
    undo->click();
    check(painted(window) == 0, "undoing both strokes clears the mask");
    check(!history.isDirty(), "back at the start, the mask is clean");
    check(!window.undoMaskEdit(), "nothing left to undo");

    redo->click();
    redo->click();
    check(painted(window) == afterSecond, "redo brings both strokes back");

    erase->click();
    stroke(axial, 6, 10, 6);
    check(window.activeMaskLabels()[index(8, 6, z)] == 0, "an erase stroke clears the painted voxels");
    undo->click();
    check(window.activeMaskLabels()[index(8, 6, z)] != 0, "and undoing it restores them");

    clear->click();
    check(painted(window) == 0, "Clear empties the mask");
    undo->click();
    check(painted(window) == afterSecond, "and Clear is undoable");

    QString error;
    const QString maskPath = dir.filePath("edit_mask.nii.gz");
    check(window.saveActiveMaskTo(maskPath.toStdString(), &error), "the mask saves");
    check(!history.isDirty(), "a save makes the mask clean");
    undo->click();
    check(history.isDirty(), "undoing past the save makes it dirty");
    redo->click();
    check(!history.isDirty(), "redoing back to the save makes it clean");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
