// Drives the window's image loading. With background loading on, selecting a row
// reads the file on a worker thread and the window applies it when the result
// arrives; a row selected during a read is loaded once that read completes; a
// failed read leaves the previous image on screen and says why; a window deleted
// during a read goes away cleanly. With background loading off, the default, a
// programmatic selection has loaded on return, which the command line and the
// other window tests rely on.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QSlider>
#include <QTemporaryDir>

#include <itkImage.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIteratorWithIndex.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-66s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

using VolumeType = itk::Image<int16_t, 3>;

// An 8 x 6 x depth volume whose voxel value is x + 100 * z.
bool writeVolume(const QString &path, unsigned int depth)
{
    auto image = VolumeType::New();
    VolumeType::SizeType size;
    size[0] = 8;
    size[1] = 6;
    size[2] = depth;
    VolumeType::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<VolumeType> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
        it.Set(static_cast<int16_t>(it.GetIndex()[0] + 100 * it.GetIndex()[2]));
    try
    {
        auto writer = itk::ImageFileWriter<VolumeType>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->Update();
    }
    catch (const std::exception &e)
    {
        std::printf("failed to write %s: %s\n", qPrintable(path), e.what());
        return false;
    }
    return true;
}

int rowForPath(QListWidget *list, const QString &path)
{
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole).toString() == wanted)
            return row;
    return -1;
}

std::string key(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toStdString();
}

void settle()
{
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid())
    {
        std::printf("no temporary directory\n");
        return 1;
    }
    const QString tall = dir.filePath("tall.nii.gz");
    const QString flat = dir.filePath("flat.nii.gz");
    const QString bad = dir.filePath("bad.nii");
    bool wrote = writeVolume(tall, 12) && writeVolume(flat, 3);
    {
        std::ofstream garbage(bad.toStdString(), std::ios::binary);
        garbage << "not a NIfTI header";
        wrote = wrote && garbage.good();
    }
    check(wrote, "fixtures written");
    if (!wrote)
        return 1;

    // --- the default: inline ------------------------------------------------
    {
        ManualSeedSelector inline_("");
        check(!inline_.backgroundImageLoading(), "background loading is off by default");
        inline_.addImagesFromPaths({tall});
        check(inline_.hasImage() && !inline_.imageLoadPending() && inline_.getImagePath() == key(tall),
              "with it off, a programmatic selection has loaded on return");
    }

    // --- background loading ---------------------------------------------------
    ManualSeedSelector window("");
    window.setBackgroundImageLoading(true);
    QListWidget *list = window.findChild<QListWidget *>("imageList");
    QLabel *status = window.findChild<QLabel *>("statusLabel");
    QSlider *axial = window.findChild<QSlider *>("axialSlider");
    OrthogonalView *view = window.findChild<OrthogonalView *>("axialView");
    QProgressBar *busy = window.findChild<QProgressBar *>("imageLoadProgressBar");
    check(list && status && axial && view && busy, "image list, status label, slider, view and busy bar found");
    if (!list || !status || !axial || !view || !busy)
        return 1;

    window.addImagesFromPaths({tall, flat, bad});
    check(window.imageLoadPending() || window.hasImage(), "selecting a row starts a read");
    check(!window.imageLoadPending() || busy->format().startsWith("Loading tall.nii.gz"),
          "the busy bar says what is being read");
    window.waitForImageLoad();
    check(!busy->isVisibleTo(&window), "the busy bar goes once the read completes");
    check(window.hasImage() && window.getImagePath() == key(tall), "tall.nii.gz is on screen once the read completes");
    check(status->text().startsWith("Loaded: "), "the status bar reports the load");
    check(view->image().width() == 8 && view->image().height() == 6, "the axial view shows its slice");
    check(axial->maximum() == 11 && axial->value() == 5, "the axial slider is ranged for 12 slices, at the middle");
    axial->setValue(11);

    // Three selections in a row: the last one wins, whatever the worker was doing.
    list->setCurrentRow(rowForPath(list, flat));
    list->setCurrentRow(rowForPath(list, tall));
    list->setCurrentRow(rowForPath(list, flat));
    window.waitForImageLoad();
    check(window.getImagePath() == key(flat), "the row selected last is the one loaded");
    check(axial->maximum() == 2 && axial->value() == 1, "the sliders are ranged for the new volume");
    check(view->image().width() == 8 && view->image().height() == 6, "the axial view shows the new volume");

    // A failed read changes nothing but the status bar.
    list->setCurrentRow(rowForPath(list, bad));
    window.waitForImageLoad();
    check(window.hasImage() && window.getImagePath() == key(flat), "a failed read leaves the previous image");
    check(status->text().startsWith("Could not read bad.nii: ") && !status->text().endsWith(": "),
          "and the status bar says why");

    // Slice positions are kept per image.
    list->setCurrentRow(rowForPath(list, tall));
    window.waitForImageLoad();
    check(window.getImagePath() == key(tall) && axial->value() == 11, "a reselected image comes back at its slice");

    // --- deleting the window during a read ------------------------------------
    {
        auto *closing = new ManualSeedSelector("");
        closing->setBackgroundImageLoading(true);
        closing->addImagesFromPaths({tall});
        const bool pending = closing->imageLoadPending();
        delete closing;
        settle();
        check(true, pending ? "a window deleted during a read goes away cleanly"
                            : "a window deleted after a quick read goes away cleanly");
    }

    std::printf("%s\n", failures == 0 ? "all checks passed" : "some checks FAILED");
    return failures == 0 ? 0 : 1;
}
