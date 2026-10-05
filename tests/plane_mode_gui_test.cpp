// Drives the real window with raster images, the input the plane mode exists for.
//
// oiftrelax and the Python helpers read files, not the volume in memory, and none
// of them reads PNG or TIFF; nativeImagePath() must hand them a NIfTI holding the
// voxels of the image now selected, in a directory that goes away with the window.
#include "ManualSeedSelector.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QTemporaryDir>

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

// Write an 8-bit image of the given size; 2D when sizeZ is 1.
template <unsigned int Dimension>
bool writeImage(const QString &path, const unsigned int (&size)[3], const ValueAt &valueAt)
{
    using ImageT = itk::Image<unsigned char, Dimension>;
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
        it.Set(static_cast<unsigned char>(valueAt(i[0], i[1], z)));
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
    const bool wrote = writeImage<3>(tifPath, stackSize, stackValue) && writeImage<2>(pngPath, planeSize, planeValue) &&
                       writeImage<2>(firstPath, planeSize, firstValue) &&
                       writeImage<2>(secondPath, planeSize, secondValue);
    check(wrote, "fixtures written");
    if (!wrote)
        return 1;

    auto window = std::make_unique<ManualSeedSelector>("");
    window->addImagesFromPaths({tifPath, pngPath, firstPath, secondPath});
    check(window->hasImage(), "stack.tif opens as the image");
    QListWidget *images = window->findChild<QListWidget *>("imageList");
    check(images != nullptr && images->count() == 4, "image list holds the four files");
    if (!images || images->count() != 4)
        return 1;

    const std::string native = window->nativeImagePath();
    const QString nativePath = QString::fromStdString(native);
    check(QFileInfo(nativePath).absoluteFilePath() != QFileInfo(tifPath).absoluteFilePath(),
          "stack.tif: nativeImagePath is not the .tif");
    check(nativePath.endsWith(".nii.gz") && QFileInfo::exists(nativePath), "stack.tif: export is an existing .nii.gz");
    check(exportHolds(native, stackSize, stackValue), "stack.tif: export is 8 x 6 x 3 with the .tif voxels");

    const QString exportDir = QFileInfo(nativePath).absolutePath();
    const QString tempRoot = QDir::cleanPath(QDir::temp().absolutePath());
    check(QDir::cleanPath(exportDir) != tempRoot && QDir::cleanPath(QFileInfo(exportDir).absolutePath()) == tempRoot,
          "export lives in a per-window directory under temp, not in temp");

    const std::string planeExport = selectAndExport(*window, images, pngPath);
    check(QString::fromStdString(planeExport).endsWith(".nii.gz") && exportHolds(planeExport, planeSize, planeValue),
          "plane.png: export is 40 x 30 x 1 with the .png values");

    const std::string first = selectAndExport(*window, images, firstPath);
    check(exportHolds(first, planeSize, firstValue), "a/image.png: export holds a/image.png");
    const std::string second = selectAndExport(*window, images, secondPath);
    check(exportHolds(second, planeSize, secondValue), "b/image.png (same name): export holds b/image.png");
    const std::string firstAgain = selectAndExport(*window, images, firstPath);
    check(exportHolds(firstAgain, planeSize, firstValue), "a/image.png reselected: export holds a/image.png again");

    const ValueAt changedValue = [](unsigned int, unsigned int y, unsigned int) { return static_cast<float>(7 * y); };
    const bool rewrote = writeImage<2>(firstPath, planeSize, changedValue);
    images->setCurrentRow(rowForPath(images, pngPath));
    const std::string changed = selectAndExport(*window, images, firstPath);
    check(rewrote && exportHolds(changed, planeSize, changedValue),
          "a/image.png rewritten: reselected export holds the new file");

    window.reset();
    check(!QFileInfo::exists(nativePath) && !QFileInfo::exists(exportDir),
          "closing the window removes its export directory");

    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
