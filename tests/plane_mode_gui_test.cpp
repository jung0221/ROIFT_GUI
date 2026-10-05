// Drives the real window with a raster image, the input the plane mode exists for.
//
// oiftrelax and the Python helpers read files, not the volume in memory, and none
// of them reads PNG or TIFF; nativeImagePath() must hand them a NIfTI holding the
// voxels the window shows.
#include "ManualSeedSelector.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>
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

constexpr unsigned int kStackX = 8;
constexpr unsigned int kStackY = 6;
constexpr unsigned int kStackZ = 3;

float stackValue(unsigned int x, unsigned int y, unsigned int z)
{
    return static_cast<float>(x + kStackX * y + kStackX * kStackY * z);
}

// A three-page TIFF whose voxels are all distinct, so any reordering shows.
bool writeStack(const QString &path)
{
    using Stack = itk::Image<unsigned char, 3>;
    auto image = Stack::New();
    Stack::SizeType size;
    size[0] = kStackX;
    size[1] = kStackY;
    size[2] = kStackZ;
    Stack::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<Stack> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
    {
        const Stack::IndexType i = it.GetIndex();
        it.Set(static_cast<unsigned char>(stackValue(i[0], i[1], i[2])));
    }
    try
    {
        auto writer = itk::ImageFileWriter<Stack>::New();
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

using FloatVolume = itk::Image<float, 3>;

FloatVolume::Pointer readNifti(const std::string &path)
{
    try
    {
        auto reader = itk::ImageFileReader<FloatVolume>::New();
        reader->SetImageIO(itk::NiftiImageIO::New());
        reader->SetFileName(path);
        reader->Update();
        return reader->GetOutput();
    }
    catch (const itk::ExceptionObject &e)
    {
        std::printf("could not read %s as NIfTI: %s\n", path.c_str(), e.GetDescription());
        return nullptr;
    }
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
    const QString tifPath = QDir(workDir.path()).filePath("stack.tif");
    const bool wrote = writeStack(tifPath);
    check(wrote, "stack.tif written");
    if (!wrote)
        return 1;

    ManualSeedSelector window("");
    window.addImagesFromPaths({tifPath});
    check(window.hasImage(), "stack.tif opens as the image");

    const std::string native = window.nativeImagePath();
    const QString nativePath = QString::fromStdString(native);
    const bool exported = QFileInfo(nativePath).absoluteFilePath() != QFileInfo(tifPath).absoluteFilePath();
    check(exported, "nativeImagePath is not the .tif");
    check(nativePath.endsWith(".nii.gz"), "nativeImagePath ends with .nii.gz");
    check(QFileInfo::exists(nativePath), "nativeImagePath exists");

    FloatVolume::Pointer volume = (exported && QFileInfo::exists(nativePath)) ? readNifti(native) : nullptr;
    check(volume != nullptr, "the export reads as NIfTI");
    bool sized = false;
    bool same = false;
    if (volume)
    {
        const FloatVolume::SizeType size = volume->GetLargestPossibleRegion().GetSize();
        sized = size[0] == kStackX && size[1] == kStackY && size[2] == kStackZ;
        same = sized;
        for (unsigned int z = 0; same && z < kStackZ; ++z)
            for (unsigned int y = 0; same && y < kStackY; ++y)
                for (unsigned int x = 0; same && x < kStackX; ++x)
                {
                    FloatVolume::IndexType index;
                    index[0] = x;
                    index[1] = y;
                    index[2] = z;
                    same = volume->GetPixel(index) == stackValue(x, y, z);
                }
    }
    check(sized, "the export is 8 x 6 x 3");
    check(same, "the export holds the .tif voxels, x + 8y + 48z");

    if (exported)
        QFile::remove(nativePath);

    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
