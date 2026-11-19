// Crash copies of the edited mask. A window that ends without saving (a crash, as far as the
// files can tell) leaves its copy; the next window to open that image offers it back, and a
// restore puts the edits back as unsaved, on top of the file. A save, an undo back to the
// file followed by a close, and a Discard each remove the copy; Later keeps it.
#include "ManualSeedSelector.h"
#include "MaskAutosave.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSettings>
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

void stroke(OrthogonalView *view, int x0, int x1, int y)
{
    emit view->mousePressed(x0, y, Qt::LeftButton);
    for (int x = x0 + 1; x <= x1; ++x)
        emit view->mouseMoved(x, y, Qt::LeftButton);
    emit view->mouseReleased(x1, y, Qt::LeftButton);
}

int copiesOnDisk()
{
    return QDir(mask_autosave::directory()).entryList({"*.json"}, QDir::Files).size();
}

// A window on one image, with label 2 and the brush picked up; the prompt answers `answer`
// and counts the questions.
struct Session
{
    ManualSeedSelector window{""};
    int asked = 0;
    OrthogonalView *axial = nullptr;
    unsigned z = 0;

    Session(const QString &image, ManualSeedSelector::AutosaveChoice answer)
    {
        window.setAutosavePrompt([this, answer](const QString &, const QDateTime &)
                                 {
            ++asked;
            return answer; });
        window.addImagesFromPaths({image});
        QCoreApplication::processEvents(); // the offer is queued behind the image load
        axial = window.findChild<OrthogonalView *>("axialView");
        if (auto *label = window.findChild<QSpinBox *>("labelSelector"))
            label->setValue(2);
        if (auto *draw = window.findChild<QPushButton *>("maskDrawButton"))
            draw->click();
        if (auto *slider = window.findChild<QSlider *>("axialSlider"))
            z = static_cast<unsigned>(slider->value());
    }

    int voxel(unsigned x, unsigned y)
    {
        const std::vector<int> &labels = window.activeMaskLabels();
        const std::size_t i = std::size_t(x) + std::size_t(y) * kDim + std::size_t(z) * kDim * kDim;
        return i < labels.size() ? labels[i] : -1;
    }
};

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir workDir;
    if (!workDir.isValid())
        return 1;
    const QDir dir(workDir.path());
    // Copies and window settings both stay in the temporary directory.
    qputenv("ROIFT_AUTOSAVE_DIR", dir.filePath("autosave").toLocal8Bit());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("settings"));
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("roift-test");
    QCoreApplication::setApplicationName("mask_autosave_test");

    const QString ctA = dir.filePath("case_0000.nii.gz");
    const QString ctB = dir.filePath("other_0000.nii.gz");
    const QString maskX = dir.filePath("case_x.nii.gz");
    const bool wrote = writeVolume(ctA, [](unsigned x, unsigned y, unsigned) { return int(x * 10 + y); }) &&
                       writeVolume(ctB, [](unsigned x, unsigned, unsigned) { return int(x * 5); }) &&
                       writeVolume(maskX, [](unsigned x, unsigned y, unsigned) { return (x < 12 && y < 12) ? 1 : 0; });
    check(wrote, "test data written");
    if (!wrote)
        return 1;

    // A mask with a file: edited, copied, then the window ends unsaved.
    {
        Session crashed(ctA, ManualSeedSelector::AutosaveChoice::Later);
        check(crashed.window.applyMaskFromPath(maskX.toStdString()), "case_x edited");
        check(!crashed.window.autosaveMaskNow(), "nothing unsaved: no copy");
        stroke(crashed.axial, 6, 10, 6);
        check(crashed.voxel(8, 6) == 2, "case_x painted");
        check(crashed.window.autosaveMaskNow() && copiesOnDisk() == 1, "the copy is written");
        stroke(crashed.axial, 6, 10, 8);
        check(crashed.window.autosaveMaskNow() && copiesOnDisk() == 1, "a later copy replaces it");
    }
    check(copiesOnDisk() == 1, "a window that ends unsaved leaves its copy");

    {
        Session later(ctA, ManualSeedSelector::AutosaveChoice::Later);
        check(later.asked == 1, "opening the image offers the copy");
        check(later.window.activeMaskPath().isEmpty() && copiesOnDisk() == 1, "Later: nothing restored, copy kept");
    }
    {
        Session restored(ctA, ManualSeedSelector::AutosaveChoice::Restore);
        check(restored.asked == 1, "offered again");
        check(QFileInfo(restored.window.activeMaskPath()).fileName() == "case_x.nii.gz", "Restore: case_x is edited");
        check(restored.voxel(8, 6) == 2 && restored.voxel(8, 8) == 2, "with both strokes back");
        check(restored.window.maskEditHistory().isDirty(), "as unsaved edits");
        check(restored.window.undoMaskEdit() && restored.voxel(8, 6) == 1, "undo shows what the file holds");
        restored.window.redoMaskEdit();
        QString error;
        check(restored.window.saveActiveMaskTo(maskX.toStdString(), &error), "case_x saved");
        check(copiesOnDisk() == 0, "a save removes the copy");
    }

    // A mask painted without a file: Discard removes the copy.
    {
        Session scratch(ctB, ManualSeedSelector::AutosaveChoice::Later);
        stroke(scratch.axial, 6, 10, 6);
        check(scratch.window.activeMaskPath().isEmpty() && scratch.window.autosaveMaskNow(),
              "a scratch mask is copied too");
    }
    {
        Session restored(ctB, ManualSeedSelector::AutosaveChoice::Restore);
        check(restored.asked == 1 && restored.voxel(8, 6) == 2 && restored.window.activeMaskPath().isEmpty(),
              "Restore: the scratch mask is back, still without a file");
        check(restored.window.maskEditHistory().isDirty(), "and unsaved");
    }
    {
        Session discarded(ctB, ManualSeedSelector::AutosaveChoice::Discard);
        check(discarded.asked == 1 && copiesOnDisk() == 0, "Discard removes the copy");
    }

    // The timer's own path: the copy is written on a worker thread while the window runs on.
    {
        Session timed(ctB, ManualSeedSelector::AutosaveChoice::Later);
        stroke(timed.axial, 6, 10, 6);
        timed.window.setMaskAutosaveInterval(20);
        QElapsedTimer waited;
        waited.start();
        while (copiesOnDisk() == 0 && waited.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        check(copiesOnDisk() == 1, "the timer writes the copy in the background");
        timed.window.undoMaskEdit();
        waited.restart();
        while (copiesOnDisk() == 1 && waited.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        check(copiesOnDisk() == 0, "and removes its own copy once undo has made the mask clean");
        timed.window.setMaskAutosaveInterval(0);
    }

    // Undone back to the file, then closed: the copy this window wrote is stale and goes.
    {
        Session undone(ctA, ManualSeedSelector::AutosaveChoice::Later);
        undone.window.applyMaskFromPath(maskX.toStdString());
        stroke(undone.axial, 6, 10, 10);
        undone.window.autosaveMaskNow();
        undone.window.undoMaskEdit();
        check(copiesOnDisk() == 1 && !undone.window.maskEditHistory().isDirty(), "copied, then undone");
        QCloseEvent close;
        QApplication::sendEvent(&undone.window, &close);
        check(close.isAccepted() && copiesOnDisk() == 0, "an accepted close removes this window's copies");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
