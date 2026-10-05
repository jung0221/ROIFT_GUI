// Drives the real window with raster images, the input the plane mode exists for.
//
// oiftrelax and the Python helpers read files, not the volume in memory, and none
// of them reads PNG or TIFF; nativeImagePath() must hand them a NIfTI holding the
// voxels of the image now selected, in a directory that goes away with the window.
// A one-slice image has no sagittal, coronal or 3D picture, so it fills the view area.
#include "ManualSeedSelector.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryDir>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIteratorWithIndex.h>
#include <itkNiftiImageIO.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

using ValueAt = std::function<float(unsigned int, unsigned int, unsigned int)>;

// Write an image of the given size, 8-bit unless told otherwise; 2D when sizeZ is 1.
template <unsigned int Dimension, typename PixelT = unsigned char>
bool writeImage(const QString &path, const unsigned int (&size)[3], const ValueAt &valueAt)
{
    using ImageT = itk::Image<PixelT, Dimension>;
    auto image = ImageT::New();
    typename ImageT::SizeType imageSize;
    for (unsigned int i = 0; i < Dimension; ++i)
        imageSize[i] = size[i];
    typename ImageT::RegionType region;
    region.SetSize(imageSize);
    image->SetRegions(region);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<ImageT> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
    {
        const typename ImageT::IndexType i = it.GetIndex();
        const unsigned int z = Dimension > 2 ? static_cast<unsigned int>(i[Dimension - 1]) : 0;
        it.Set(static_cast<PixelT>(valueAt(i[0], i[1], z)));
    }
    try
    {
        auto writer = itk::ImageFileWriter<ImageT>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->Update();
    }
    catch (const itk::ExceptionObject &e)
    {
        std::printf("could not write %s: %s\n", qPrintable(path), e.GetDescription());
        return false;
    }
    return true;
}

// True when path is a NIfTI of this size whose voxels are valueAt.
bool exportHolds(const std::string &path, const unsigned int (&size)[3], const ValueAt &valueAt)
{
    using FloatVolume = itk::Image<float, 3>;
    FloatVolume::Pointer volume;
    try
    {
        auto reader = itk::ImageFileReader<FloatVolume>::New();
        reader->SetImageIO(itk::NiftiImageIO::New());
        reader->SetFileName(path);
        reader->Update();
        volume = reader->GetOutput();
    }
    catch (const itk::ExceptionObject &e)
    {
        std::printf("could not read %s as NIfTI: %s\n", path.c_str(), e.GetDescription());
        return false;
    }
    const FloatVolume::SizeType actual = volume->GetLargestPossibleRegion().GetSize();
    if (actual[0] != size[0] || actual[1] != size[1] || actual[2] != size[2])
        return false;
    for (unsigned int z = 0; z < size[2]; ++z)
        for (unsigned int y = 0; y < size[1]; ++y)
            for (unsigned int x = 0; x < size[0]; ++x)
            {
                FloatVolume::IndexType index;
                index[0] = x;
                index[1] = y;
                index[2] = z;
                if (volume->GetPixel(index) != valueAt(x, y, z))
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

// Select the list row of path, then ask for the file the tools would be handed.
std::string selectAndExport(ManualSeedSelector &window, QListWidget *list, const QString &path)
{
    const int row = rowForPath(list, path);
    if (row < 0)
        return std::string();
    list->setCurrentRow(row);
    return window.nativeImagePath();
}

// Let posted layout requests run so widget geometry reflects the current layout.
void settle()
{
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
}

double widthShare(const QWidget *panel, const QWidget *container)
{
    return container->width() > 0 ? double(panel->width()) / container->width() : 0.0;
}

double heightShare(const QWidget *panel, const QWidget *container)
{
    return container->height() > 0 ? double(panel->height()) / container->height() : 0.0;
}

// The axial panel's title: its only direct QLabel child.
QString axialTitle(QWidget *axialPanel)
{
    const QList<QLabel *> labels = axialPanel->findChildren<QLabel *>(Qt::FindDirectChildrenOnly);
    return labels.size() == 1 ? labels.front()->text() : QString();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTemporaryDir workDir;
    if (!workDir.isValid())
    {
        std::printf("could not create a temporary directory\n");
        return 1;
    }
    const QDir dir(workDir.path());
    const unsigned int stackSize[3] = {8, 6, 3};
    const unsigned int planeSize[3] = {40, 30, 1};
    const ValueAt stackValue = [](unsigned int x, unsigned int y, unsigned int z)
    { return static_cast<float>(x + 8 * y + 48 * z); };
    const ValueAt planeValue = [](unsigned int x, unsigned int y, unsigned int) { return static_cast<float>(x + y); };
    const ValueAt firstValue = [](unsigned int x, unsigned int, unsigned int) { return static_cast<float>(x); };
    const ValueAt secondValue = [](unsigned int x, unsigned int y, unsigned int) { return static_cast<float>(200 - x - y); };

    const QString tifPath = dir.filePath("stack.tif");
    const QString pngPath = dir.filePath("plane.png");
    dir.mkdir("a");
    dir.mkdir("b");
    const QString firstPath = dir.filePath("a/image.png");
    const QString secondPath = dir.filePath("b/image.png");
    const QString badPath = dir.filePath("bad.png");
    QFile bad(badPath);
    const bool wroteBad = bad.open(QIODevice::WriteOnly) && bad.write("not a picture") > 0;
    bad.close();
    const bool wrote = writeImage<3>(tifPath, stackSize, stackValue) && writeImage<2>(pngPath, planeSize, planeValue) &&
                       writeImage<2>(firstPath, planeSize, firstValue) &&
                       writeImage<2>(secondPath, planeSize, secondValue) && wroteBad;
    check(wrote, "fixtures written");
    if (!wrote)
        return 1;

    auto window = std::make_unique<ManualSeedSelector>("");
    window->addImagesFromPaths({tifPath, pngPath, firstPath, secondPath, badPath});
    check(window->hasImage(), "stack.tif opens as the image");
    QListWidget *images = window->findChild<QListWidget *>("imageList");
    QLabel *status = window->findChild<QLabel *>("statusLabel");
    check(images != nullptr && images->count() == 5 && status != nullptr, "image list holds the five files");
    if (!images || images->count() != 5 || !status)
        return 1;

    const std::string native = window->nativeImagePath();
    const QString nativePath = QString::fromStdString(native);
    check(QFileInfo(nativePath).absoluteFilePath() != QFileInfo(tifPath).absoluteFilePath(),
          "stack.tif: nativeImagePath is not the .tif");
    check(nativePath.endsWith(".nii.gz") && QFileInfo::exists(nativePath), "stack.tif: export is an existing .nii.gz");
    check(exportHolds(native, stackSize, stackValue), "stack.tif: export is 8 x 6 x 3 with the .tif voxels");

    // <temp>/<window directory>/<n>/roift_src_<base>.nii.gz: a fresh <n> per export.
    const QString exportDir = QFileInfo(nativePath).absolutePath();
    const QString windowDir = QFileInfo(exportDir).absolutePath();
    const QString tempRoot = QDir::cleanPath(QDir::temp().absolutePath());
    check(QDir::cleanPath(windowDir) != tempRoot && QDir::cleanPath(QFileInfo(windowDir).absolutePath()) == tempRoot,
          "export lives in a per-window directory under temp, not in temp");
    check(QFileInfo(nativePath).fileName() == "roift_src_stack.nii.gz", "export keeps the name roift_src_<base>.nii.gz");

    const std::string planeExport = selectAndExport(*window, images, pngPath);
    check(QString::fromStdString(planeExport).endsWith(".nii.gz") && exportHolds(planeExport, planeSize, planeValue),
          "plane.png: export is 40 x 30 x 1 with the .png values");

    // A queued run may still be reading an earlier export, so none is ever overwritten.
    const std::string first = selectAndExport(*window, images, firstPath);
    check(exportHolds(first, planeSize, firstValue), "a/image.png: export holds a/image.png");
    const std::string second = selectAndExport(*window, images, secondPath);
    check(exportHolds(second, planeSize, secondValue), "b/image.png (same name): export holds b/image.png");
    check(first != second, "a/image.png and b/image.png export to different paths");
    check(exportHolds(first, planeSize, firstValue), "the export of a/image.png survives exporting b/image.png");
    const std::string firstAgain = selectAndExport(*window, images, firstPath);
    check(exportHolds(firstAgain, planeSize, firstValue), "a/image.png reselected: export holds a/image.png again");

    const ValueAt changedValue = [](unsigned int, unsigned int y, unsigned int) { return static_cast<float>(7 * y); };
    const bool rewrote = writeImage<2>(firstPath, planeSize, changedValue);
    images->setCurrentRow(rowForPath(images, pngPath));
    const std::string changed = selectAndExport(*window, images, firstPath);
    check(rewrote && exportHolds(changed, planeSize, changedValue),
          "a/image.png rewritten: reselected export holds the new file");
    check(changed != firstAgain && exportHolds(first, planeSize, firstValue) &&
              exportHolds(firstAgain, planeSize, firstValue),
          "re-exporting a/image.png leaves its earlier exports unchanged");

    images->setCurrentRow(rowForPath(images, badPath));
    std::printf("  status: %s\n", qPrintable(status->text()));
    check(status->text().startsWith("Could not read bad.png: ") && !status->text().endsWith(": "),
          "an unreadable file shows its reason in the status bar");
    check(!status->text().contains("Tried to create"), "the reason is ITK's first line, not its list of readers");

    window.reset();
    check(!QFileInfo::exists(nativePath) && !QFileInfo::exists(windowDir),
          "closing the window removes its export directory");

    // Panel geometry needs a shown window; the window above is never shown.
    {
        const QString grayPath = dir.filePath("gray.png");
        const QString volumePath = dir.filePath("volume.nii.gz");
        const unsigned int volumeSize[3] = {12, 10, 8};
        const bool wroteLayout = writeImage<2>(grayPath, planeSize, planeValue) &&
                                 writeImage<3, std::int16_t>(volumePath, volumeSize, stackValue);
        check(wroteLayout, "layout fixtures written");

        ManualSeedSelector shown("");
        shown.resize(1200, 900);
        shown.show();
        settle();

        QListWidget *list = shown.findChild<QListWidget *>("imageList");
        QWidget *container = shown.findChild<QWidget *>("viewContainer");
        QWidget *axialPanel = shown.findChild<QWidget *>("axialPanel");
        QWidget *sagittalPanel = shown.findChild<QWidget *>("sagittalPanel");
        QWidget *coronalPanel = shown.findChild<QWidget *>("coronalPanel");
        QWidget *renderPanel = shown.findChild<QWidget *>("renderPanel");
        OrthogonalView *axialView = shown.findChild<OrthogonalView *>("axialView");
        const bool found = wroteLayout && list && container && axialPanel && sagittalPanel && coronalPanel &&
                           renderPanel && axialView;
        check(found, "view container, four panels and axial view are named");
        if (!found)
            return 1;

        shown.addImagesFromPaths({grayPath, volumePath});
        list->setCurrentRow(rowForPath(list, grayPath));
        settle();
        std::printf("  gray.png: axial panel %dx%d in container %dx%d, title \"%s\"\n", axialPanel->width(),
                    axialPanel->height(), container->width(), container->height(), qPrintable(axialTitle(axialPanel)));
        check(shown.hasImage(), "gray.png opens as the image");
        check(sagittalPanel->isHidden() && coronalPanel->isHidden() && renderPanel->isHidden(),
              "gray.png: sagittal, coronal and 3D panels are hidden");
        check(!axialPanel->isHidden() && axialPanel->isVisible(), "gray.png: axial panel is shown");
        check(widthShare(axialPanel, container) >= 0.9 && heightShare(axialPanel, container) >= 0.9,
              "gray.png: axial panel fills the view area");
        check(axialView->image().size() == QSize(40, 30), "gray.png: axial view shows the 40 x 30 image");
        check(axialTitle(axialPanel).startsWith("Image"), "gray.png: axial panel is titled Image");

        list->setCurrentRow(rowForPath(list, volumePath));
        settle();
        std::printf("  volume: axial panel %dx%d in container %dx%d, title \"%s\"\n", axialPanel->width(),
                    axialPanel->height(), container->width(), container->height(), qPrintable(axialTitle(axialPanel)));
        check(axialPanel->isVisible() && sagittalPanel->isVisible() && coronalPanel->isVisible() &&
                  renderPanel->isVisible(),
              "12 x 10 x 8 volume: all four panels are shown again");
        const double share = widthShare(axialPanel, container);
        check(share >= 0.35 && share <= 0.65, "12 x 10 x 8 volume: axial panel takes half the width");
        check(!axialTitle(axialPanel).startsWith("Image"), "12 x 10 x 8 volume: axial panel is not titled Image");
    }

    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
